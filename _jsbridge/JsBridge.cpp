// JsBridge.cpp — NovReader 的 JavaScriptCore 桥。
// 静态链接 Apotheosis 的 JavaScriptCore / WTF；ICU 使用随包携带的动态库。
// 只导出 3 个 C 函数：JsBridgeInit / JsBridgeEvaluate / JsBridgeFree。
// 宿主函数（java.* 全部）经函数指针回调进 C#；结果写入调用方缓冲区（不够返回 -1）。
// 构建环境与 Apotheosis 一致：clang-cl ARM32 UWP、无异常（_HAS_EXCEPTIONS=0）。

#include "config.h"
#include "JsBridgeDiagnostics.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include <JavaScriptCore/JSBase.h>
#include <JavaScriptCore/JSContextRef.h>
#include <JavaScriptCore/JSObjectRef.h>
#include <JavaScriptCore/JSStringRef.h>
#include <JavaScriptCore/JSValueRef.h>

#include <wtf/MainThread.h>
#include <JavaScriptCore/InitializeThreading.h>
#include <JavaScriptCore/Options.h>
#include <JavaScriptCore/ExecutableAllocator.h>

// These private headers must use the very same generated configuration as the library.
#if !ENABLE(JIT) || ENABLE(C_LOOP)
#error JsBridge must match build-clang-gpu JIT ABI
#endif
extern void WTFWinUWPSetCrashHook(void (*hook)(const char* reason));

static wchar_t g_logPath[1024] = { 0 };

void JsBridgeTrace(const char* reason)
{
    if (!g_logPath[0]) return;
    HANDLE file = CreateFile2(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_ALWAYS, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME time;
    GetLocalTime(&time);
    char line[2048];
    int length = snprintf(line, sizeof(line), "%02u:%02u:%02u native tid=%lu %s\r\n",
        time.wHour, time.wMinute, time.wSecond, GetCurrentThreadId(), reason ? reason : "");
    if (length > 0)
    {
        DWORD written = 0;
        WriteFile(file, line, (DWORD)((size_t)length < sizeof(line) ? length : sizeof(line) - 1), &written, nullptr);
        FlushFileBuffers(file);
    }
    CloseHandle(file);
}

typedef int (*HostFn)(const char* in, char* out, int cap);

// ---------------------------------------------------------------- 全局状态

static JSGlobalContextRef g_ctx = nullptr;
static HostFn g_hosts[16];                    // 按 __bridgeN 顺序
static JSObjectRef g_fns[16];
static int g_hostCount = 0;

static char* g_out = nullptr;
static size_t g_outCap = 0;

static CRITICAL_SECTION g_lock;
static int g_lockInit = 0;

static void OutReserve(size_t need)
{
    if (g_out && g_outCap >= need) return;
    size_t cap = g_outCap ? g_outCap : 65536;
    while (cap < need) cap *= 2;
    char* next = (char*)realloc(g_out, cap);
    if (next) { g_out = next; g_outCap = cap; }
}

static const char* OutSet(const char* text)
{
    if (!text) text = "";
    size_t need = strlen(text) + 1;
    OutReserve(need);
    if (g_out && g_outCap >= need) { memcpy(g_out, text, need); return g_out; }
    return "";
}

// ---------------------------------------------------------------- UTF-16 → UTF-8

static int Utf16ToUtf8(const wchar_t* src, char* dst, size_t dstCap)
{
    if (!src) { if (dst && dstCap > 0) dst[0] = 0; return 0; }
    int need = WideCharToMultiByte(CP_UTF8, 0, src, -1, nullptr, 0, nullptr, nullptr);
    // 查询长度时不写目标缓冲区，返回长度不含 NUL。
    if (!dst) return need > 0 ? need - 1 : -1;
    if (need <= 0 || (size_t)need > dstCap) return -1;
    WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, need, nullptr, nullptr);
    return need - 1;
}

// ---------------------------------------------------------------- 宿主函数回调

