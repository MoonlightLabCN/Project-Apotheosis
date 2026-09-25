// psl-test.cpp — host (x64) unit test for port/PublicSuffixLookup.h.
//
// The engine only runs on the ARM32 device, so the PSL algorithm is deliberately
// factored to be WebKit-free and testable here. Build + run:
//     pwsh -File tests/psl/run-psl-tests.ps1
//
// Cases marked [PSL] come from the canonical checkPublicSuffix() test vectors
// published with the Public Suffix List; the rest cover the port's own contract
// (leading dots from cookie domains, case folding, unknown TLDs).

#include "../../port/PublicSuffixLookup.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace WebCorePort::PSL;

static int g_failures = 0;
static int g_checks = 0;

static void expectIsPublicSuffix(const char* domain, bool expected)
{
    ++g_checks;
    bool actual = isPublicSuffix(domain, static_cast<unsigned>(std::strlen(domain)));
    if (actual != expected) {
        ++g_failures;
        std::printf("FAIL  isPublicSuffix(\"%s\") = %s, expected %s\n",
            domain, actual ? "true" : "false", expected ? "true" : "false");
    }
}

// expected == nullptr means "no registrable domain".
static void expectRegistrable(const char* domain, const char* expected)
{
    ++g_checks;
    Host host;
    unsigned length = 0;
    const char* result = topPrivatelyControlledDomain(host, domain, static_cast<unsigned>(std::strlen(domain)), length);
    std::string actual = result ? std::string(result, length) : std::string();
    std::string want = expected ? std::string(expected) : std::string();
    if (actual != want) {
        ++g_failures;
        std::printf("FAIL  topPrivatelyControlledDomain(\"%s\") = \"%s\", expected \"%s\"\n",
            domain, actual.c_str(), want.c_str());
    }
}

