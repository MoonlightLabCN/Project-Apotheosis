// PortSocketStreamHandle.cpp — WebSocket 的字节流层(curl 版 SocketStreamHandleImpl)。
//
// 类声明在 Source/WebKitLegacy/WebCoreSupport/SocketStreamHandleImpl.h 的 WK_WINUWP 分支,
// 那里有为什么这么分层的完整说明。一句话:协议部分(握手/分帧/permessage-deflate/关闭握手)
// 完全复用 WebKitLegacy 的 WebSocketChannel + WebCore 的 WebSocketHandshake/WebSocketFrame,
// 本文件只提供它需要的"能连、能收、能发、能关"的 TCP/TLS 流,底座是 WebCore 自己的
// CurlStream/CurlStreamScheduler(WebKit 的 curl WebSocket 后端用的同一套)。
//
// 线程:CurlStreamScheduler 在自己的 worker 线程上跑 select(),但所有回调都经
// callClientOnMainThread 投回主线程(= 本 port 的引擎线程)。因此本文件里的一切都在引擎线程,
// 与"引擎线程绝不同步等 UI 线程"的规矩不冲突 —— 这里根本不碰 UI 线程。

#include "config.h"

#include "SocketStreamHandleImpl.h"

#include <WebCore/CookieRequestHeaderFieldProxy.h>
#include <WebCore/CurlContext.h>
#include <WebCore/CurlStream.h>
#include <WebCore/CurlStreamScheduler.h>
#include <WebCore/NetworkStorageSession.h>
#include <WebCore/SharedBuffer.h>
#include <WebCore/SocketStreamError.h>
#include <WebCore/StorageSessionProvider.h>
#include "SocketStreamHandleClient.h"
#include <wtf/MainThread.h>
#include <wtf/UniqueArray.h>
#include <wtf/text/CString.h>
#include <wtf/text/StringBuilder.h>

// profile 的唯一网络会话(定义在 PortNetworkStorageSession.cpp)。握手 cookie 走它,
// 与 HTTP 路、document.cookie 是同一个 jar —— WebSocket 不另开 cookie store。
extern "C" WebCore::NetworkStorageSession* WebCorePortDefaultStorageSession();

namespace WebCore {

namespace {

class CurlSocketStreamHandle final : public SocketStreamHandleImpl, public CurlStream::Client {
public:
    CurlSocketStreamHandle(const URL& url, SocketStreamHandleClient& client, const StorageSessionProvider* provider, bool shouldAcceptInsecureCertificates)
        : SocketStreamHandleImpl(url, client)
        , m_storageSessionProvider(provider)
        , m_scheduler(CurlContext::singleton().streamScheduler())
    {
        ASSERT(isMainThread());
        m_streamID = m_scheduler.createStream(m_url, *this,
            shouldAcceptInsecureCertificates ? CurlStream::ServerTrustEvaluation::Disable : CurlStream::ServerTrustEvaluation::Enable,
            CurlStream::LocalhostAlias::Disable);
    }

    ~CurlSocketStreamHandle() override
    {
        ASSERT(isMainThread());
        destructStream();
    }

    // ---- SocketStreamHandle ----------------------------------------------
    void platformSend(std::span<const uint8_t> data, Function<void(bool)>&& completionHandler) final
    {
        completionHandler(sendBytes(data));
    }

    // 握手报文。cookie 由 WebSocketChannel 以 CookieRequestHeaderFieldProxy 的形式交下来
    // (它自己不查 jar),这里向 profile 的唯一 NetworkStorageSession 取值后插进握手头。
    // 不做这一步,登录态站点的 wss 握手就是匿名的 —— 服务器会用 401/403 打回来。
    void platformSendHandshake(std::span<const uint8_t> data, const std::optional<CookieRequestHeaderFieldProxy>& headerFieldProxy, Function<void(bool, bool)>&& completionHandler) final
    {
        bool didAccessSecureCookies = false;
        Vector<uint8_t> message;

        String cookieHeader;
        if (headerFieldProxy) {
            if (auto* session = storageSession()) {
                auto result = session->cookieRequestHeaderFieldValue(*headerFieldProxy);
                cookieHeader = result.first;
                didAccessSecureCookies = result.second;
            }
        }

        if (cookieHeader.isEmpty())
            message.append(data);
        else {
            // 握手报文以空行(\r\n\r\n)结束。Cookie 头插在结尾空行之前。
            auto cookieLine = makeString("Cookie: "_s, cookieHeader, "\r\n"_s).utf8();
            size_t terminator = findHeaderTerminator(data);
            if (terminator == notFound) {
                message.append(data);
            } else {
                message.append(data.first(terminator));
                message.append(std::span<const uint8_t> { byteCast<uint8_t>(cookieLine.data()), cookieLine.length() });
                message.append(data.subspan(terminator));
            }
        }

        bool success = sendBytes(message.span());
        completionHandler(success, didAccessSecureCookies);
    }