static JSValueRef HostCallback(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject,
                               size_t argc, const JSValueRef argv[], JSValueRef* exception)
{
    (void)thisObject;

    HostFn fn = nullptr;
    for (int i = 0; i < g_hostCount; i++)
        if (function == g_fns[i]) { fn = g_hosts[i]; break; }
    if (!fn)
    {
        if (exception) *exception = JSValueMakeString(ctx, JSStringCreateWithUTF8CString("host fn missing"));
        return JSValueMakeUndefined(ctx);
    }

    // 参数按 UTF-8 拼接，条目间用 \x01 分隔（C# 侧按约定拆）
    size_t inCap = 256;
    for (size_t i = 0; i < argc; i++)
    {
        JSStringRef arg = JSValueToStringCopy(ctx, argv[i], nullptr);
        if (arg) { inCap += (size_t)JSStringGetLength(arg) * 4 + 2; JSStringRelease(arg); }
    }

    char* in = (char*)malloc(inCap);
    if (!in) return JSValueMakeUndefined(ctx);
    size_t used = 0;
    for (size_t i = 0; i < argc; i++)
    {
        if (i > 0) in[used++] = '\x01';
        JSStringRef arg = JSValueToStringCopy(ctx, argv[i], nullptr);
        if (arg)
        {
            size_t argCap = (size_t)JSStringGetLength(arg) * 4 + 2;
            if (used + argCap < inCap)
            {
                size_t written = (size_t)JSStringGetUTF8CString(arg, in + used, argCap);
                used += written > 0 ? written - 1 : 0; // 减掉补的 NUL
            }
            JSStringRelease(arg);
        }
    }
    in[used] = 0;

    size_t cap = 262144;
    for (int attempt = 0; attempt < 3; attempt++)
    {
        char* out = (char*)malloc(cap);
        if (!out) break;
        int n = fn(in, out, (int)cap);
        if (n >= 0)
        {
            out[n] = 0;
            JSStringRef result = JSStringCreateWithUTF8CString(out);
            free(out);
            free(in);
            JSValueRef value = JSValueMakeString(ctx, result);
            JSStringRelease(result);
            return value;
        }
        free(out);
        cap *= 4;
    }

    free(in);
    if (exception) *exception = JSValueMakeString(ctx, JSStringCreateWithUTF8CString("host call failed"));
    return JSValueMakeUndefined(ctx);
}

static void MakeHostFn(JSContextRef ctx, const char* name, HostFn fn, int index)
{
    JSStringRef nameRef = JSStringCreateWithUTF8CString(name);
    JSObjectRef f = JSObjectMakeFunctionWithCallback(ctx, nameRef, HostCallback);
    g_fns[index] = f;
    g_hosts[index] = fn;
    if (index + 1 > g_hostCount) g_hostCount = index + 1;

    JSObjectRef global = JSContextGetGlobalObject(ctx);
    JSObjectSetProperty(ctx, global, nameRef, f, kJSPropertyAttributeNone, nullptr);
    JSStringRelease(nameRef);
}

static void SetGlobalString(JSContextRef ctx, const char* name, const char* value)
{
    JSStringRef nameRef = JSStringCreateWithUTF8CString(name);
    JSStringRef valueRef = JSStringCreateWithUTF8CString(value);
    JSObjectRef global = JSContextGetGlobalObject(ctx);
    JSObjectSetProperty(ctx, global, nameRef, JSValueMakeString(ctx, valueRef), kJSPropertyAttributeNone, nullptr);
    JSStringRelease(nameRef);
    JSStringRelease(valueRef);
}

static char* DupUtf8FromValue(JSContextRef ctx, JSValueRef value)
{
    if (!value) return nullptr;
    JSValueRef err = nullptr;
    JSStringRef str = JSValueToStringCopy(ctx, value, &err);
    if (!str) return nullptr;
    size_t cap = (size_t)JSStringGetLength(str) * 4 + 2;
    char* utf8 = (char*)malloc(cap);
    if (utf8) JSStringGetUTF8CString(str, utf8, cap);
    JSStringRelease(str);
    return utf8;
}

// ---------------------------------------------------------------- 绑定

