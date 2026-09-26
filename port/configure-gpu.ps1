# configure-gpu.ps1 — GPU 路径1 的 WebCore 构建配置(build-clang-gpu)。
# = configure-jit(保留 JIT:gpu-path1 分支 = JIT + GPU)+ 触发 GPU cmake 分支(-DAPOTHEOSIS_GPU=ON)。
# GPU 分支(OptionsWinUWP.cmake,见 [[gpu-acceleration]] 记忆 B 路配方):
#   USE_TEXTURE_MAPPER/USE_GRAPHICS_LAYER_TEXTURE_MAPPER/USE_ANGLE ON、USE_GRAPHICS_LAYER_WC OFF、
#   COORDINATED_GRAPHICS OFF;ANGLE::GLES/ANGLE::EGL 做成 IMPORTED 指向预编译 NuGet ANGLE(E:\Apotheosis\angle),
#   跳过 add_subdirectory(ThirdParty/ANGLE)(复用预编译,绕开为 thumbv7 编 in-tree ANGLE)。
# 前置:OptionsWinUWP.cmake 需加 APOTHEOSIS_GPU 分支;Source/CMakeLists.txt 的 ANGLE 子目录 add 需 AND NOT APOTHEOSIS_GPU。
# 真机探针(0.1.5.0)验证预编译 ANGLE 能在 App Container 跑通后再开本构建。
param(
    [ValidateSet('Release','Debug')] [string]$Config = 'Release',
    [string]$IcuRoot = 'C:\icu-arm-uwp'
)
$ErrorActionPreference = 'Continue'
. "$PSScriptRoot\arm32-uwp-env.ps1"

$Root   = Split-Path -Parent $PSScriptRoot
$cmake  = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja  = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$WebKit = Join-Path $Root 'WebKit'
$Build  = Join-Path $Root 'build-clang-gpu'
$Toolchain = Join-Path $PSScriptRoot 'Toolchain-ARM32-UWP-clang.cmake'

# 经垫片调用 pkg-config:FindPkgConfig 注入的反斜杠路径会被 msys2 pkg-config
# 按冒号切掉盘符,产出混合路径让 CMake 解析崩溃(见 pkg-config-shim.cmd 说明)。
$pkgconfig = Join-Path $PSScriptRoot 'pkg-config-shim.cmd'
# 故意指向空目录:msys2 的 pkg-config 在 PowerShell 环境里能找到 vcpkg 的 .pc,
# 但其前缀重定位会拼出 "\vcpkg\...\lib\pkgconfig/../../lib" 这种混合路径,CMake 的
# FindPkgConfig 解析时把 \v 当转义符直接 FATAL(2.54 的 FindHarfBuzz 走 pkg_check_modules
# 首查)。让 pkg-config 一律"找不到" -> 所有 find_* 走 CMAKE_PREFIX_PATH 兜底,
# 与本地 2.52.4 实际生效的发现路径一致。
$env:PKG_CONFIG_PATH = "C:/vcpkg/installed/arm-uwp/lib/pkgconfig-none"
$env:PATH = "C:\vcpkg\installed\x64-windows\tools\gperf;$env:PATH"

Write-Host "==> configure GPU build (build-clang-gpu, $Config, JIT+TextureMapper+ANGLE)" -ForegroundColor Cyan
& $cmake -S $WebKit -B $Build -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" `
    "-DCMAKE_PREFIX_PATH=$(($IcuRoot -replace '\\','/'));C:/vcpkg/installed/arm-uwp" `
    "-DPKG_CONFIG_EXECUTABLE=$pkgconfig" `
    "-DPORT=WinUWP" `
    "-DCMAKE_BUILD_TYPE=$Config" `
    "-DENABLE_STATIC_JSC=ON" `
    "-DENABLE_C_LOOP=OFF" `
    "-DENABLE_JIT=ON" `
        "-DAPOTHEOSIS_JIT=OFF" `
    "-DENABLE_DFG_JIT=OFF" `
    "-DENABLE_FTL_JIT=OFF" `
    "-DENABLE_SAMPLING_PROFILER=OFF" `
    "-DUSE_SYSTEM_MALLOC=ON" `
    "-DAPOTHEOSIS_GPU=ON"
Write-Host "==> configure 退出码 $LASTEXITCODE" -ForegroundColor $(if($LASTEXITCODE -eq 0){'Green'}else{'Red'})
