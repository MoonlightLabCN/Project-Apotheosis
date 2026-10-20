/*
 * Copyright (C) 2004-2017 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "ResourceHandle.h"
#include "ResourceHandleInternal.h"

#include "DNS.h"
#include "Logging.h"
#include "NetworkingContext.h"
#include "NotImplemented.h"
#include "ResourceHandleClient.h"
#include "SecurityOrigin.h"
#include "Timer.h"
#include "TimingAllowOrigin.h"
#include <algorithm>
#include <wtf/CompletionHandler.h>
#include <wtf/MainThread.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/text/AtomStringHash.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>

#if USE(CURL) && defined(WK_WINUWP)
#include "CertificateInfo.h"
#include "CurlContext.h"
#include "CurlRequest.h"
#include "CurlRequestClient.h"
#include "CurlResponse.h"
#include "HTTPHeaderNames.h"
#include "NetworkLoadMetrics.h"
#include "ProtectionSpace.h"
#include "ResourceError.h"
#include "ResourceResponse.h"
#include "SharedBuffer.h"
#include <wtf/text/StringCommon.h>
#endif

namespace WebCore {

static bool shouldForceContentSniffing;

DEFINE_ALLOCATOR_WITH_HEAP_IDENTIFIER(ResourceHandleInternal);

typedef HashMap<AtomString, ResourceHandle::BuiltinConstructor> BuiltinResourceHandleConstructorMap;
static BuiltinResourceHandleConstructorMap& builtinResourceHandleConstructorMap()
{
#if PLATFORM(IOS_FAMILY)
    ASSERT(WebThreadIsLockedOrDisabled());
#else
    ASSERT(isMainThread());
#endif
    static NeverDestroyed<BuiltinResourceHandleConstructorMap> map;
    return map;
}

void ResourceHandle::registerBuiltinConstructor(const AtomString& protocol, ResourceHandle::BuiltinConstructor constructor)
{
    builtinResourceHandleConstructorMap().add(protocol, constructor);
}

typedef HashMap<AtomString, ResourceHandle::BuiltinSynchronousLoader> BuiltinResourceHandleSynchronousLoaderMap;
static BuiltinResourceHandleSynchronousLoaderMap& builtinResourceHandleSynchronousLoaderMap()
{
    ASSERT(isMainThread());
    static NeverDestroyed<BuiltinResourceHandleSynchronousLoaderMap> map;
    return map;
}

void ResourceHandle::registerBuiltinSynchronousLoader(const AtomString& protocol, ResourceHandle::BuiltinSynchronousLoader loader)
{
    builtinResourceHandleSynchronousLoaderMap().add(protocol, loader);
}

ResourceHandle::ResourceHandle(NetworkingContext* context, const ResourceRequest& request, ResourceHandleClient* client, bool defersLoading, bool shouldContentSniff, ContentEncodingSniffingPolicy contentEncodingSniffingPolicy, RefPtr<SecurityOrigin>&& sourceOrigin, bool isMainFrameNavigation)
    : d(makeUnique<ResourceHandleInternal>(this, context, request, client, defersLoading, shouldContentSniff && shouldContentSniffURL(request.url()), contentEncodingSniffingPolicy, WTF::move(sourceOrigin), isMainFrameNavigation))
{
    if (!request.url().isValid()) {
        scheduleFailure(InvalidURLFailure);
        return;
    }

    if (!portAllowed(request.url()) || isIPAddressDisallowed(request.url())) {
        scheduleFailure(BlockedFailure);
        return;
    }
}

RefPtr<ResourceHandle> ResourceHandle::create(NetworkingContext* context, const ResourceRequest& request, ResourceHandleClient* client, bool defersLoading, bool shouldContentSniff, ContentEncodingSniffingPolicy contentEncodingSniffingPolicy, RefPtr<SecurityOrigin>&& sourceOrigin, bool isMainFrameNavigation)
{
    if (auto protocol = request.url().protocol().toExistingAtomString(); !protocol.isNull()) {
        if (auto constructor = builtinResourceHandleConstructorMap().get(protocol))
            return constructor(request, client);
    }

    auto newHandle = adoptRef(*new ResourceHandle(context, request, client, defersLoading, shouldContentSniff, contentEncodingSniffingPolicy, WTF::move(sourceOrigin), isMainFrameNavigation));

    if (newHandle->d->m_scheduledFailureType != NoFailure)
        return newHandle;

    if (newHandle->start())
        return newHandle;

    return nullptr;
}

void ResourceHandle::scheduleFailure(FailureType type)
{
    d->m_scheduledFailureType = type;
    d->m_failureTimer.startOneShot(0_s);
}

void ResourceHandle::failureTimerFired()
{
    if (!client())
        return;

    switch (d->m_scheduledFailureType) {
        case NoFailure:
            ASSERT_NOT_REACHED();
            return;
        case BlockedFailure:
            d->m_scheduledFailureType = NoFailure;
            client()->wasBlocked(this);
            return;
        case InvalidURLFailure:
            d->m_scheduledFailureType = NoFailure;
            client()->cannotShowURL(this);
            return;
    }

    ASSERT_NOT_REACHED();
}

void ResourceHandle::loadResourceSynchronously(NetworkingContext* context, const ResourceRequest& request, StoredCredentialsPolicy storedCredentialsPolicy, SecurityOrigin* sourceOrigin, ResourceError& error, ResourceResponse& response, Vector<uint8_t>& data)
{
    if (auto protocol = request.url().protocol().toExistingAtomString(); !protocol.isNull()) {
        if (auto constructor = builtinResourceHandleSynchronousLoaderMap().get(protocol)) {
            constructor(context, request, storedCredentialsPolicy, error, response, data);
            return;
        }
    }

    platformLoadResourceSynchronously(context, request, storedCredentialsPolicy, sourceOrigin, error, response, data);
}

ResourceHandleClient* ResourceHandle::client() const
{
    return d->m_client;
}

void ResourceHandle::clearClient()
{
    d->m_client = nullptr;
}

void ResourceHandle::didReceiveResponse(ResourceResponse&& response, CompletionHandler<void()>&& completionHandler)
{
    if (response.isHTTP09()) {
        auto url = response.url();
        std::optional<uint16_t> port = url.port();
        if (port && !WTF::isDefaultPortForProtocol(port.value(), url.protocol())) {
#if defined(WK_WINUWP)
            // Apotheosis: our cancel() clears d->m_client (ResourceLoader::cancel()
            // nulls its own m_handle before releaseResources() -> finishNetworkLoad()
            // can call clearClient(), so without that the handle would keep a
            // dangling client pointer across every later async hop). Take the
            // client into a local *before* cancelling so this HTTP/0.9 failure
            // still reaches the loader instead of dereferencing a null pointer.
            auto* clientBeforeCancel = client();
            cancel();
            auto message = makeString("Cancelled load from '"_s, url.stringCenterEllipsizedToLength(), "' because it is using HTTP/0.9."_s);
            if (clientBeforeCancel)
                clientBeforeCancel->didFail(this, { String(), 0, url, message });
#else
            cancel();
            auto message = makeString("Cancelled load from '"_s, url.stringCenterEllipsizedToLength(), "' because it is using HTTP/0.9."_s);
            d->m_client->didFail(this, { String(), 0, url, message });
#endif
            completionHandler();
            return;
        }
    }
#if defined(WK_WINUWP)
    // Apotheosis: the load can be cancelled between curlDidReceiveResponse()'s
    // client() check and here (willSendRequestAsync/policy hops run nested run
    // loops in this port); completeDidReceiveResponse() is a no-op on a
    // cancelled CurlRequest, so just unblock the transfer and bail.
    if (!client()) {
        completionHandler();
        return;
    }
#endif
    client()->didReceiveResponseAsync(this, WTF::move(response), WTF::move(completionHandler));
}

ResourceRequest& ResourceHandle::firstRequest()
{
    return d->m_firstRequest;
}

const ResourceRequest& ResourceHandle::firstRequest() const
{
    return d->m_firstRequest;
}

NetworkingContext* ResourceHandle::context() const
{
    return d->m_context.get();
}

const String& ResourceHandle::lastHTTPMethod() const
{
    return d->m_lastHTTPMethod;
}

bool ResourceHandle::hasAuthenticationChallenge() const
{
    return !d->m_currentWebChallenge.isNull();
}

void ResourceHandle::clearAuthentication()
{
#if PLATFORM(COCOA)
    d->m_currentMacChallenge = nil;
#endif
    d->m_currentWebChallenge.nullify();
}

bool ResourceHandle::failsTAOCheck() const
{
    return d->m_failsTAOCheck;
}

void ResourceHandle::checkTAO(const ResourceResponse& response)
{
    if (d->m_failsTAOCheck)
        return;

    RefPtr<SecurityOrigin> origin;
    if (d->m_isMainFrameNavigation)
        origin = SecurityOrigin::create(firstRequest().url());
    else
        origin = d->m_sourceOrigin;

    if (origin)
        d->m_failsTAOCheck = !passesTimingAllowOriginCheck(response, *origin);
}

bool ResourceHandle::hasCrossOriginRedirect() const
{
    return d->m_hasCrossOriginRedirect;
}

void ResourceHandle::markAsHavingCrossOriginRedirect()
{
    d->m_hasCrossOriginRedirect = true;
}

void ResourceHandle::incrementRedirectCount()
{
    d->m_redirectCount++;
}

uint16_t ResourceHandle::redirectCount() const
{
    return d->m_redirectCount;
}

MonotonicTime ResourceHandle::startTimeBeforeRedirects() const
{
    return d->m_startTime;
}

NetworkLoadMetrics* ResourceHandle::networkLoadMetrics()
{
    return d->m_networkLoadMetrics.get();
}

void ResourceHandle::setNetworkLoadMetrics(Box<NetworkLoadMetrics>&& metrics)
{
    d->m_networkLoadMetrics = WTF::move(metrics);
}

bool ResourceHandle::shouldContentSniff() const
{
    return d->m_shouldContentSniff;
}

ContentEncodingSniffingPolicy ResourceHandle::contentEncodingSniffingPolicy() const
{
    return d->m_contentEncodingSniffingPolicy;
}

bool ResourceHandle::shouldContentSniffURL(const URL& url)
{
#if PLATFORM(COCOA)
    if (shouldForceContentSniffing)
        return true;
#endif
    // We shouldn't content sniff file URLs as their MIME type should be established via their extension.
    return !url.protocolIsFile();
}

void ResourceHandle::forceContentSniffing()
{
    shouldForceContentSniffing = true;
}

void ResourceHandle::setDefersLoading(bool defers)
{
    LOG(Network, "Handle %p setDefersLoading(%s)", this, defers ? "true" : "false");

    ASSERT(d->m_defersLoading != defers); // Deferring is not counted, so calling setDefersLoading() repeatedly is likely to be in error.
    d->m_defersLoading = defers;

    if (defers) {
        ASSERT(d->m_failureTimer.isActive() == (d->m_scheduledFailureType != NoFailure));
        if (d->m_failureTimer.isActive())
            d->m_failureTimer.stop();
    } else if (d->m_scheduledFailureType != NoFailure) {
        ASSERT(!d->m_failureTimer.isActive());
        d->m_failureTimer.startOneShot(0_s);
    }

    platformSetDefersLoading(defers);
}

#if USE(CURL) && defined(WK_WINUWP)
// ============================================================================
// In-process curl network bridge (EdgeHTML Reborn — WinUWP ARM32).
// Drives ResourceHandle -> CurlRequest directly inside WebCore.lib (these
// symbols are compiled into WebCore.lib; a separate TU would LNK2005).
// ============================================================================

// --- ref-counted forwarder: curl callbacks -> ResourceHandleClient ----------
// CurlRequest holds a Ref<> to its client (CurlRequestClient : AbstractRefCounted).
// ResourceHandleInternal is not refcounted, so this thin object IS the
// CurlRequestClient and forwards into the handle. invalidate() severs the
// back-pointer on cancel/destruction so late curl callbacks are dropped safely.
class CurlResourceHandleDelegate final : public CurlRequestClient {
public:
    static Ref<CurlResourceHandleDelegate> create(ResourceHandle& handle)
    {
        return adoptRef(*new CurlResourceHandleDelegate(handle));
    }

    void ref() const final { ++m_refCount; }
    void deref() const final { if (!--m_refCount) delete this; }

    void invalidate() { m_handle = nullptr; }

    // CurlRequestClient
    void curlDidSendData(CurlRequest&, unsigned long long bytesSent, unsigned long long totalToSend) final
    {
        if (RefPtr handle = m_handle)
            handle->curlDidSendData(bytesSent, totalToSend);
    }
    void curlDidReceiveResponse(CurlRequest& request, CurlResponse&& response) final
    {
        if (RefPtr handle = m_handle)
            handle->curlDidReceiveResponse(request, WTF::move(response));
    }
    void curlDidReceiveData(CurlRequest&, Ref<SharedBuffer>&& buffer) final
    {
        if (RefPtr handle = m_handle)
            handle->curlDidReceiveData(WTF::move(buffer));
    }
    void curlDidComplete(CurlRequest&, NetworkLoadMetrics&& metrics) final
    {
        if (RefPtr handle = m_handle)
            handle->curlDidComplete(WTF::move(metrics));
    }
    void curlDidFailWithError(CurlRequest&, ResourceError&& error, CertificateInfo&&) final
    {
        if (RefPtr handle = m_handle)
            handle->curlDidFailWithError(WTF::move(error));
    }

private:
    explicit CurlResourceHandleDelegate(ResourceHandle& handle)
        : m_handle(&handle) { }

    mutable uint32_t m_refCount { 1 };
    RefPtr<ResourceHandle> m_handle;
};

// Apotheosis: break the ResourceHandle <-> CurlResourceHandleDelegate reference
// cycle.
//
// Ownership as built by start(): ResourceLoader -RefPtr-> ResourceHandle
// -unique_ptr-> ResourceHandleInternal -RefPtr-> CurlResourceHandleDelegate
// -RefPtr-> ResourceHandle. ResourceHandleInternal also holds
// RefPtr<CurlRequest>, and CurlRequest keeps the delegate alive transiently
// (CurlRequest::m_client is a raw pointer; callClient() takes Ref { *m_client }
// for the duration of a main-thread callback).
//
// The delegate's strong back-reference is only ever severed by invalidate(),
// which used to run on exactly two paths: cancel() and the redirect teardown.
// ~ResourceHandle() also calls it, but the cycle makes that destructor
// unreachable. So a load that simply *finished* (curlDidComplete) or *failed*
// (curlDidFailWithError) left the cycle intact: the ResourceLoader dropped its
// ref in finishNetworkLoad(), and handle + internal + delegate + CurlRequest
// (which owns an isolatedCopy of the request, the response and its header maps)
// leaked for the lifetime of the process - once per resource, on a 32-bit
// address space.
//
// Called on the terminal paths below. Takes its own protecting Ref because
// dropping the delegate's back-reference can be the last ref on the handle, and
// clears the members *before* invalidating, so a callback still queued on the
// main thread (which holds its own Ref via callClient) is neutered rather than
// destroyed under itself.
static void releaseCurlResources(ResourceHandle& handle, ResourceHandleInternal* d)
{
    Ref protectHandle { handle };

    if (RefPtr delegate = WTF::move(d->m_curlDelegate)) {
        d->m_curlDelegate = nullptr;
        delegate->invalidate();
    }
    if (RefPtr request = WTF::move(d->m_curlRequest)) {
        d->m_curlRequest = nullptr;
        // Apotheosis: cancel(), not just invalidateClient(). A 3xx transfer is *paused* when we
        // get here - willPerformHTTPRedirection() never calls completeDidReceiveResponse() - so
        // the terminal redirect paths (too many redirects, invalid Location, client aborted,
        // handle cancelled while the policy check was pending) left a paused transfer parked in
        // CurlRequestScheduler for the lifetime of the session, permanently holding one of the
        // eight per-host connection slots of f0c5f48ec9. cancel() is idempotent (CurlRequest.cpp
        // returns early on m_cancelled) and ends with invalidateClient() itself, so the path
        // through ResourceHandle::cancel(), which cancels first, is unchanged.
        request->cancel();
    }
}

ResourceHandleInternal::~ResourceHandleInternal() = default;

ResourceHandle::~ResourceHandle()
{
    // Make sure no in-flight curl callback can touch a destroyed handle.
    if (d->m_curlDelegate)
        d->m_curlDelegate->invalidate();
    if (d->m_curlRequest)
        d->m_curlRequest->invalidateClient();
}

// Apotheosis: 子资源加载诊断计数。每个网络请求(主文档 + 全部子资源:CSS/JS/图片)都经此桥,
// 在 start/response/complete/fail 四处打点,计数由驱动 WebCorePortBumpLoad 累加并并入 WebCoreGetDiag。
// kind: 0=started 1=response 2=complete 3=fail。驱动未链接时为弱符号无操作。
extern "C" void WebCorePortBumpLoad(int kind);

bool ResourceHandle::start()
{
    ASSERT(isMainThread());

    WebCorePortBumpLoad(0);

    // Ensure libcurl + scheduler thread are up. Idempotent.
    CurlContext::singleton();

    ResourceRequest request = d->m_firstRequest;

    // Credentials embedded in the URL were stripped into m_user/m_password by
    // ResourceHandleInternal's ctor; re-apply them onto the CurlRequest below.
    d->m_curlDelegate = CurlResourceHandleDelegate::create(*this);
    d->m_curlRequest = CurlRequest::create(request, *d->m_curlDelegate,
        CurlRequest::CaptureNetworkLoadMetrics::Basic);

    if (!d->m_user.isEmpty() || !d->m_password.isEmpty()) {
        d->m_curlRequest->setUserPass(d->m_user, d->m_password);
        d->m_curlRequest->setAuthenticationScheme(ProtectionSpace::AuthenticationScheme::HTTPBasic);
    }

    // CurlRequest is created suspended; resume() hands it to the scheduler thread.
    if (!d->m_defersLoading)
        d->m_curlRequest->resume();

    return true;
}

void ResourceHandle::cancel()
{
    // Apotheosis: severs any pending redirect completion (see
    // ResourceHandleInternal::m_cancelled) before touching curlDelegate/
    // curlRequest, so a willSendRequestAsync completion already queued
    // against this handle becomes a no-op even if client() still reads
    // as non-null when it runs.
    d->m_cancelled = true;

    // Apotheosis: ResourceLoader::cancel() (loader/ResourceLoader.cpp:738-741,
    // shared code) does `handle->cancel(); m_handle = nullptr;` *before*
    // releaseResources() -> finishNetworkLoad(), so finishNetworkLoad()'s
    // `handle->clearClient()` never runs on the cancel path and d->m_client keeps
    // pointing at the ResourceLoader that is about to be destroyed. Any later
    // async hop that revives this handle (redirect policy completion,
    // didReceiveResponse completion, a queued curl callback) then dereferences a
    // dangling client - crash 20260903-155446. Clear it here; every callback in
    // this file is already gated on client(). ResourceLoader::willSwitchToSubstituteResource()
    // also cancels without dropping m_handle, so its later finishNetworkLoad()
    // sees a null client - harmless (clearClient() is idempotent); only the
    // debug-only ASSERT(handle->client() == this) there would notice.
    d->m_client = nullptr;

    if (d->m_curlRequest)
        d->m_curlRequest->cancel();

    // Apotheosis: also drops our RefPtrs, not just the delegate's back-reference
    // (see releaseCurlResources) - CurlRequest::cancel() protects itself for the
    // worker-thread hop it posts.
    releaseCurlResources(*this, d.get());
}

void ResourceHandle::platformSetDefersLoading(bool defers)
{
    ASSERT(isMainThread());

    // Apotheosis: never (re)start a transfer that was cancelled - resume() on a
    // request that has not started yet hands it to the scheduler.
    if (d->m_cancelled || !d->m_curlRequest)
        return;

    // Apotheosis: deferring is a pause, not a cancel. cancel() set m_cancelled,
    // so didCompleteTransfer() took the cancelled branch and the load never
    // reported complete or fail - a main resource deferred after its response
    // arrived (ImageDocument commit path) stayed "loading" forever.
    // suspend()/resume() map onto curl_easy_pause() on the scheduler thread;
    // resume() also starts a request that was deferred before start().
    if (defers)
        d->m_curlRequest->suspend();
    else
        d->m_curlRequest->resume();
}

// --- curl -> client translation (mirrors NetworkDataTaskCurl) ---------------

void ResourceHandle::curlDidSendData(unsigned long long bytesSent, unsigned long long totalToSend)
{
    if (!client())
        return;
    client()->didSendData(this, bytesSent, totalToSend);
}

void ResourceHandle::curlDidReceiveResponse(CurlRequest& request, CurlResponse&& receivedResponse)
{
    WebCorePortBumpLoad(1);

    // Apotheosis: m_cancelled as well as client() - see cancel().
    if (d->m_cancelled || !client())
        return;

    d->m_curlResponse = ResourceResponse(receivedResponse);

    // Redirect: a 3xx status carrying a Location header. Follow it ourselves by
    // driving willSendRequestAsync() and rebuilding the CurlRequest, instead of
    // forwarding the (empty) redirect body up to the client. Mirrors
    // NetworkDataTaskCurl::willPerformHTTPRedirection().
    // TODO(auth): 401/407 handling. Omitted for v1 (treat as normal response).
    if (d->m_curlResponse.isRedirection() && !d->m_curlResponse.httpHeaderField(HTTPHeaderName::Location).isEmpty()) {
        willPerformHTTPRedirection();
        return;
    }

    // didReceiveResponse() forwards to client()->didReceiveResponseAsync with a
    // completion handler that must call completeDidReceiveResponse() to unblock
    // the body. This is the critical async hop.
    ResourceResponse responseCopy = d->m_curlResponse;
    didReceiveResponse(WTF::move(responseCopy), [this, protectedThis = Ref { *this }] {
        // Apotheosis: didReceiveResponseAsync() is asynchronous (the loader may
        // hop through a policy decision); the load can be cancelled in that
        // window, and protectedThis keeps this handle alive past the point where
        // ResourceLoader::cancel() destroyed our client. Don't resume a transfer
        // that was torn down.
        if (d->m_cancelled)
            return;
        if (d->m_curlRequest)
            d->m_curlRequest->completeDidReceiveResponse();
    });
}

void ResourceHandle::willPerformHTTPRedirection()
{
    static const unsigned maxRedirects = 20;

    if (!client())
        return;

    // m_redirectCount is pre-incremented and compared so the 21st hop fails.
    if (d->m_redirectCount++ > maxRedirects) {
        client()->didFail(this, ResourceError("WebKitInternal"_s, 0, d->m_curlResponse.url(), "Too many redirects"_s));
        // Apotheosis: terminal - see releaseCurlResources().
        releaseCurlResources(*this, d.get());
        return;
    }

    // Resolve Location relative to the response URL (handles absolute + relative).
    URL redirectedURL { d->m_curlResponse.url(), d->m_curlResponse.httpHeaderField(HTTPHeaderName::Location) };
    if (!redirectedURL.isValid() || redirectedURL.protocolIsFile()) {
        client()->didFail(this, ResourceError("WebKitInternal"_s, 0, d->m_curlResponse.url(), "Invalid redirect location"_s));
        // Apotheosis: terminal - see releaseCurlResources().
        releaseCurlResources(*this, d.get());
        return;
    }

    // Build the redirected request from the original first request (preserving
    // method/headers), then point it at the new URL. Carry over a fragment from
    // the previous URL if the Location didn't supply one.
    ResourceRequest newRequest = d->m_firstRequest;
    if (!redirectedURL.hasFragmentIdentifier() && newRequest.url().hasFragmentIdentifier())
        redirectedURL.setFragmentIdentifier(newRequest.url().fragmentIdentifier());
    newRequest.setURL(WTF::move(redirectedURL));

    bool isCrossOrigin = !protocolHostAndPortAreEqual(d->m_firstRequest.url(), newRequest.url());

    // 301/302/303 to a non-GET request become GET per fetch semantics; 307/308
    // preserve the method and body. (303 always switches to GET.)
    int statusCode = d->m_curlResponse.httpStatusCode();
    if (!equalLettersIgnoringASCIICase(newRequest.httpMethod(), "get"_s)) {
        if (statusCode == 301 || statusCode == 302 || statusCode == 303) {
            newRequest.setHTTPMethod("GET"_s);
            newRequest.setHTTPBody(nullptr);
            newRequest.clearHTTPContentType();
        }
    }

    // Do not leak credentials / Authorization / Origin across an origin boundary,
    // and never resend a Referer when downgrading https -> http.
    if (isCrossOrigin) {
        newRequest.clearHTTPAuthorization();
        newRequest.clearHTTPOrigin();
    }
    if (!newRequest.url().protocolIs("https"_s) && WTF::protocolIs(newRequest.httpReferrer(), "https"_s))
        newRequest.clearHTTPReferrer();

    d->m_lastHTTPMethod = newRequest.httpMethod();

    // Hand the redirect to the client. willSendRequestAsync() runs the loader's
    // redirect bookkeeping (server-redirect notification, cross-origin checks)
    // and calls our completion handler back with the request to actually issue
    // — which the client may modify, or null out to abort the load.
    ResourceResponse redirectResponse = d->m_curlResponse;
    client()->willSendRequestAsync(this, WTF::move(newRequest), WTF::move(redirectResponse),
        [this, protectedThis = Ref { *this }](ResourceRequest&& continueRequest) {
            // Apotheosis: this handle can be cancel()led while the redirect
            // policy check is pending (e.g. the driver tears the load down
            // on a navigation click). protectedThis keeps ResourceHandle
            // itself alive, but its client may already be destroyed without
            // clearClient() having run (see m_cancelled), leaving client()
            // a dangling pointer - checking m_cancelled first avoids
            // dereferencing it. This was crash 20260903-155446: SEH access
            // violation at null+0x14 in client()->didFail's vtable dispatch.
            if (d->m_cancelled || !client()) {
                // Apotheosis: cancel() already invalidated the delegate; still
                // drop our own references so the CurlRequest that carried the
                // 3xx dies with this hop instead of at session teardown.
                releaseCurlResources(*this, d.get());
                return;
            }

            // Client aborted the redirect (e.g. blocked, or load cancelled).
            if (continueRequest.isNull()) {
                // Apotheosis: terminal - see releaseCurlResources().
                releaseCurlResources(*this, d.get());
                return;
            }

            // Tear down the request that delivered the 3xx; its body is the
            // redirect page we are intentionally discarding.
            if (d->m_curlRequest) {
                d->m_curlRequest->cancel();
                d->m_curlRequest->invalidateClient();
                d->m_curlRequest = nullptr;
            }
            if (d->m_curlDelegate)
                d->m_curlDelegate->invalidate();

            // Fresh delegate + CurlRequest for the new URL, then resume.
            d->m_curlDelegate = CurlResourceHandleDelegate::create(*this);
            d->m_curlRequest = CurlRequest::create(continueRequest, *d->m_curlDelegate,
                CurlRequest::CaptureNetworkLoadMetrics::Basic);

            if (!d->m_user.isEmpty() || !d->m_password.isEmpty()) {
                d->m_curlRequest->setUserPass(d->m_user, d->m_password);
                d->m_curlRequest->setAuthenticationScheme(ProtectionSpace::AuthenticationScheme::HTTPBasic);
            }

            if (!d->m_defersLoading)
                d->m_curlRequest->resume();
        });
}

void ResourceHandle::curlDidReceiveData(Ref<SharedBuffer>&& buffer)
{
    if (!client())
        return;
    // ResourceHandleClient consumes FragmentedSharedBuffer; SharedBuffer is one.
    auto size = buffer->size();
    client()->didReceiveBuffer(this, WTF::move(buffer), size);
}

void ResourceHandle::curlDidComplete(NetworkLoadMetrics&& metrics)
{
    WebCorePortBumpLoad(2);
    if (auto* client = this->client())
        client->didFinishLoading(this, metrics);
    // Apotheosis: terminal - drop the delegate/request cycle even when the
    // client is already gone (see releaseCurlResources).
    releaseCurlResources(*this, d.get());
}

void ResourceHandle::curlDidFailWithError(ResourceError&& error)
{
    WebCorePortBumpLoad(3);
    if (auto* client = this->client())
        client->didFail(this, error);
    // Apotheosis: terminal - see curlDidComplete().
    releaseCurlResources(*this, d.get());
}

// --- remaining stubs kept loud until implemented ----------------------------

void ResourceHandle::platformLoadResourceSynchronously(NetworkingContext*, const ResourceRequest&, StoredCredentialsPolicy, SecurityOrigin*, ResourceError& error, ResourceResponse&, Vector<uint8_t>&)
{
    // TODO: drive a CurlRequest with a nested run loop / MessageQueue and block.
    // Not on the render critical path; fail loudly for now.
    error = ResourceError("WebKitInternal"_s, 0, URL { }, "sync load not implemented (WinUWP curl)"_s);
}

bool ResourceHandle::shouldUseCredentialStorage() { return false; }
void ResourceHandle::didReceiveAuthenticationChallenge(const AuthenticationChallenge&) { }
void ResourceHandle::receivedCredential(const AuthenticationChallenge&, const Credential&) { }
void ResourceHandle::receivedRequestToContinueWithoutCredential(const AuthenticationChallenge&) { }
void ResourceHandle::receivedCancellation(const AuthenticationChallenge&) { if (client()) client()->receivedCancellation(this, { }); }
void ResourceHandle::receivedRequestToPerformDefaultHandling(const AuthenticationChallenge&) { }
void ResourceHandle::receivedChallengeRejection(const AuthenticationChallenge&) { }

#elif USE(SOUP) || USE(CURL)
ResourceHandleInternal::~ResourceHandleInternal() = default;

ResourceHandle::~ResourceHandle()
{
    ASSERT_NOT_REACHED();
}

bool ResourceHandle::start()
{
    ASSERT_NOT_REACHED();
    return false;
}

void ResourceHandle::cancel()
{
    ASSERT_NOT_REACHED();
}

void ResourceHandle::platformSetDefersLoading(bool)
{
    ASSERT_NOT_REACHED();
}

void ResourceHandle::platformLoadResourceSynchronously(NetworkingContext*, const ResourceRequest&, StoredCredentialsPolicy, SecurityOrigin*, ResourceError&, ResourceResponse&, Vector<uint8_t>&)
{
    ASSERT_NOT_REACHED();
}

bool ResourceHandle::shouldUseCredentialStorage()
{
    ASSERT_NOT_REACHED();
    return false;
}

void ResourceHandle::didReceiveAuthenticationChallenge(const AuthenticationChallenge&)
{
    ASSERT_NOT_REACHED();
}

void ResourceHandle::receivedCredential(const AuthenticationChallenge&, const Credential&)
{
    ASSERT_NOT_REACHED();
}

void ResourceHandle::receivedRequestToContinueWithoutCredential(const AuthenticationChallenge&)
{
    ASSERT_NOT_REACHED();
}

void ResourceHandle::receivedCancellation(const AuthenticationChallenge&)
{
    ASSERT_NOT_REACHED();
}

void ResourceHandle::receivedRequestToPerformDefaultHandling(const AuthenticationChallenge&)
{
    ASSERT_NOT_REACHED();
}

void ResourceHandle::receivedChallengeRejection(const AuthenticationChallenge&)
{
    ASSERT_NOT_REACHED();
}
#endif

} // namespace WebCore
