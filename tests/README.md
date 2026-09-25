# Apotheosis tests

Two kinds, split by where they can actually run.

## `psl/` — host unit tests (x64, seconds)

```bash
pwsh -File tests/psl/run-psl-tests.ps1
```

Builds and runs `psl-test.cpp` against `port/PublicSuffixLookup.h` with the host
compiler. The PSL algorithm is deliberately kept WebKit-free so its logic can be
verified here rather than through a 20-minute appx round trip to a Lumia. Includes
the canonical `checkPublicSuffix()` vectors published with the Public Suffix List.

Regenerate the rule tables when the upstream list moves:

```bash
python tools/gen-psl-table.py tools/public_suffix_list.dat
```

(omit the path to download the current list from publicsuffix.org)

## `web-platform/` — device browser tests (real device, manual)

```bash
python tests/web-platform/server.py
```

Then open `http://<build-host-lan-ip>:8099/` **on the Lumia**. Every check runs
in-page and prints PASS/FAIL in large type, because the device browser has no
console and no devtools — a result that is not on screen does not exist.

A second listener (default port 8100) is the other origin for SOP / SameSite
checks. SameSite across *sites* needs two hostnames (`?alt=host:port`); two
localhost ports are the same site, and those checks SKIP.

### 0.1.9 — network correctness

DOM/HTTP cookie unification, HttpOnly, public-suffix rejection of `Domain=.com`,
`Set-Cookie` on a 302, redirect chains, slow and truncated responses, CSP (header
policy and `<meta>` policy: nonce, sha256, `'self'`, `img-src`, `connect-src`),
`<input type=file>` multipart upload, fragment navigation vs. reload,
`pushState`/`replaceState`/back-forward, and JS dialogs.

### 0.2.0 — Web Platform baseline

Open `/pages/platform.html` for presence probes, then the per-subsystem suites:

| Page | What it checks |
| --- | --- |
| `/pages/iframe/` | subframe create/load/postMessage/SOP/sandbox/CSP/remove |
| `/pages/cookies-samesite.html` | Strict / Lax / None+Secure / unspecified |
| `/pages/storage.html` | localStorage / sessionStorage |
| `/pages/indexeddb.html` | object store CRUD, 1 MB value |
| `/pages/websocket.html` | WS echo, binary, close, handshake Cookie |
| `/pages/new-window.html` | `window.open` / `target=_blank` (shell tab) |
| `/pages/crypto.html` | SHA / HMAC / PBKDF2 / AES-GCM test vectors |

The suite is validated against a known-correct browser (all automatic checks pass
on desktop Chromium), so a FAIL on device means the port is wrong, not the test.

Notes:

- Reach the server by LAN IP, not `localhost` — the device is a different machine.
- Reaching it by bare IP makes the `Domain=` sharing check SKIP: an IP address has
  no public suffix, so there is nothing meaningful to assert.
- Cookie / localStorage / IndexedDB persistence needs the browser fully closed and
  reopened; the snapshot is written on suspend, so backgrounding alone is not the
  test.
- `wss://` and `SameSite=None; Secure` over HTTPS are NOT RUN until a local CA is
  installed on the device.
