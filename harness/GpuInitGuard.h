#pragma once
// 老驱动(WDDM 1.1/1.2,Lumia 920/930)的 D3D/swapchain 初始化故障若可 SEH 捕获,
// 转成错误码让调用方落回软件渲染(GitHub issue #6)。MSVC 专属:ARM32 clang-cl 不支持 __try。
// 纯 C++ + SEH,单独编译(CompileAsWinRT=false,同 JitProbe)。
int GpuInitGuarded(void* nativeWindow, int w, int h);
