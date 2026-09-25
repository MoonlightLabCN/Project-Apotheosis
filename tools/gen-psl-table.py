#!/usr/bin/env python3
"""
gen-psl-table.py — Mozilla Public Suffix List -> port/PublicSuffixData.h

Why a generated header instead of libpsl:
  libpsl does not build for UWP (vcpkg refuses: "libpsl is only supported on !uwp"),
  so PlatformWinUWP.cmake drops PublicSuffixStoreCurl.cpp and the port used to stub
  platformIsPublicSuffix() -> false / platformTopPrivatelyControlledDomain() -> {}.
  That is not a safe default: it lets a site set Domain=.co.uk.

Why this shape (matters on ARM32 / 32-bit address space):
  Everything lands in .rdata. One NUL-separated blob of rule text plus a sorted
  uint32 offset table -> binary search, ZERO heap allocation and zero init cost.
  A HashSet<String> of ~10k rules would cost several hundred KB of heap on a
  device that has already OOM'd in this project's history.

Rules are split into three sorted tables so lookup never has to parse a prefix:
  normal     "com", "co.uk"        stored verbatim
  wildcard   "*.ck"                stored as the part after "*."  -> "ck"
  exception  "!www.ck"             stored as the part after "!"   -> "www.ck"

IDN rules are stored in punycode (IDNA/ACE) form only. WTF::URL always exposes hosts
already encoded to ASCII, so the raw UTF-8 spelling would never be matched against --
and keeping the table pure ASCII also keeps the generated header free of any source
encoding question.

Usage:
    python tools/gen-psl-table.py [public_suffix_list.dat] [-o port/PublicSuffixData.h]
    (with no input path, downloads from https://publicsuffix.org/list/)
"""

import argparse
import os
import sys
import urllib.request

LIST_URL = "https://publicsuffix.org/list/public_suffix_list.dat"


def to_ascii(label_string):
    """Rule -> pure-ASCII form (punycode for IDN labels), or None if unencodable."""
    if all(ord(c) < 128 for c in label_string):
        return label_string
    try:
        parts = []
        for label in label_string.split("."):
            if all(ord(c) < 128 for c in label):
                parts.append(label)
            else:
                parts.append(label.encode("idna").decode("ascii"))
        return ".".join(parts)
    except Exception:
        return None


def parse(text):
    normal, wildcard, exception = set(), set(), set()
    version = "unknown"
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("// VERSION:"):
            version = line.split(":", 1)[1].strip()
        if not line or line.startswith("//"):
            continue
        rule = line.split()[0].lower()

        if rule.startswith("!"):
            target, bucket = rule[1:], exception
        elif rule.startswith("*."):
            target, bucket = rule[2:], wildcard
        elif "*" in rule:
            # The PSL format allows '*' in other positions; none are currently used,
            # and supporting them would change the lookup shape. Fail loudly instead
            # of silently dropping a rule that affects cookie-domain safety.
            raise SystemExit("unsupported wildcard placement in rule: %r" % rule)
        else:
            target, bucket = rule, normal

        ace = to_ascii(target)
        if ace is None:
            print("warning: skipping unencodable rule %r" % rule, file=sys.stderr)
            continue
        bucket.add(ace)
    return version, normal, wildcard, exception


OCTAL_DIGITS = "01234567"


def emit_table(out, name, rules):
    rules = sorted(rules)
    offsets, pos = [], 0
    for r in rules:
        if '"' in r or "\\" in r:
            raise SystemExit("rule needs escaping, generator does not handle it: %r" % r)
        offsets.append(pos)
        pos += len(r) + 1

    out.append("// %s: %d rules, %d bytes of text" % (name, len(rules), pos))
    out.append("static const char k%sBlob[] =" % name)
    # Adjacent string literals concatenate, so chunking is free. Two constraints:
    #  - keep each literal well under MSVC's 16380-byte per-literal limit;
    #  - a C octal escape swallows up to three digits, so "\0" must never be
    #    immediately followed by an octal digit inside the SAME literal, or
    #    "\0" "123..." silently becomes the single byte \012. Starting a new
    #    literal at that point is itself the separator.
    line, lines = "", []
    for r in rules:
        if line and (len(line) + len(r) + 2 > 120 or r[0] in OCTAL_DIGITS):
            lines.append(line)
            line = ""
        line += r + "\\0"
    if line:
        lines.append(line)
    for chunk in lines:
        out.append('    "%s"' % chunk)
    out.append("    ;")
    out.append("static const unsigned k%sCount = %d;" % (name, len(rules)))
    out.append("static const unsigned k%sOffsets[] = {" % name)
    for i in range(0, len(offsets), 16):
        out.append("    " + ",".join(str(o) for o in offsets[i:i + 16]) + ",")
    out.append("};")
    out.append("")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input", nargs="?", help="public_suffix_list.dat (downloaded if omitted)")
    ap.add_argument("-o", "--output", default=None)
    args = ap.parse_args()

    if args.input:
        with open(args.input, "r", encoding="utf-8") as f:
            text = f.read()
        source = args.input
    else:
        text = urllib.request.urlopen(LIST_URL, timeout=60).read().decode("utf-8")
        source = LIST_URL

    version, normal, wildcard, exception = parse(text)

    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    output = args.output or os.path.join(repo, "port", "PublicSuffixData.h")

    out = [
        "// PublicSuffixData.h — GENERATED, DO NOT EDIT.",
        "//",
        "// Source : %s" % source,
        "// PSL version: %s" % version,
        "// Regenerate : python tools/gen-psl-table.py",
        "//",
        "// Sorted, NUL-separated rule text in .rdata + a sorted offset table for binary",
        "// search. No heap, no initialization cost -- deliberate, this runs on a 32-bit",
        "// ARM device with a 2 GB user address space.",
        "",
        "#pragma once",
        "",
        "namespace WebCorePort {",
        "namespace PSLData {",
        "",
    ]
    emit_table(out, "Normal", normal)
    emit_table(out, "Wildcard", wildcard)
    emit_table(out, "Exception", exception)
    out += ["} // namespace PSLData", "} // namespace WebCorePort", ""]

    with open(output, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))

    print("wrote %s" % output)
    print("  PSL version : %s" % version)
    print("  normal=%d wildcard=%d exception=%d" % (len(normal), len(wildcard), len(exception)))
    print("  size        : %.1f KB" % (os.path.getsize(output) / 1024.0))


if __name__ == "__main__":
    sys.exit(main())
