/*
 * PublicSuffixLookup.h — the Public Suffix List algorithm, free of WebKit types.
 *
 * Deliberately dependency-free (plain char buffers, no WTF, no allocation) for two
 * reasons:
 *   1. it can be compiled and unit-tested on the x64 build host — the ARM32 device
 *      is the only place the engine runs, so anything testable off-device should be;
 *   2. it keeps the hot path allocation-free on a 32-bit phone.
 *
 * WebCore's PublicSuffixStore hooks live in PublicSuffixStorePort.cpp, which is a
 * thin adapter over this header. Rule data comes from PublicSuffixData.h, generated
 * from the canonical Mozilla list by tools/gen-psl-table.py.
 *
 * Algorithm: https://publicsuffix.org/list/ ("Algorithm").
 */

#pragma once

#include "PublicSuffixData.h"

namespace WebCorePort {
namespace PSL {

// DNS caps a name at 253 bytes and 127 labels. Anything past these limits is not a
// host we need to classify; refusing it can only make us more conservative.
static constexpr unsigned maxHostBytes = 256;
static constexpr unsigned maxLabels = 40;

struct Host {
    char data[maxHostBytes];
    unsigned length { 0 };
    unsigned starts[maxLabels];   // byte offset of each label within `data`
    unsigned labelCount { 0 };
    bool valid { false };

    const char* suffixAt(unsigned i) const { return data + starts[i]; }
    unsigned suffixLengthAt(unsigned i) const { return length - starts[i]; }
};

// Normalize `domain` (length `n`) into `out`: strip leading dots (cookie domains
// arrive as ".example.com") and one trailing root dot, lowercase ASCII, split into
// labels. Rejects non-ASCII: WTF::URL always hands us punycode hosts, and the rule
// tables are punycode-only, so a non-ASCII input could never match anything anyway.
inline void parseHost(Host& out, const char* domain, unsigned n)
{
    out.valid = false;
    out.length = 0;
    out.labelCount = 0;
    if (!domain)
        return;

    unsigned begin = 0;
    while (begin < n && domain[begin] == '.')
        ++begin;
    unsigned end = n;
    while (end > begin && domain[end - 1] == '.')
        --end;
    if (end <= begin)
        return;

    unsigned len = end - begin;
    if (len >= maxHostBytes)
        return;

    for (unsigned i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(domain[begin + i]);
        if (c >= 0x80)
            return;
        if (c >= 'A' && c <= 'Z')
            c = static_cast<unsigned char>(c - 'A' + 'a');
        out.data[i] = static_cast<char>(c);
    }
    out.data[len] = '\0';
    out.length = len;

    out.starts[out.labelCount++] = 0;
    for (unsigned i = 0; i < len && out.labelCount < maxLabels; ++i) {
        if (out.data[i] == '.')
            out.starts[out.labelCount++] = i + 1;
    }
    out.valid = true;
}

// strcmp-like comparison of a (ptr, len) candidate against a NUL-terminated rule,
// using the same unsigned-byte ordering the generator sorted the tables by.
inline int compareToRule(const char* candidate, unsigned candidateLength, const char* rule)
{
    for (unsigned i = 0; i < candidateLength; ++i) {
        unsigned char a = static_cast<unsigned char>(candidate[i]);
        unsigned char b = static_cast<unsigned char>(rule[i]);
        if (!b)
            return 1;             // rule ended first -> candidate sorts after it
        if (a != b)
            return a < b ? -1 : 1;
    }
    return rule[candidateLength] ? -1 : 0;
}

inline bool tableContains(const char* blob, const unsigned* offsets, unsigned count,
    const char* candidate, unsigned candidateLength)
{
    unsigned lo = 0, hi = count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        int cmp = compareToRule(candidate, candidateLength, blob + offsets[mid]);
        if (!cmp)
            return true;
        if (cmp < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return false;
}

inline bool isNormalRule(const char* s, unsigned n)
{
    return tableContains(PSLData::kNormalBlob, PSLData::kNormalOffsets, PSLData::kNormalCount, s, n);
}

inline bool isWildcardRest(const char* s, unsigned n)
{
    return tableContains(PSLData::kWildcardBlob, PSLData::kWildcardOffsets, PSLData::kWildcardCount, s, n);
}

inline bool isExceptionRule(const char* s, unsigned n)
{
    return tableContains(PSLData::kExceptionBlob, PSLData::kExceptionOffsets, PSLData::kExceptionCount, s, n);
}

// How many trailing labels of `host` form its public suffix, counting EXPLICIT rules
// only. 0 means no rule matched; the caller decides whether the implicit "*" rule
// applies (it does for registrable-domain queries, it does not for isPublicSuffix,
// mirroring libpsl's PSL_TYPE_NO_STAR_RULE in PublicSuffixStoreCurl.cpp).
inline unsigned publicSuffixLabelCount(const Host& host)
{
    const unsigned n = host.labelCount;

    // Exception rules ("!www.ck") win outright: the public suffix becomes the
    // matched rule minus its leftmost label.
    for (unsigned i = 0; i < n; ++i) {
        if (isExceptionRule(host.suffixAt(i), host.suffixLengthAt(i)))
            return n - i - 1;
    }

    // Otherwise the prevailing (longest) matching rule. i == 0 is the longest
    // candidate, so the first hit while scanning left-to-right already wins.
    for (unsigned i = 0; i < n; ++i) {
        if (isNormalRule(host.suffixAt(i), host.suffixLengthAt(i)))
            return n - i;
        // Wildcard "*.X" matches labels[i..] iff labels[i+1..] == X, and needs a
        // label to consume, so i must not be the last label.
        if (i + 1 < n && isWildcardRest(host.suffixAt(i + 1), host.suffixLengthAt(i + 1)))
            return n - i;
    }
    return 0;
}

inline bool isPublicSuffix(const char* domain, unsigned n)
{
    Host host;
    parseHost(host, domain, n);
    if (!host.valid)
        return false;
    unsigned matched = publicSuffixLabelCount(host);
    return matched && matched == host.labelCount;
}

// Registrable domain (eTLD+1). Writes into `host` so the caller can build a string
// from the returned (pointer, length); returns nullptr when there is none, i.e. the
// input IS a public suffix or is shorter than one.
inline const char* topPrivatelyControlledDomain(Host& host, const char* domain, unsigned n, unsigned& outLength)
{
    outLength = 0;
    parseHost(host, domain, n);
    if (!host.valid)
        return nullptr;

    unsigned suffixLabels = publicSuffixLabelCount(host);
    // Implicit "*" rule: an unknown TLD still yields a registrable domain, which is
    // what psl_registrable_domain() does.
    if (!suffixLabels)
        suffixLabels = 1;
    if (host.labelCount <= suffixLabels)
        return nullptr;

    unsigned firstLabel = host.labelCount - suffixLabels - 1;
    outLength = host.suffixLengthAt(firstLabel);
    return host.suffixAt(firstLabel);
}

} // namespace PSL
} // namespace WebCorePort
