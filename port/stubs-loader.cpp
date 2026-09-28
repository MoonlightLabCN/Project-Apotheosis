// stubs-loader.cpp — Phase 1b 接入 curl 网络后端后链接残留的符号。
//  - WebResourceLoadScheduler 的 10 个 xxxError 工厂(LoaderStrategy 纯虚,upstream 由嵌入层
//    实现;本端无嵌入层 → 返回通用 ResourceError)。
//  - CurlSSLHandle::platformInitialize(跨平台版被 OS(WINDOWS) 守掉,Win 版 CurlSSLHandleWin.cpp
//    已排除 → 空 no-op;TLS 根证书走打包的 cacert.pem + setCACertPath)。
// 0.2.0:CryptoAlgorithmRSA_PSS::platformSign/Verify 已由 crypto/openssl/
// CryptoAlgorithmRSA_PSSOpenSSL.cpp 提供,不可再在这里出 NotSupported 假实现(会 LNK2005)。
#include "config.h"

#include "WebResourceLoadScheduler.h"
#include "ResourceError.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "CurlSSLHandle.h"
#include <wtf/Assertions.h>

using namespace WebCore;

// ---- WebResourceLoadScheduler 错误工厂(全局命名空间类)----
// Apotheosis (2026-09-27): every factory used to hand back the same
// ResourceError(domain, 0, url, desc) - errorCode 0 and Type::General regardless
// of what happened. Callers that branch on the code/type (FrameLoader's failure
// classification, the driver's retry judgment, isCancellation(), the
// https-only/https-upgrade recovery) all collapsed to "unknown error", so
// blocked vs cancelled vs httpsOnly was indistinguishable in the logs AND in
// behavior. Each factory now carries its own stable code and the closest
// ResourceErrorBase::Type; the messages are unchanged.
static constexpr int kStubErrCancelled              = 100;
static constexpr int kStubErrBlocked                = 101;
static constexpr int kStubErrBlockedByContentBlocker = 102;
static constexpr int kStubErrCannotShowURL          = 103;
static constexpr int kStubErrInterruptedForPolicy   = 104;
static constexpr int kStubErrHTTPSUpgradeLoop       = 105;
static constexpr int kStubErrHTTPSOnly              = 106;
static constexpr int kStubErrCannotShowMIMEType     = 107;
static constexpr int kStubErrFileDoesNotExist       = 108;
static constexpr int kStubErrPluginWillHandleLoad   = 109;

static ResourceError webKitStubError(const URL& url, ASCIILiteral desc, int code, ResourceError::Type type)
{
    return ResourceError("WebKitErrorDomain"_s, code, url, desc, type);
}
ResourceError WebResourceLoadScheduler::cancelledError(const ResourceRequest& r) const { return webKitStubError(r.url(), "cancelled"_s, kStubErrCancelled, ResourceError::Type::Cancellation); }
ResourceError WebResourceLoadScheduler::blockedError(const ResourceRequest& r) const { return webKitStubError(r.url(), "blocked"_s, kStubErrBlocked, ResourceError::Type::General); }
ResourceError WebResourceLoadScheduler::blockedByContentBlockerError(const ResourceRequest& r) const { return webKitStubError(r.url(), "blocked by content blocker"_s, kStubErrBlockedByContentBlocker, ResourceError::Type::General); }
ResourceError WebResourceLoadScheduler::cannotShowURLError(const ResourceRequest& r) const { return webKitStubError(r.url(), "cannot show URL"_s, kStubErrCannotShowURL, ResourceError::Type::General); }
ResourceError WebResourceLoadScheduler::interruptedForPolicyChangeError(const ResourceRequest& r) const { return webKitStubError(r.url(), "interrupted for policy change"_s, kStubErrInterruptedForPolicy, ResourceError::Type::Cancellation); }
ResourceError WebResourceLoadScheduler::httpsUpgradeRedirectLoopError(const ResourceRequest& r) const { return webKitStubError(r.url(), "https upgrade redirect loop"_s, kStubErrHTTPSUpgradeLoop, ResourceError::Type::General); }
ResourceError WebResourceLoadScheduler::httpNavigationWithHTTPSOnlyError(const ResourceRequest& r) const { return webKitStubError(r.url(), "http navigation with https-only"_s, kStubErrHTTPSOnly, ResourceError::Type::General); }
ResourceError WebResourceLoadScheduler::cannotShowMIMETypeError(const ResourceResponse& r) const { return webKitStubError(r.url(), "cannot show MIME type"_s, kStubErrCannotShowMIMEType, ResourceError::Type::General); }
ResourceError WebResourceLoadScheduler::fileDoesNotExistError(const ResourceResponse& r) const { return webKitStubError(r.url(), "file does not exist"_s, kStubErrFileDoesNotExist, ResourceError::Type::General); }
ResourceError WebResourceLoadScheduler::pluginWillHandleLoadError(const ResourceResponse& r) const { return webKitStubError(r.url(), "plugin will handle load"_s, kStubErrPluginWillHandleLoad, ResourceError::Type::Cancellation); }

namespace WebCore {

// ---- CurlSSLHandle 平台初始化(Win 版已排除;空 no-op,CA 走 cacert.pem)----
void CurlSSLHandle::platformInitialize() { }

} // namespace WebCore