// 绑定串：名\x03值 → 字符串；名\x04JSON → JSON.parse；条目间 \x02
static void ApplyBindings(JSContextRef ctx, const wchar_t* bindings)
{
    if (!bindings) return;
    size_t need = (size_t)Utf16ToUtf8(bindings, nullptr, 0);
    if (need == (size_t)-1 || need == 0) return;
    char* utf8 = (char*)malloc(need + 1);
    if (!utf8) return;
    Utf16ToUtf8(bindings, utf8, need + 1);

    int jsonIndex = 0;
    char* p = utf8;
    while (*p)
    {
        char* entry = p;
        while (*p && *p != '\x02') p++;
        if (*p) { *p = 0; p++; }

        char sep = 0;
        for (char* q = entry; *q; q++)
        {
            if (*q == '\x03' || *q == '\x04') { sep = *q; *q = 0; break; }
        }
        if (!sep || !entry[0]) continue;
        char* value = entry + strlen(entry) + 1;

        if (sep == '\x03')
        {
            SetGlobalString(ctx, entry, value);
        }
        else
        {
            char varName[48];
            snprintf(varName, sizeof(varName), "__jn%d", jsonIndex++);
            SetGlobalString(ctx, varName, value);

            size_t codeCap = strlen(entry) + strlen(varName) + 40;
            char* code = (char*)malloc(codeCap);
            if (code)
            {
                snprintf(code, codeCap, "%s = JSON.parse(%s);", entry, varName);
                JSStringRef codeRef = JSStringCreateWithUTF8CString(code);
                JSEvaluateScript(ctx, codeRef, nullptr, nullptr, 0, nullptr);
                JSStringRelease(codeRef);
                free(code);
            }
        }
    }

    free(utf8);
}

// ---------------------------------------------------------------- 导出

