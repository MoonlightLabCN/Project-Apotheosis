// GpuInitGuard.cpp — 见 .h。SEH 壳包 WebCoreGpuInit:GPU 初始化在任何线程被调;
// 壳本身无线程状态,异常码原样透传成 -23。
#include <windows.h>

extern "C" int WebCoreGpuInit(void* nativeWindow, int w, int h);   // WebCoreDriver-gpu.lib

int GpuInitGuarded(void* nativeWindow, int w, int h)
{
    __try {
        return WebCoreGpuInit(nativeWindow, w, h);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -23;   // 驱动初始化崩溃:引擎侧 g_gpuActive 仍 false,调用方落软件路径
    }
}
