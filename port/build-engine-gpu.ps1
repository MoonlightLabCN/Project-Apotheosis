# build-engine-gpu.ps1 — 用当前 ARM32 UWP 环境跑 ninja 构建引擎目标(默认 WebCore)。
# 可反复调用:ninja 天然断点续跑,中断后再执行即可接着编。
param(
    [string]$Target = 'WebCore',
    [string]$BuildDir = 'E:\Apotheosis\build-clang-gpu'
)
$ErrorActionPreference = 'Continue'
. "$PSScriptRoot\arm32-uwp-env.ps1" *> $null
$ninja = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
Write-Host "==> ninja -C $BuildDir $Target" -ForegroundColor Cyan
& $ninja -C $BuildDir $Target
exit $LASTEXITCODE
