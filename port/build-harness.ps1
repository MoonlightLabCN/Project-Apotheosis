# Builds the ARM32 UWP harness (appx).
#
# XAML: 默认 'Fallback' —— 系统更新打挂了 C++/CX XamlCompiler(生成 XamlTypeInfo 时空引用
# WMC9999,所有装着的 SDK + VS2017/VS18 两套 MSBuild 全崩,连空白页都崩),整个 markup
# compiler 已被绕开:gen-xaml-codebehind.ps1 生成运行期 XamlReader::Load 的 code-behind。
# 'Official' 保留只为将来 XamlCompiler 修好后能回去,当前环境下必然失败,别当默认。
# 详见 CLAUDE.md『手搓 XAML 工具链』。
#
# 工具链: 不接受"PATH 里谁新用谁"。resolve-arm32-toolchain.ps1 主动验证 ARM32 能力并把选中的
# 版本钉进 /p:VCToolsVersion,防止 VS 更新塞进来的 14.51(永久删了 32 位 ARM)被静默选中。
param(
    [string]$SdkVersion = '10.0.22621.0',
    [string]$XamlSdkVersion = '10.0.17763.0',
    # 空 = 由 resolve-arm32-toolchain.ps1 自己挑(优先 14.44.35207);显式传值则钉死该版本。
    [string]$VCToolsVersion = '',
    [string]$VcpkgRoot = 'C:\vcpkg',
    [string]$IcuRoot = 'C:\icu-arm-uwp',
    [ValidateSet('Official', 'Fallback')]
    [string]$XamlMode = 'Fallback',
    # Apotheosis (M4): package an A/B engine tree + its driver without touching the baseline
    # (defaults: <root>\build-clang-gpu and <root>\port, see Harness.vcxproj).
    [string]$EngineBuildDir = '',
    [string]$DriverDir = '',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$proj = Join-Path $root 'harness\Harness.vcxproj'
$log = Join-Path $root 'harness-build.log'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

# Apotheosis (worktree support): in a git worktree $root has no build-clang-gpu (that tree is GB
# scale and deliberately shared), so point the engine at the MAIN checkout's build while still
# consuming harness/ and port/ from this worktree. Explicit -EngineBuildDir/-DriverDir still win.
# DriverDir stays local: link-driver-gpu.ps1 just produced WebCoreDriver-gpu.lib *here*, and that
# lib is what this worktree's driver source was built into.
# NOTE: paths.ps1 defines $EngineBuildDir itself, so snapshot the parameters first — dot-sourcing
# would otherwise clobber an explicit override.
$engineArg = $EngineBuildDir
$driverArg = $DriverDir
. (Join-Path $PSScriptRoot 'paths.ps1')
if ($engineArg) { $EngineBuildDir = $engineArg }
if ($driverArg) { $DriverDir = $driverArg }
if (-not (Test-Path (Join-Path $EngineBuildDir 'lib\WebCore.lib'))) {
    Write-Host "[build-harness] 警告:引擎库不在 $EngineBuildDir\lib —— 先跑 ninja 或在主 checkout 构建(见 docs/UX-RESEARCH-2026-10-23.md)" -ForegroundColor Yellow
}
# ANGLE 头/库/DLL 也从共享根取(worktree 里没有 angle\arm)
$sharedArg = "/p:ApotheosisSharedRoot=$SharedRoot\"
# 打包用的 ANGLE DLL 必须先落到**本树内**:MSBuild 18.9 的 zone check(MSB3821)拒绝处理项目
# 树外的 <None Include>,而主 checkout 里共享根 == 项目树所以同一批 DLL 能过。两个 DLL 只
# 1.4MB,拷一份比绕开检查老实。头/导入库不参与打包,继续共享。
$packAngleDir = Join-Path $HarnessDir 'angle-arm'
$angleSrcDir  = Join-Path $SharedRoot 'angle\arm'
$packArg = "/p:ApotheosisPackAngleDir=$packAngleDir\"
if ($SharedRoot -ne $RepoRoot) {
    New-Item -ItemType Directory -Path $packAngleDir -Force | Out-Null
    foreach ($dll in 'libEGL.dll', 'libGLESv2.dll') {
        $src = Join-Path $angleSrcDir $dll
        if (Test-Path $src) { Copy-Item $src (Join-Path $packAngleDir $dll) -Force }
        else { Write-Host "[build-harness] 警告:共享根缺 $dll ($src)" -ForegroundColor Yellow }
    }
    Write-Host "[build-harness] worktree:ANGLE 打包 DLL 已就位 -> $packAngleDir"
}
# 字体同样 gitignored(约 20MB,port\fetch-fonts.ps1 可重下),worktree 里自然没有。
# 它们必须随 appx 打包(App Container 拿不到设备系统字体),所以从主 checkout 拷进来。
$fontDir     = Join-Path $HarnessDir 'Assets\fonts'
$mainFontDir = Join-Path $SharedRoot 'harness\Assets\fonts'
if ($SharedRoot -ne $RepoRoot -and (Test-Path $mainFontDir)) {
    New-Item -ItemType Directory -Path $fontDir -Force | Out-Null
    $copied = 0
    Get-ChildItem $mainFontDir -File | ForEach-Object {
        $dst = Join-Path $fontDir $_.Name
        if (-not (Test-Path $dst)) { Copy-Item $_.FullName $dst -Force; $copied++ }
    }
    Write-Host "[build-harness] worktree:字体已就位(新增 $copied 个)-> $fontDir"
}
# 签名证书同样 gitignored(*.pfx)。3KB,口令已明文写在 vcxproj 里(不是秘密),拷一份即可。
$pfxName = 'Harness_TemporaryKey.pfx'
$pfxDst  = Join-Path $HarnessDir $pfxName
$pfxSrc  = Join-Path $SharedRoot "harness\$pfxName"
if ($SharedRoot -ne $RepoRoot -and -not (Test-Path $pfxDst) -and (Test-Path $pfxSrc)) {
    Copy-Item $pfxSrc $pfxDst -Force
    Write-Host "[build-harness] worktree:签名证书已就位 -> $pfxDst"
}

if (-not (Test-Path -LiteralPath $vswhere)) { throw "vswhere.exe was not found: $vswhere" }

$resolverArgs = @{ SdkVersion = $SdkVersion }
if ($VCToolsVersion) { $resolverArgs['Preferred'] = $VCToolsVersion }
$tc = & (Join-Path $PSScriptRoot 'resolve-arm32-toolchain.ps1') @resolverArgs
if ($VCToolsVersion -and $tc.MSVCVersion -ne $VCToolsVersion) {
    throw "Requested MSVC $VCToolsVersion cannot target ARM32 (or is not installed); refusing to silently build with $($tc.MSVCVersion)."
}
$VCToolsVersion = $tc.MSVCVersion

$vs = $tc.VSInstall
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\amd64\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) { throw "MSBuild.exe was not found: $msbuild" }
$sdkRoot = $tc.SdkRoot

