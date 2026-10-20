// JsBridgeStubs.cpp — 补齐 JSC/WTF 引用但没链进来的符号（Apotheosis 的 stubs 思路）。
// WTFCrashWithInfoImpl：先保存断言位置/载荷，再 fastfail。
// Gigacage::…：USE_SYSTEM_MALLOC 下 Gigacage 关闭，但符号仍被 WTF 引用 → 退到普通堆。
#include "config.h"
#include "JsBridgeDiagnostics.h"
#include <stddef.h>
#include <stdlib.h>
#include <windows.h>

#include <bmalloc/Gigacage.h>
#include <wtf/Assertions.h>

template<typename... Values>
[[noreturn]] static void BridgeCrash(int line, const char* file, const char* function, Values... values)
{
    const uint64_t payload[] = { static_cast<uint64_t>(values)... };
    WTFWinUWPReportCrashWithInfo(line, file, function, 0, sizeof...(Values), payload);
    __fastfail(FAST_FAIL_FATAL_APP_EXIT);
}

// 与 Apotheosis 驱动相同的报告出口，不能丢掉引擎提供的断言上下文。
void WTFCrashWithInfoImpl(int line, const char* file, const char* function, unsigned int a)
{
    BridgeCrash(line, file, function, a);
}
void WTFCrashWithInfoImpl(int line, const char* file, const char* function, unsigned int a, unsigned int b)
{
    BridgeCrash(line, file, function, a, b);
}
void WTFCrashWithInfoImpl(int line, const char* file, const char* function, unsigned int a, unsigned int b, unsigned int c)
{
    BridgeCrash(line, file, function, a, b, c);
}
void WTFCrashWithInfoImpl(int line, const char* file, const char* function, unsigned int a, unsigned int b, unsigned int c, unsigned int d)
{
    BridgeCrash(line, file, function, a, b, c, d);
}
void WTFCrashWithInfoImpl(int line, const char* file, const char* function, unsigned int a, unsigned int b, unsigned int c, unsigned int d, unsigned int e)
{
    BridgeCrash(line, file, function, a, b, c, d, e);
}
void WTFCrashWithInfoImpl(int line, const char* file, const char* function, unsigned int a, unsigned int b, unsigned int c, unsigned int d, unsigned int e, unsigned int f)
{
    BridgeCrash(line, file, function, a, b, c, d, e, f);
}
void WTFCrashWithInfoImpl(int line, const char* file, const char* function, unsigned int a, unsigned int b, unsigned int c, unsigned int d, unsigned int e, unsigned int f, unsigned int g)
{
    BridgeCrash(line, file, function, a, b, c, d, e, f, g);
}

// Gigacage 在系统 malloc 配置下不可用，这些只应被“已判可用”的路径调用；
// 保险起见都退化到普通堆（不保证对齐/连续性，但保持进程可用）。
namespace Gigacage {

void* tryMalloc(Kind, size_t size) { return ::malloc(size); }
void* tryZeroedMalloc(Kind, size_t size) { void* p = ::malloc(size); if (p) memset(p, 0, size); return p; }
void free(Kind, void* p) { ::free(p); }
void* tryAlignedMalloc(Kind, size_t alignment, size_t size) { return _aligned_malloc(size, alignment); }
void* tryRealloc(Kind, void* p, size_t size) { return ::realloc(p, size); }
void* tryAllocateZeroedVirtualPages(Kind, size_t size) { void* p = _aligned_malloc(size, 4096); if (p) memset(p, 0, size); return p; }
void freeVirtualPages(Kind, void* p, size_t) { _aligned_free(p); }

} // namespace Gigacage