    void platformClose() final
    {
        ASSERT(isMainThread());
        destructStream();
        // 主动关闭也要通知上层,否则 WebSocketChannel 会一直等一个永远不来的 close。
        m_client.didCloseSocketStream(*this);
    }

    size_t bufferedAmount() final { return m_totalSendDataSize; }

private:
    static size_t findHeaderTerminator(std::span<const uint8_t> data)
    {
        for (size_t i = 0; i + 3 < data.size(); ++i) {
            if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n')
                return i + 2;   // 插在最后那个 CRLF 之前
        }
        return notFound;
    }

    NetworkStorageSession* storageSession() const
    {
        if (m_storageSessionProvider) {
            if (auto* session = m_storageSessionProvider->storageSession())
                return session;
        }
        return WebCorePortDefaultStorageSession();
    }

    bool sendBytes(std::span<const uint8_t> data)
    {
        ASSERT(isMainThread());
        if (m_streamID == invalidCurlStreamID || !data.size())
            return false;

        auto buffer = makeUniqueArray<uint8_t>(data.size());
        memcpySpan(std::span<uint8_t> { buffer.get(), data.size() }, data);
        m_scheduler.send(m_streamID, WTF::move(buffer), data.size());
        m_totalSendDataSize += data.size();
        return true;
    }

    void destructStream()
    {
        if (m_streamID == invalidCurlStreamID)
            return;
        m_scheduler.destroyStream(m_streamID);
        m_streamID = invalidCurlStreamID;
    }

    // ---- CurlStream::Client(全部在主线程被调用)---------------------------
    void didOpen(CurlStreamID) final
    {
        ASSERT(isMainThread());
        if (m_state != Connecting)
            return;
        m_state = Open;
        m_client.didOpenSocketStream(*this);
    }

    void didSendData(CurlStreamID, size_t length) final
    {
        ASSERT(isMainThread());
        m_totalSendDataSize -= std::min(m_totalSendDataSize, length);
        m_client.didUpdateBufferedAmount(*this, bufferedAmount());
    }

    void didReceiveData(CurlStreamID, const SharedBuffer& buffer) final
    {
        ASSERT(isMainThread());
        if (m_state != Open)
            return;

        // CurlStream::tryToReceive() 读到 0 字节 = 对端关闭:它已经 destroyHandle(),
        // 然后仍会把这个空 buffer 投上来。这是本层唯一能看到"对端正常断开"的信号。
        if (!buffer.size()) {
            destructStream();
            m_state = Closed;
            m_client.didCloseSocketStream(*this);
            return;
        }

        m_client.didReceiveSocketStreamData(*this, buffer.span());
    }

    void didFail(CurlStreamID, CURLcode errorCode, CertificateInfo&&) final
    {
        ASSERT(isMainThread());
        destructStream();
        if (m_state == Closed)
            return;
        m_state = Closed;
        m_client.didFailSocketStream(*this, SocketStreamError(static_cast<int>(errorCode), m_url.string(),
            makeString("WebSocket network error (curl "_s, static_cast<int>(errorCode), ")"_s)));
    }

    RefPtr<const StorageSessionProvider> m_storageSessionProvider;
    CurlStreamScheduler& m_scheduler;
    CurlStreamID m_streamID { invalidCurlStreamID };
    size_t m_totalSendDataSize { 0 };
};

} // anonymous namespace

Ref<SocketStreamHandleImpl> SocketStreamHandleImpl::create(const URL& url, SocketStreamHandleClient& client, PAL::SessionID, const String&, SourceApplicationAuditToken&&, const StorageSessionProvider* provider, bool shouldAcceptInsecureCertificates)
{
    return adoptRef(*new CurlSocketStreamHandle(url, client, provider, shouldAcceptInsecureCertificates));
}

SocketStreamHandleImpl::~SocketStreamHandleImpl() = default;

} // namespace WebCore
