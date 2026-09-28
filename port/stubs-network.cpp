// ============================================================================
// stubs-network.cpp — link-time stubs for the "network" class of undefined
// platform symbols (EdgeHTML Reborn — ARM32 thumbv7 Win10-Mobile App Container).
//
// STATUS (Phase 1b, 2026-06-15 — curl backend turned ON):
//   This build is now USE(CURL)=1 / USE(OPENSSL)=1. The curl backend TUs under
//   platform/network/curl ARE compiled, so they now own the symbols that this
//   file used to stub. ALL of the former stubs here would LNK2005-collide with:
//     * ResourceHandle::*            -> revived in ResourceHandle.cpp
//                                       (#if USE(CURL) && WK_WINUWP curl bridge)
//     * SynchronousLoaderClient::*   -> SynchronousLoaderClient.cpp curl tail
//     * NetworkStorageSession cookies-> NetworkStorageSessionCurl.cpp
//     * ResourceError platform bits  -> ResourceErrorCurl.cpp
//     * ResourceResponse::platformSuggestedFilename -> ResourceResponseCurl.cpp
//     * CertificateInfo summary/isolatedCopy        -> CertificateInfoCurl.cpp
//   They have ALL been removed from this file.
//
// WHAT REMAINS:
//   Only the two NetworkStateNotifier platform hooks. The agnostic
//   NetworkStateNotifier.cpp IS compiled and owns singleton()/onLine()/
//   addListener()/updateState(); only these two platform members are missing
//   (normally NetworkStateNotifier{Win,GLib,Mac}.cpp, none of which is built for
//   this App-Container port). The Win port's polling via
//   InternetGetConnectedState is the one native answer available here, so query
//   it (wininet is in the App partition and the manifest carries the
//   internetClient capability).
//
// PlatformScreen / DNSResolveQueue are owned by other stub files — untouched.
// ============================================================================

#include "config.h"

#include <windows.h>
#include <wininet.h>

#include "NetworkStateNotifier.h"

namespace WebCore {

void NetworkStateNotifier::updateStateWithoutNotifying()
{
    // Apotheosis (2026-09-27): was an unconditional `m_isOnLine = true`. On a
    // phone that drops Wi-Fi whenever the screen is off this made navigator.onLine
    // lie exactly when it mattered: a load that could never succeed would then be
    // pumped until the settle cap / watchdog instead of failing fast, and no page
    // script could tell offline from slow. InternetGetConnectedState answers
    // synchronously from the cached connectivity state - no request, no blocking.
    DWORD flags = 0;
    m_isOnLine = InternetGetConnectedState(&flags, 0) ? true : false;
}

void NetworkStateNotifier::startObserving()
{
    // No change notification exists in App Container (no
    // RegisterWaitForSingleObject / NotifyAddrChange); updateState() is re-run on
    // each query path, so the answer is fresh without an observer.
}

} // namespace WebCore
