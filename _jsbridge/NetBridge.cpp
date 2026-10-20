// Image transport: independent HTTPS DNS providers, memory CA bundle, no OS WebP requirement.
#include <windows.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "DnsWire.h"

static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_dnsLock;
struct CachedDns { std::string host; DnsWire::Answer answer; ULONGLONG expires; int provider; };
static std::vector<CachedDns> g_cache;
static int g_preferred = 0;
struct Provider { const char* name; const char* host; const char* ips; };
static Provider g_providers[] = {
    { "Ali", "dns.alidns.com", "223.5.5.5,223.6.6.6" },
    { "DNSPod", "doh.pub", "1.12.12.12,120.53.53.53" },
    { "Cloudflare", "cloudflare-dns.com", "1.1.1.1,1.0.0.1" }
};
static BOOL CALLBACK InitCurl(PINIT_ONCE, PVOID, PVOID*) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return FALSE;
    InitializeCriticalSection(&g_dnsLock); return TRUE;
}
static void Note(char* out, int cap, const char* format, ...) {
    size_t n = strlen(out); if (n >= size_t(cap - 1)) return;
    va_list args; va_start(args, format); vsnprintf(out + n, cap - n, format, args); va_end(args);
}
struct Buffer { unsigned char* data = nullptr; size_t size = 0; size_t limit = 32 * 1024 * 1024; };
static size_t WriteBytes(char* input, size_t size, size_t count, void* user) {
    Buffer* b = static_cast<Buffer*>(user);
    if (size && count > b->limit / size) return 0;
    size_t n = size * count; if (n > b->limit - b->size) return 0;
    if (!n) return 0;
    void* next = realloc(b->data, b->size + n); if (!next) return 0;
    b->data = static_cast<unsigned char*>(next); memcpy(b->data + b->size, input, n); b->size += n; return n;
}
static long Remaining(ULONGLONG deadline) {
    ULONGLONG now = GetTickCount64(); return deadline > now ? static_cast<long>(deadline - now) : 0;
}
static bool Configure(CURL* curl, const char* ca, long milliseconds, Buffer& b, char* errors) {
    if (milliseconds < 1) milliseconds = 1;
    curl_blob blob = { const_cast<char*>(ca), strlen(ca), CURL_BLOB_COPY };
    // OpenSSL file CA loading fails in W10M AppContainer. Match Apotheosis's blob path.
    if (curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob) != CURLE_OK) return false;
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, milliseconds < 6000 ? milliseconds : 6000);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, milliseconds);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errors);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBytes);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &b);
    return true;
}
static bool QueryDns(const std::string& host, int provider, const char* ca, ULONGLONG deadline,
    DnsWire::Answer& answer, char* log, int cap) {
    auto wire = DnsWire::Query(host); if (wire.empty() || Remaining(deadline) <= 0) return false;
    const auto& p = g_providers[provider]; CURL* curl = curl_easy_init(); if (!curl) return false;
    std::string url = std::string("https://") + p.host + "/dns-query";
    std::string pin = std::string(p.host) + ":443:" + p.ips;
    curl_slist* resolve = curl_slist_append(nullptr, pin.c_str());
    curl_slist* headers = curl_slist_append(nullptr, "Accept: application/dns-message");
    headers = curl_slist_append(headers, "Content-Type: application/dns-message");
    Buffer b; b.limit = 65535; char errors[CURL_ERROR_SIZE] = {};
    long ms = Remaining(deadline); if (ms > 5000) ms = 5000;
    bool configured = Configure(curl, ca, ms, b, errors);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve); // Bootstrap without local DNS.
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, wire.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(wire.size()));
    CURLcode code = configured ? curl_easy_perform(curl) : CURLE_SSL_CACERT_BADFILE;
    long status = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    bool ok = code == CURLE_OK && status == 200 && DnsWire::Parse(b.data, b.size, host, answer);
    Note(log, cap, "DNS[%s] %s curl=%d HTTP=%ld %s%s; ", p.name, host.c_str(), int(code), status,
        ok ? answer.addresses.c_str() : errors, ok ? "" : " (no valid A answer)");
    free(b.data); curl_easy_cleanup(curl); curl_slist_free_all(headers); curl_slist_free_all(resolve); return ok;
}
static bool CacheGet(const std::string& host, DnsWire::Answer& answer, int& provider) {
    EnterCriticalSection(&g_dnsLock); bool found = false;
    for (const auto& entry : g_cache) if (entry.host == host && entry.expires > GetTickCount64()) {
        answer = entry.answer; provider = entry.provider; found = true; break;
    }
    LeaveCriticalSection(&g_dnsLock); return found;
}
static void CachePut(const std::string& host, const DnsWire::Answer& answer, int provider) {
    EnterCriticalSection(&g_dnsLock);
    for (auto it = g_cache.begin(); it != g_cache.end();) {
        if (it->host == host || it->expires <= GetTickCount64()) it = g_cache.erase(it); else ++it;
    }
    if (answer.ttl) {
        if (g_cache.size() >= 32) g_cache.erase(g_cache.begin());
        g_cache.push_back({host, answer, GetTickCount64() + ULONGLONG(answer.ttl) * 1000, provider});
        g_preferred = provider;
    }
    LeaveCriticalSection(&g_dnsLock);
}
static bool ConnectionFailure(CURLcode code) {
    return code == CURLE_COULDNT_RESOLVE_HOST || code == CURLE_COULDNT_CONNECT ||
        code == CURLE_OPERATION_TIMEDOUT || code == CURLE_SSL_CONNECT_ERROR;
}
extern "C" __declspec(dllexport) int NetBridgeFetch(const char* url, const char* headerLines,
    const char* caPem, int timeout, void** output, int* length, char* log, int cap) {
    if (!output || !length || !log || cap < 1) return -1;
    *output = nullptr; *length = 0; log[0] = 0;
    if (!url || !caPem || !*caPem) { Note(log, cap, "missing URL or memory CA bundle"); return -1; }
    if (!InitOnceExecuteOnce(&g_once, InitCurl, nullptr, nullptr)) return -2;
    ULONGLONG deadline = GetTickCount64() + ULONGLONG(timeout > 0 && timeout <= 60 ? timeout : 30) * 1000;
    std::string current(url); curl_slist* headers = nullptr;
    char* copy = _strdup(headerLines ? headerLines : "");
    if (copy) {
        char* line = copy;
        while (*line) {
            char* end = strchr(line, '\n'); if (end) *end = 0;
            size_t n = strlen(line); if (n && line[n - 1] == '\r') line[n - 1] = 0;
            if (*line && _strnicmp(line, "Accept:", 7)) headers = curl_slist_append(headers, line);
            if (!end) break; line = end + 1;
        }
    }
    headers = curl_slist_append(headers, "Accept: image/jpeg,image/png,image/gif;q=0.9,*/*;q=0.1");
    int result = -4;
    for (int hop = 0; hop <= 8 && Remaining(deadline) > 0; ++hop) {
        CURLU* parsed = curl_url(); char* rawHost = nullptr; char* port = nullptr; char* scheme = nullptr;
        bool valid = parsed && curl_url_set(parsed, CURLUPART_URL, current.c_str(), 0) == CURLUE_OK &&
            curl_url_get(parsed, CURLUPART_HOST, &rawHost, 0) == CURLUE_OK &&
            curl_url_get(parsed, CURLUPART_PORT, &port, CURLU_DEFAULT_PORT) == CURLUE_OK &&
            curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK &&
            (!strcmp(scheme, "http") || !strcmp(scheme, "https"));
        std::string host = valid ? DnsWire::Lower(rawHost) : "", portString = valid ? port : "";
        curl_free(rawHost); curl_free(port); curl_free(scheme); if (parsed) curl_url_cleanup(parsed);
        if (!valid) { Note(log, cap, "invalid redirect URL; "); break; }
        DnsWire::Answer cached; int cachedProvider = -1;
        bool hasCache = CacheGet(host, cached, cachedProvider);
        int preferred; EnterCriticalSection(&g_dnsLock); preferred = g_preferred; LeaveCriticalSection(&g_dnsLock);
        bool redirected = false;
        for (int attempt = hasCache ? -1 : 0; attempt < 3 && Remaining(deadline) > 0; ++attempt) {
            int provider = attempt < 0 ? cachedProvider : (preferred + attempt) % 3;
            if (attempt >= 0 && hasCache && provider == cachedProvider) continue;
            DnsWire::Answer answer;
            if (attempt < 0) { answer = cached; Note(log, cap, "DNS[cache] %s %s; ", host.c_str(), answer.addresses.c_str()); }
            else if (!QueryDns(host, provider, caPem, deadline, answer, log, cap)) { result = CURLE_COULDNT_RESOLVE_HOST; continue; }
            CURL* curl = curl_easy_init(); if (!curl) { result = -3; break; }
            std::string pin = host + ":" + portString + ":" + answer.addresses;
            curl_slist* resolve = curl_slist_append(nullptr, pin.c_str());
            Buffer b; char errors[CURL_ERROR_SIZE] = {}; long ms = Remaining(deadline);
            if (ms > 15000) ms = 15000;
            bool configured = Configure(curl, caPem, ms, b, errors);
            curl_easy_setopt(curl, CURLOPT_URL, current.c_str());
            curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 Chrome/120.0 Safari/537.36");
            curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
            curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
            CURLcode code = configured ? curl_easy_perform(curl) : CURLE_SSL_CACERT_BADFILE;
            long status = 0; char* remote = nullptr; char* type = nullptr; char* next = nullptr;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            curl_easy_getinfo(curl, CURLINFO_PRIMARY_IP, &remote);
            curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &type);
            curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &next);
            Note(log, cap, "image curl=%d HTTP=%ld remote=%s type=%s bytes=%zu final=%s detail=%s; ", int(code), status,
                remote ? remote : "", type ? type : "", b.size, current.c_str(), errors[0] ? errors : curl_easy_strerror(code));
            bool success = code == CURLE_OK && status >= 200 && status < 300 && b.size > 0;
            bool redirect = code == CURLE_OK && next && (status == 301 || status == 302 || status == 303 || status == 307 || status == 308);
            if ((success || redirect) && attempt >= 0) CachePut(host, answer, provider);
            if (success) { *output = b.data; *length = static_cast<int>(b.size); b.data = nullptr; result = 0; }
            else if (redirect) { current = next; redirected = true; }
            else result = code != CURLE_OK ? int(code) : -4;
            free(b.data); curl_easy_cleanup(curl); curl_slist_free_all(resolve);
            if (success || redirect) break;
            if (!ConnectionFailure(code)) break;
            CachePut(host, DnsWire::Answer{"", 0}, provider);
        }
        if (!redirected) break;
        if (hop == 8) { Note(log, cap, "redirect limit; "); result = CURLE_TOO_MANY_REDIRECTS; }
    }
    if (Remaining(deadline) <= 0 && result) { Note(log, cap, "overall timeout; "); result = CURLE_OPERATION_TIMEDOUT; }
    free(copy); curl_slist_free_all(headers); return result;
}
extern "C" __declspec(dllexport) void NetBridgeFree(void* data) { free(data); }