int main()
{
    // ---- isPublicSuffix -------------------------------------------------
    // The three cases named in the 0.1.9 acceptance criteria.
    expectIsPublicSuffix("com", true);
    expectIsPublicSuffix("co.uk", true);
    expectIsPublicSuffix("example.com", false);

    // ICANN + private-section entries, and multi-level suffixes.
    expectIsPublicSuffix("uk", true);
    expectIsPublicSuffix("jp", true);
    expectIsPublicSuffix("ac.jp", true);
    expectIsPublicSuffix("github.io", true);        // private section
    expectIsPublicSuffix("s3.amazonaws.com", true); // private section
    expectIsPublicSuffix("amazonaws.com", false);
    expectIsPublicSuffix("appspot.com", true);
    expectIsPublicSuffix("foo.github.io", false);
    expectIsPublicSuffix("www.example.co.uk", false);

    // The implicit "*" rule must NOT apply here (libpsl PSL_TYPE_NO_STAR_RULE),
    // so an unknown TLD is not a public suffix.
    expectIsPublicSuffix("nosuchtldanywhere", false);
    expectIsPublicSuffix("localhost", false);

    // Wildcard rules: PSL has "*.ck" plus the exception "!www.ck".
    expectIsPublicSuffix("foo.ck", true);
    expectIsPublicSuffix("www.ck", false);          // exception rule
    expectIsPublicSuffix("bar.foo.ck", false);

    // Wildcard rules under .jp: "*.kobe.jp" with "!city.kobe.jp".
    expectIsPublicSuffix("foo.kobe.jp", true);
    expectIsPublicSuffix("city.kobe.jp", false);

    // Normalization the port must handle: cookie-style leading dot, trailing root
    // dot, and uppercase.
    expectIsPublicSuffix(".com", true);
    expectIsPublicSuffix("CO.UK", true);
    expectIsPublicSuffix("com.", true);
    expectIsPublicSuffix("", false);
    expectIsPublicSuffix(".", false);

    // ---- topPrivatelyControlledDomain (eTLD+1) --------------------------
    expectRegistrable("a.example.com", "example.com");
    expectRegistrable("b.example.com", "example.com");
    expectRegistrable("example.com", "example.com");
    expectRegistrable("deep.sub.example.com", "example.com");
    expectRegistrable("com", nullptr);              // a public suffix has none
    expectRegistrable("co.uk", nullptr);
    expectRegistrable("example.co.uk", "example.co.uk");
    expectRegistrable("www.example.co.uk", "example.co.uk");

    // github.io is itself a public suffix, so each user site is registrable.
    expectRegistrable("github.io", nullptr);
    expectRegistrable("foo.github.io", "foo.github.io");
    expectRegistrable("pages.foo.github.io", "foo.github.io");

    // Cookie-style leading dot and case folding must not change the answer.
    expectRegistrable(".a.example.com", "example.com");
    expectRegistrable("A.EXAMPLE.COM", "example.com");

    // Unknown TLD: implicit "*" rule DOES apply here, matching psl_registrable_domain.
    expectRegistrable("foo.nosuchtldanywhere", "foo.nosuchtldanywhere");
    expectRegistrable("nosuchtldanywhere", nullptr);

    // [PSL] canonical vectors from https://publicsuffix.org/list/ checkPublicSuffix.
    expectRegistrable("example", nullptr);
    expectRegistrable("example.example", "example.example");
    expectRegistrable("b.example.example", "example.example");
    expectRegistrable("biz", nullptr);
    expectRegistrable("domain.biz", "domain.biz");
    expectRegistrable("b.domain.biz", "domain.biz");
    expectRegistrable("uk.com", nullptr);
    expectRegistrable("example.uk.com", "example.uk.com");
    expectRegistrable("b.example.uk.com", "example.uk.com");
    expectRegistrable("test.ac", "test.ac");
    // TLD whose only rule is a wildcard ("*.mm").
    expectRegistrable("mm", nullptr);
    expectRegistrable("c.mm", nullptr);
    expectRegistrable("b.c.mm", "b.c.mm");
    expectRegistrable("jp", nullptr);
    expectRegistrable("test.jp", "test.jp");
    expectRegistrable("www.test.jp", "test.jp");
    expectRegistrable("ac.jp", nullptr);
    expectRegistrable("test.ac.jp", "test.ac.jp");
    expectRegistrable("www.test.ac.jp", "test.ac.jp");
    expectRegistrable("kyoto.jp", nullptr);
    expectRegistrable("test.kyoto.jp", "test.kyoto.jp");
    expectRegistrable("ide.kyoto.jp", nullptr);
    expectRegistrable("b.ide.kyoto.jp", "b.ide.kyoto.jp");
    expectRegistrable("a.b.ide.kyoto.jp", "b.ide.kyoto.jp");
    // "*.kobe.jp" with the "!city.kobe.jp" exception.
    expectRegistrable("c.kobe.jp", nullptr);
    expectRegistrable("b.c.kobe.jp", "b.c.kobe.jp");
    expectRegistrable("city.kobe.jp", "city.kobe.jp");
    expectRegistrable("www.city.kobe.jp", "city.kobe.jp");
    // TLD with a wildcard rule and an exception ("*.ck" / "!www.ck").
    expectRegistrable("ck", nullptr);
    expectRegistrable("test.ck", nullptr);
    expectRegistrable("b.test.ck", "b.test.ck");
    expectRegistrable("www.ck", "www.ck");
    expectRegistrable("www.www.ck", "www.ck");

    // Cookie-safety property: a site must never be able to scope a cookie to a
    // public suffix. These are the inputs CookieJarDB rejects via this API.
    expectIsPublicSuffix("co.jp", true);
    expectIsPublicSuffix("com.au", true);
    expectIsPublicSuffix("org.uk", true);
    expectIsPublicSuffix("net", true);

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if (!g_failures)
        std::printf("PSL: PASS\n");
    else
        std::printf("PSL: FAIL\n");
    return g_failures ? 1 : 0;
}
