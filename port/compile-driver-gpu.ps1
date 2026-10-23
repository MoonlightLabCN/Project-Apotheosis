# compile-driver-gpu.ps1 — 同 compile-driver-jit.ps1,但头/库路径换成 build-clang-gpu
# (GPU 配置:USE_TEXTURE_MAPPER/USE_ANGLE ON + ENABLE_JIT ON;TextureMapper/GraphicsLayer 头才有)。
# 用法: pwsh -File port\compile-driver-gpu.ps1 <src.cpp> <out.obj>
param([Parameter(Mandatory)][string]$Src, [Parameter(Mandatory)][string]$Obj)
$ErrorActionPreference = 'Stop'
# Apotheosis (worktree support): 路径从 paths.ps1 解析。WebKit\ 源与 build-clang-gpu 是
# GB 级共享产物,从主 checkout 借用;驱动源码与脚本用当前 worktree。
. "$PSScriptRoot\paths.ps1"
. "$PSScriptRoot\arm32-uwp-env.ps1" *> $null

# 本脚本末尾要 Set-Location 到引擎构建目录,相对路径到那时就会失效 —— 先把两个入参绝对化。
# (原来调用方总传绝对路径所以没暴露;worktree 里第一次用相对路径就炸了。)
$Src = if ([System.IO.Path]::IsPathRooted($Src)) { $Src } else { Join-Path (Get-Location).Path $Src }
$Obj = if ([System.IO.Path]::IsPathRooted($Obj)) { $Obj } else { Join-Path (Get-Location).Path $Obj }

# harness-cmd.bat 来自引擎构建目录(共享),含引擎那一套 -I / -D / 宏
$baseCmd = Join-Path $EngineBuildDir 'harness-cmd.bat'
if (-not (Test-Path $baseCmd)) {
    # 有些配置把它留在 port\;两处都找不到就直接报清楚,别让 cmd 报一句莫名其妙的错
    $alt = Join-Path $PortDir 'harness-cmd.bat'
    if (Test-Path $alt) { $baseCmd = $alt }
    else { Write-Host "!! 找不到 harness-cmd.bat(找过 $EngineBuildDir 与 $PortDir)" -ForegroundColor Red; exit 1 }
}
$cmd = (Get-Content $baseCmd -Raw).Trim()
$cmd = $cmd -replace '/showIncludes',''
$cmd = $cmd -replace '/Fo\S+',''
$cmd = $cmd -replace '/Fd\S+',''
$cmd = $cmd -replace '-c\s+--\s+\S+UnifiedSource\S+',''
$cmd = $cmd -replace 'build-clang-webcore','build-clang-gpu'
# M2 GPU 呈现:WebCoreDriver.cpp 要包 texmap(经 platform/graphics 已在 -I 上)+ ANGLE 的 GLES2/EGL 头
# (angle\include 不在 harness-cmd 的 -I 里,补上)。WebKitLegacy 头与 bmalloc 头都在共享根下。
$cmd = "$cmd -DWK_WINUWP_JIT=1" `
     + " -I$WebKitRoot\Source\WebKitLegacy\WebCoreSupport" `
     + " -I$WebKitRoot\Source\WebKitLegacy\Storage" `
     + " -I$WebKitRoot\Source\WebKitLegacy" `
     + " -I$AngleDir\include" `
     + " -I$EngineBuildDir\bmalloc\Headers" `
     + " /Fo`"$Obj`" -c -- `"$Src`""

$bat = Join-Path $PortDir '_driver_compile_gpu.bat'
Set-Content -Path $bat -Value $cmd -Encoding ASCII
$log = Join-Path $PortDir 'driver-compile-gpu.log'
Set-Location $EngineBuildDir
cmd /c $bat 1> $log 2>&1
$exit = $LASTEXITCODE
$err = @(Select-String -Path $log -Pattern ': error:|fatal error:').Count
Write-Host "[compile-driver-gpu] EXIT=$exit  errors=$err  obj=$Obj  log=$log"
if ($exit -ne 0) { Get-Content $log -Tail 25 }
