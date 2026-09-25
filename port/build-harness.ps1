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
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$proj = Join-Path $root 'harness\Harness.vcxproj'
$log = Join-Path $root 'harness-build.log'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

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
    "/p:ApotheosisIcuRoot=$IcuRoot"
)
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
        '/p:IntDir=Harness\ARM\XamlCodegen\'
    )
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