extern "C" {

// C# 在求值前把结果变量名预先放好；异常对象也用全局槽传回
__declspec(dllexport) int JsBridgeInit(void** context, const wchar_t* diagnosticPath,
    HostFn ajax, HostFn post, HostFn httpGet, HostFn rule, HostFn cookie,
    HostFn getVar, HostFn put, HostFn base64e, HostFn base64d, HostFn base64url,
    HostFn md5, HostFn md5_16, HostFn sha1, HostFn sha256,
    HostFn aesDecode, HostFn aesEncode)
{
    if (context) *context = nullptr;
    if (diagnosticPath)
    {
        size_t length = wcslen(diagnosticPath);
        if (length < sizeof(g_logPath) / sizeof(g_logPath[0]))
            memcpy(g_logPath, diagnosticPath, (length + 1) * sizeof(wchar_t));
    }
    WTFWinUWPSetCrashHook(&JsBridgeTrace);
    JsBridgeTrace("init begin");
    if (!g_lockInit)
    {
        InitializeCriticalSection(&g_lock);
        g_lockInit = 1;
    }
    EnterCriticalSection(&g_lock);

    if (!g_ctx)
    {
        // Fail before constructing a VM if App Container denies executable memory.
        // The ARM LLInt also calls generated host thunks; a missing pool is not a safe fallback.
        void* probe = VirtualAllocFromApp(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        DWORD oldProtection = 0;
        bool executable = probe && VirtualProtectFromApp(probe, 4096, PAGE_EXECUTE_READ, &oldProtection);
        DWORD error = executable ? 0 : GetLastError();
        if (probe) VirtualFree(probe, 0, MEM_RELEASE);
        if (!executable)
        {
            char message[160];
            snprintf(message, sizeof(message), "executable memory denied, Win32=%lu; check codeGeneration capability", error);
            JsBridgeTrace(message);
            LeaveCriticalSection(&g_lock);
            return -3;
        }
        JsBridgeTrace("executable memory available; JSC initialize");
        // Match Apotheosis's current default mode 1: JIT thunks on, baseline tier-up off.
        // Apply before the first VM and before options are finalized.
        JSC::initialize([] {
            JSC::Options::useJIT() = true;
            JSC::Options::useBaselineJIT() = false;
            JSC::Options::forceRAMSize() = 536870912;
        });
        if (!JSC::ExecutableAllocator::singleton().isValid())
        {
            JsBridgeTrace("JSC executable pool unavailable; refusing VM creation");
            LeaveCriticalSection(&g_lock);
            return -4;
        }
        WTF::initializeMainThread();
        JsBridgeTrace("creating global context");
        g_ctx = JSGlobalContextCreate(nullptr);
        if (!g_ctx) { LeaveCriticalSection(&g_lock); return -1; }

        JsBridgeTrace("registering host callbacks");
        MakeHostFn(g_ctx, "__hAjax", ajax, 0);
        MakeHostFn(g_ctx, "__hPost", post, 1);
        MakeHostFn(g_ctx, "__hHttpGet", httpGet, 2);
        MakeHostFn(g_ctx, "__hRule", rule, 3);
        MakeHostFn(g_ctx, "__hCookie", cookie, 4);
        MakeHostFn(g_ctx, "__hGetVar", getVar, 5);
        MakeHostFn(g_ctx, "__hPut", put, 6);
        MakeHostFn(g_ctx, "__hBase64E", base64e, 7);
        MakeHostFn(g_ctx, "__hBase64D", base64d, 8);
        MakeHostFn(g_ctx, "__hBase64Url", base64url, 9);
        MakeHostFn(g_ctx, "__hMd5", md5, 10);
        MakeHostFn(g_ctx, "__hMd5_16", md5_16, 11);
        MakeHostFn(g_ctx, "__hSha1", sha1, 12);
        MakeHostFn(g_ctx, "__hSha256", sha256, 13);
        MakeHostFn(g_ctx, "__hAesDecode", aesDecode, 14);
        MakeHostFn(g_ctx, "__hAesEncode", aesEncode, 15);

        // java 对象与预置辅助（unpack / bhost），与 Jint 后端的 prelude 保持一致
        const char* prelude =
            "var java = {\n"
            "  ajax: function (u) { return __hAjax(u); },\n"
            "  ajaxAll: function (u) { var o = []; if (u) for (var i = 0; i < u.length; i++) o.push(__hAjax(u[i])); return o; },\n"
            "  post: function (u, b, h) { return __hPost(u, b, h); },\n"
            "  get: function (a, b) { return (b === undefined || b === null) ? __hGetVar(a) : __hHttpGet(a, b); },\n"
            "  put: function (k, v) { __hPut(k, v); },\n"
            "  getString: function (r) { return __hRule(r); },\n"
            "  getElement: function (r) { return __hRule(r); },\n"
            "  base64Encode: function (t) { return __hBase64E(t); },\n"
            "  base64Decode: function (t) { return __hBase64D(t); },\n"
            "  base64EncodeToBase64Url: function (t) { return __hBase64Url(t); },\n"
            "  md5Encode: function (t) { return __hMd5(t); },\n"
            "  md5: function (t) { return __hMd5(t); },\n"
            "  md5Encode16: function (t) { return __hMd5_16(t); },\n"
            "  sha1: function (t) { return __hSha1(t); },\n"
            "  sha256: function (t) { return __hSha256(t); },\n"
            "  aesBase64DecodeToString: function (d, k, t, i) { return __hAesDecode(d, k, t, i); },\n"
            "  aesBase64Encode: function (d, k, t, i) { return __hAesEncode(d, k, t, i); },\n"
            "  getCookie: function (u, n) { return (n === undefined || n === null) ? __hCookie(u, '') : __hCookie(u, n); },\n"
            "  androidId: function () { return ''; },\n"
            "  log: function (m) { },\n"
            "  sleep: function (m) { },\n"
            "  timeFormat: function (p) { return Date.now(); }\n"
            "};\n"
            "var unpack = function (p, a, c, k, e, d) {\n"
            "  if (a < 2 || a > 62) throw new Error('Unsupported packer radix ' + a);\n"
            "  var encode = function (n) { return (n < a ? '' : encode(Math.floor(n / a))) + ((n = n % a) > 35 ? String.fromCharCode(n + 29) : n.toString(36)); };\n"
            "  while (c-- > 0) { if (k[c]) { p = p.replace(new RegExp('\\\\b' + encode(c) + '\\\\b', 'g'), k[c]); } }\n"
            "  return p;\n"
            "};\n"
            "var bhost = function () { return (typeof source !== 'undefined' && source && source.bookSourceUrl) ? source.bookSourceUrl : ''; };\n";

        JsBridgeTrace("evaluating prelude");
        JSValueRef err = nullptr;
        JSStringRef preRef = JSStringCreateWithUTF8CString(prelude);
        JSEvaluateScript(g_ctx, preRef, nullptr, nullptr, 0, &err);
        JSStringRelease(preRef);
        if (err)
        {
            char* text = DupUtf8FromValue(g_ctx, err);
            OutSet(text ? text : "prelude failed");
            free(text);
            LeaveCriticalSection(&g_lock);
            return -2;
        }
    }

    if (context) *context = g_ctx;
    JsBridgeTrace("init complete");
    LeaveCriticalSection(&g_lock);
    return 0;
}

__declspec(dllexport) const char* JsBridgeEvaluate(void* context, const wchar_t* scriptUni, const wchar_t* bindingsUni, int* ok)
{
    JSGlobalContextRef ctx = (JSGlobalContextRef)context;
    if (!ctx || !ok) { if (ok) *ok = 1; return OutSet("invalid context"); }
    *ok = 0;

    EnterCriticalSection(&g_lock);
    JsBridgeTrace("evaluate: applying bindings");

    size_t need = 0;
    char* scriptUtf8 = nullptr;
    if (scriptUni)
    {
        need = (size_t)Utf16ToUtf8(scriptUni, nullptr, 0);
        if (need != (size_t)-1)
        {
            scriptUtf8 = (char*)malloc(need + 1);
            if (scriptUtf8) Utf16ToUtf8(scriptUni, scriptUtf8, need + 1);
        }
    }

    ApplyBindings(ctx, bindingsUni);

    JSValueRef exception = nullptr;
    JSStringRef scriptRef = JSStringCreateWithUTF8CString(scriptUtf8 ? scriptUtf8 : "");
    JsBridgeTrace("evaluate: executing script");
    JSValueRef completion = JSEvaluateScript(ctx, scriptRef, nullptr, nullptr, 0, &exception);

    if (!completion || exception)
    {
        const char* msg = "JS 求值失败";
        if (exception)
        {
            char* text = DupUtf8FromValue(ctx, exception);
            if (text)
            {
                JsBridgeTrace("evaluate error:");
                JsBridgeTrace(text);
                OutSet(text);
                free(text);
                msg = nullptr;
            }
        }
        if (msg) OutSet(msg);
        JSStringRelease(scriptRef);
        free(scriptUtf8);
        if (ok) *ok = 1;
        LeaveCriticalSection(&g_lock);
        return g_out ? g_out : "";
    }
    JSStringRelease(scriptRef);
    free(scriptUtf8);

    // 空结果回退到 result 变量
    bool empty = JSValueIsUndefined(ctx, completion) || JSValueIsNull(ctx, completion);
    char* completionUtf8 = nullptr;
    if (!empty)
    {
        completionUtf8 = DupUtf8FromValue(ctx, completion);
        empty = !completionUtf8 || completionUtf8[0] == 0;
    }
    if (empty)
    {
        free(completionUtf8);
        JSStringRef resultName = JSStringCreateWithUTF8CString("result");
        JSValueRef result = JSObjectGetProperty(ctx, JSContextGetGlobalObject(ctx), resultName, nullptr);
        JSStringRelease(resultName);
        completionUtf8 = nullptr;
        if (result && !JSValueIsUndefined(ctx, result) && !JSValueIsNull(ctx, result))
            completionUtf8 = DupUtf8FromValue(ctx, result);
        if (!completionUtf8) completionUtf8 = _strdup("");
    }

    OutSet(completionUtf8 ? completionUtf8 : "");
    JsBridgeTrace("evaluate complete");
    free(completionUtf8);
    LeaveCriticalSection(&g_lock);
    return g_out ? g_out : "";
}

__declspec(dllexport) void JsBridgeFree(void)
{
    if (g_ctx) { JSGlobalContextRelease(g_ctx); g_ctx = nullptr; }
}

} // extern "C"
