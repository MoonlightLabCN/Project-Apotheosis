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
static ResourceError webKitStubError(const URL& url, ASCIILiteral desc)
{
    return ResourceError("WebKitErrorDomain"_s, 0, url, desc);
}
ResourceError WebResourceLoadScheduler::cancelledError(const ResourceRequest& r) const { return webKitStubError(r.url(), "cancelled"_s); }
ResourceError WebResourceLoadScheduler::blockedError(const ResourceRequest& r) const { return webKitStubError(r.url(), "blocked"_s); }
ResourceError WebResourceLoadScheduler::blockedByContentBlockerError(const ResourceRequest& r) const { return webKitStubError(r.url(), "blocked by content blocker"_s); }
ResourceError WebResourceLoadScheduler::cannotShowURLError(const ResourceRequest& r) const { return webKitStubError(r.url(), "cannot show URL"_s); }
ResourceError WebResourceLoadScheduler::interruptedForPolicyChangeError(const ResourceRequest& r) const { return webKitStubError(r.url(), "interrupted for policy change"_s); }
ResourceError WebResourceLoadScheduler::httpsUpgradeRedirectLoopError(const ResourceRequest& r) const { return webKitStubError(r.url(), "https upgrade redirect loop"_s); }
ResourceError WebResourceLoadScheduler::httpNavigationWithHTTPSOnlyError(const ResourceRequest& r) const { return webKitStubError(r.url(), "http navigation with https-only"_s); }
ResourceError WebResourceLoadScheduler::cannotShowMIMETypeError(const ResourceResponse& r) const { return webKitStubError(r.url(), "cannot show MIME type"_s); }
ResourceError WebResourceLoadScheduler::fileDoesNotExistError(const ResourceResponse& r) const { return webKitStubError(r.url(), "file does not exist"_s); }
ResourceError WebResourceLoadScheduler::pluginWillHandleLoadError(const ResourceResponse& r) const { return webKitStubError(r.url(), "plugin will handle load"_s); }

namespace WebCore {

// ---- CurlSSLHandle 平台初始化(Win 版已排除;空 no-op,CA 走 cacert.pem)----
void CurlSSLHandle::platformInitialize() { }

} // namespace WebCore