$generator = Join-Path $PSScriptRoot 'gen-xaml-codebehind.ps1'
$commonArgs = @(
    $proj,
    '/p:Configuration=Release',
    '/p:Platform=ARM',
    "/p:ApotheosisWindowsSdkVersion=$SdkVersion",
    "/p:VCToolsVersion=$VCToolsVersion",
    "/p:ApotheosisVcpkgRoot=$VcpkgRoot",
    "/p:ApotheosisIcuRoot=$IcuRoot",
    $sharedArg
    $packArg
)
if ($EngineBuildDir) { $commonArgs += "/p:ApotheosisEngineBuildDir=$EngineBuildDir" }
if ($DriverDir) { $commonArgs += "/p:ApotheosisDriverDir=$DriverDir" }
Write-Host "=== Harness: XAML=$XamlMode, SDK=$SdkVersion, MSVC=$VCToolsVersion, Platform=ARM (ARM32) ===" -ForegroundColor Cyan

if ($Clean) {
    Write-Host '=== [clean] v143 outputs ===' -ForegroundColor Cyan
    & $msbuild @commonArgs /p:ApotheosisConsumeOfficialXaml=true /t:Clean /v:minimal
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if ($XamlMode -eq 'Official') {
    $vs2017 = & $vswhere -latest -products '*' -version '[15.0,16.0)' -requires Microsoft.Component.MSBuild -property installationPath
    if (-not $vs2017) { throw 'Visual Studio 2017 with MSBuild is required for official C++/CX XAML codegen.' }
    $msbuild2017 = Join-Path $vs2017 'MSBuild\15.0\Bin\amd64\MSBuild.exe'
    $v141ArmCompiler = Join-Path $vs2017 'VC\Tools\MSVC\14.16.27023\bin\Hostx64\arm\cl.exe'
    $xamlTask = Join-Path $sdkRoot "bin\$XamlSdkVersion\XamlCompiler\Microsoft.Windows.UI.Xaml.Build.Tasks.dll"
    foreach ($path in @($msbuild2017, $v141ArmCompiler, $xamlTask)) {
        if (-not (Test-Path -LiteralPath $path)) { throw "Official XAML dependency was not found: $path" }
    }

    $xamlArgs = @(
        $proj,
        '/p:Configuration=Release',
        '/p:Platform=ARM',
        '/p:PlatformToolset=v141',
        "/p:ApotheosisWindowsSdkVersion=$XamlSdkVersion",
        '/p:ApotheosisUseOfficialXaml=true',
        '/p:ApotheosisXamlCodegen=true',
        '/p:IntDir=Harness\ARM\XamlCodegen\',
        $sharedArg,
        $packArg
    )
    # BuildCompile also links, so this pass needs the same engine/driver trees as the v143 pass;
    # otherwise it falls back to <root>\build-clang-gpu and fails with LNK1181 WebCore.lib.
    if ($EngineBuildDir) { $xamlArgs += "/p:ApotheosisEngineBuildDir=$EngineBuildDir" }
    if ($DriverDir) { $xamlArgs += "/p:ApotheosisDriverDir=$DriverDir" }
    Write-Host "=== [1/2] Official XAML codegen: VS2017 + SDK $XamlSdkVersion ===" -ForegroundColor Cyan
    & $msbuild2017 @xamlArgs /t:Clean /v:minimal
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $msbuild2017 @xamlArgs /t:BuildCompile /m /v:minimal
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $generated = @('App.g.h', 'App.g.hpp', 'App.xbf', 'MainPage.g.h', 'MainPage.g.hpp', 'MainPage.xbf', 'XamlTypeInfo.Impl.g.cpp', 'XamlTypeInfo.g.cpp')
    $missingGenerated = $generated | Where-Object { -not (Test-Path -LiteralPath (Join-Path $root "harness\Generated Files\$_")) }
    if ($missingGenerated) { throw "Official XAML codegen did not produce:`n$($missingGenerated -join "`n")" }
    $buildArgs = @($commonArgs + '/p:ApotheosisConsumeOfficialXaml=true')
} else {
    Write-Host '=== [1/2] Generate XamlReader fallback ===' -ForegroundColor Cyan
    & pwsh -NoProfile -File $generator
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $buildArgs = $commonArgs
}

Write-Host '=== [2/2] Build and package appx ===' -ForegroundColor Cyan
& $msbuild @buildArgs /m /v:minimal 2>&1 | Tee-Object $log
$code = $LASTEXITCODE
Write-Host "=== MSBuild exit code: $code ==="
exit $code
