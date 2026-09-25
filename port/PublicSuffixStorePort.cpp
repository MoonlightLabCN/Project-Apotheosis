/*
 * PublicSuffixStorePort.cpp — real Public Suffix List for the WinUWP ARM32 port.
 *
 * Replaces the previous stub in stubs-other.cpp:
 *     platformIsPublicSuffix()             -> false
 *     platformTopPrivatelyControlledDomain -> { }
 * Those defaults are not merely incomplete, they are unsafe in the permissive
 * direction: with "nothing is a public suffix", CookieJarDB's domain check accepts
 * `Set-Cookie: ...; Domain=.co.uk` from any site under .co.uk, and every
 * registrable-domain comparison (SameSite / third-party classification) collapses.
 *
 * libpsl is unavailable here (vcpkg: "libpsl is only supported on !uwp"), so
 * PlatformWinUWP.cmake drops PublicSuffixStoreCurl.cpp and the port must supply
 * these two hooks itself.
 *
 * This file is only the WebCore adapter. The algorithm and rule tables live in
 * PublicSuffixLookup.h / PublicSuffixData.h so they can be unit-tested on the x64
 * build host (tests/psl/run-psl-tests.ps1) — the ARM32 device is the only place
 * the engine itself runs, so anything testable off-device should be.
 *
 * Memory shape (this matters — the target is a 32-bit ARM phone that has OOM'd
 * before): the rule tables are `static const` .rdata, demand-paged by the loader,
 * and lookup is binary search with zero allocation and zero init cost. A
 * HashSet<String> of ~10k rules would instead cost several hundred KB of heap
 * for the life of the process.
 */

#include "config.h"

#include "PublicSuffixStore.h"

#include <span>
#include <wtf/text/StringView.h>
#include <wtf/text/WTFString.h>

#include "PublicSuffixLookup.h"

namespace WebCore {

// Copy a StringView into an ASCII byte buffer. Returns 0 for anything that cannot
// be a host we classify (too long, or non-ASCII — WTF::URL hosts are punycode).
static unsigned copyASCIIHost(StringView domain, char* out, unsigned capacity)
{
    if (domain.length() >= capacity)
        return 0;
    for (unsigned i = 0; i < domain.length(); ++i) {
        char16_t c = domain[i];
        if (c >= 0x80)
            return 0;
        out[i] = static_cast<char>(c);
    }
    return domain.length();
}

bool PublicSuffixStore::platformIsPublicSuffix(StringView domain) const
{
    char buffer[WebCorePort::PSL::maxHostBytes];
    unsigned length = copyASCIIHost(domain, buffer, sizeof(buffer));
    if (!length)
        return false;
    return WebCorePort::PSL::isPublicSuffix(buffer, length);
}

String PublicSuffixStore::platformTopPrivatelyControlledDomain(StringView domain) const
{
    char buffer[WebCorePort::PSL::maxHostBytes];
    unsigned length = copyASCIIHost(domain, buffer, sizeof(buffer));
    if (!length)
        return String();

    WebCorePort::PSL::Host host;
    unsigned resultLength = 0;
    const char* result = WebCorePort::PSL::topPrivatelyControlledDomain(host, buffer, length, resultLength);
    if (!result || !resultLength)
        return String();
    return String::fromUTF8(std::span<const char>(result, resultLength));
}

} // namespace WebCore
