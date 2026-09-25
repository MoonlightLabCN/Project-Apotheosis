// PortStorage.cpp — 见 .h。

#include "config.h"
#include "PortStorage.h"

#include "PortSQLiteAppContainer.h"

#include <WebCore/DatabaseProvider.h>
#include <WebCore/Document.h>
#include <WebCore/SocketProvider.h>
#include <WebCore/StorageNamespaceProvider.h>
#include <WebCore/WebTransportSession.h>

// WebKitLegacy 的单进程实现(经 PlatformWinUWP.cmake 编进 WebCore.lib;
// 头目录由 compile-driver-*.ps1 的 -I…\WebKitLegacy\Storage 提供)。
#include "InProcessIDBServer.h"
#include "WebDatabaseProvider.h"
#include "WebSocketChannel.h"
#include "WebStorageNamespaceProvider.h"

#include <wtf/FileSystem.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/text/MakeString.h>

namespace WebCorePort {

using namespace WebCore;

static String& profilePathSlot()
{
    static NeverDestroyed<String> path;
    return path.get();
}

void setPortProfilePath(const String& path)
{
    profilePathSlot() = path;
}

// 建目录并返回;根路径没设、或建不出来 → 返回空串,调用方据此退化为非持久。
// 绝不返回一个"看起来对但写不进去"的路径:那会让 SQLite 在 App Container 里以最难查的方式失败。
static String ensureProfileSubdirectory(ASCIILiteral name)
{
    const String& root = profilePathSlot();
    if (root.isEmpty())
        return String();
    auto directory = FileSystem::pathByAppendingComponent(root, name);
    if (!FileSystem::makeAllDirectories(directory))
        return String();
    return directory;
}

String portLocalStorageDirectory()
{
    return ensureProfileSubdirectory("storage"_s);
}

String portIndexedDBDirectory()
{
    return ensureProfileSubdirectory("indexeddb"_s);
}

// localStorage / IndexedDB 的持久化底座都是 SQLite 真实文件。这个构建里 SQLite 打开真实文件
// 会经空函数指针崩(见 PortSQLiteAppContainer.h 的完整根因)。0.2.0 补了缺失的系统调用项,
// 但只有 installSQLiteAppContainerSyscalls() 确认全部就位才敢往磁盘写;否则一律退回内存态。
// 宁可"关掉应用就没了",也不要闪退 —— 这是 0.1.8.8 cookie 那次踩出来的判断。
static bool persistentStorageAllowed()
{
    installSQLiteAppContainerSyscalls();
    return sqliteFileIOAvailable();
}

Ref<StorageNamespaceProvider> makeStorageNamespaceProvider()
{
    String path;
    if (persistentStorageAllowed())
        path = portLocalStorageDirectory();

    // 路径为空 = StorageNamespaceImpl 不建 StorageSyncManager → 纯内存 StorageMap。
    // 语义仍然是对的(origin 隔离、配额、storage 事件、session/local 区分都在),只是不落盘。
    return WebKit::WebStorageNamespaceProvider::create(path);
}

Ref<DatabaseProvider> makeDatabaseProvider()
{
    return WebDatabaseProvider::singleton();
}

// ---------------------------------------------------------------------------
// SocketProvider。上游的 LegacySocketProvider.cpp 用的是 ObjC 风格的 #import 且只在
// Cocoa 构建里编,不适合直接拿来;它本身也只有这两个函数,原样重写一遍比改上游干净。
// ---------------------------------------------------------------------------
namespace {

class PortSocketProvider final : public SocketProvider {
public:
    static Ref<PortSocketProvider> create() { return adoptRef(*new PortSocketProvider); }

private:
    RefPtr<ThreadableWebSocketChannel> createWebSocketChannel(Document& document, WebSocketChannelClient& client) final
    {
        return WebCore::WebSocketChannel::create(document, client, *this);
    }

    // WebTransport 本轮不做(它需要 HTTP/3,curl 后端这里没开 QUIC)。明确拒绝,
    // 而不是返回一个永远不 settle 的 promise —— 后者会让页面挂着等。
    std::pair<RefPtr<WebTransportSession>, Ref<WebTransportSessionPromise>> initializeWebTransportSession(ScriptExecutionContext&, WebTransportSessionClient&, const URL&, const WebTransportOptions&) final
    {
        return { nullptr, WebTransportSessionPromise::createAndReject() };
    }
};

} // anonymous namespace

Ref<SocketProvider> makeSocketProvider()
{
    return PortSocketProvider::create();
}

void flushLocalStorage()
{
    WebKit::WebStorageNamespaceProvider::syncLocalStorage();
}

String portStorageStatusLine()
{
    bool persistent = persistentStorageAllowed();
    return makeString("storage persistent="_s, persistent ? "yes"_s : "no"_s,
        " local="_s, persistent ? portLocalStorageDirectory() : String("(memory)"_s),
        " idb="_s, persistent ? portIndexedDBDirectory() : String("(memory)"_s),
        " sqlite-missing="_s, String::fromUTF8(sqliteMissingSyscalls()));
}

} // namespace WebCorePort

// WebDatabaseProvider(全局命名空间,WebKitLegacy/Storage/WebDatabaseProvider.h)把 IDB 数据
// 目录的选择留给平台 —— 上游只有 mac 版(WebKitLegacy/mac/Storage/WebDatabaseProvider.mm)。
// 这里给 WinUWP 版:LocalState\profile\indexeddb。返回空串时 WebDatabaseProvider 会改用
// InProcessIDBServer::create(sessionID) 的内存态构造,IDB 可用但不跨启动保留。
WTF::String WebDatabaseProvider::indexedDatabaseDirectoryPath()
{
    if (!WebCorePort::persistentStorageAllowed())
        return WTF::String();
    return WebCorePort::portIndexedDBDirectory();
}
