# ============================================================================
# resolve-arm32-toolchain.ps1 — 唯一的 ARM32 工具链选择点(0.1.9)
#
# 背景 / 为什么需要这个文件:
#   VS18 随系统更新装进来的 MSVC 14.51.36231 **永久删除了 32 位 ARM 支持**。它的
#   include\vadefs.h 顶上写死:
#       #ifdef _M_ARM
#       #error Support for 32-bit ARM has been permanently removed.
#       #endif
#   一旦任何路径(vcvars / IDE / 忘了传 /p:VCToolsVersion / PATH 里谁新用谁)选中
#   14.51,构建就会在几百行日志深处炸出这一句,和真正的原因(选错工具集)看不出关系。
#
#   本脚本把"选哪个工具集"收敛到一处,并且**主动验证 ARM32 能力**而不是信任版本号:
#     1) bin\Hostx64\arm\cl.exe 存在
#     2) lib\arm 存在(ARM32 CRT)
#     3) include\vadefs.h 里**没有**那句 _M_ARM #error
#   三条全过才算候选。优先用 -Preferred 指定的版本;没有则在所有通过的候选里取最新。
#   一个都没有 → 明确报出"缺什么 / 现在有什么 / 该装什么",而不是让编译器去炸。
#
# 用法:
#   $tc = & "$PSScriptRoot\resolve-arm32-toolchain.ps1"            # 返回 hashtable
#   $tc = & "$PSScriptRoot\resolve-arm32-toolchain.ps1" -Quiet     # 不打横幅
#   返回字段: VSInstall MSVCVersion MSVCRoot ClExe LinkExe LibExe ClangCl LldLink
#             SdkRoot SdkVersion TargetArch
# ============================================================================
[CmdletBinding()]
param(
    # 已知可用的 ARM32 工具集(v143 14.44.35207)。找不到时自动降级到任何通过能力检测的版本。
    [string]$Preferred = '14.44.35207',
    # ARM32 UWP 需要的 SDK。26100 已删除 arm32 库;22621 是最后一个带 Lib\...\um\arm 的。
    [string]$SdkVersion = '10.0.22621.0',
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

function Test-Arm32Toolset {
    <#
      判定一个 MSVC 工具集目录是否真的能编 ARM32。只看事实,不看版本号——
      这样 MS 以后再删/再加 ARM 支持都不用改这里。
    #>
    param([string]$ToolsetRoot)

    $cl = Join-Path $ToolsetRoot 'bin\Hostx64\arm\cl.exe'
    if (-not (Test-Path -LiteralPath $cl)) { return @{ Ok = $false; Reason = 'no bin\Hostx64\arm\cl.exe' } }
    if (-not (Test-Path -LiteralPath (Join-Path $ToolsetRoot 'lib\arm'))) { return @{ Ok = $false; Reason = 'no lib\arm (ARM32 CRT)' } }

    # 决定性检查:14.51+ 的 vadefs.h 对 _M_ARM 直接 #error。有 arm\cl.exe 但头文件拒绝的
    # 混合安装同样不可用,所以两样都得查。
    $vadefs = Join-Path $ToolsetRoot 'include\vadefs.h'
    if (Test-Path -LiteralPath $vadefs) {
        if (Select-String -LiteralPath $vadefs -Pattern 'Support for 32-bit ARM has been permanently removed' -Quiet) {
            return @{ Ok = $false; Reason = 'headers reject _M_ARM (ARM32 support removed)' }
        }
    }
    return @{ Ok = $true; Reason = 'ok'; Cl = $cl }
}

# --- 定位 VS ---------------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw "vswhere.exe not found: $vswhere" }
$vsInstall = & $vswhere -latest -products '*' -version '[17.0,19.0)' -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsInstall) { throw 'No Visual Studio 2022+ installation with MSBuild was found.' }

$msvcRoot = Join-Path $vsInstall 'VC\Tools\MSVC'
if (-not (Test-Path -LiteralPath $msvcRoot)) { throw "MSVC tools directory not found: $msvcRoot" }

# --- 枚举 + 能力检测 -------------------------------------------------------
$all = Get-ChildItem -LiteralPath $msvcRoot -Directory | Sort-Object Name -Descending
$report = foreach ($t in $all) {
    $r = Test-Arm32Toolset -ToolsetRoot $t.FullName
    [pscustomobject]@{ Version = $t.Name; Path = $t.FullName; Ok = $r.Ok; Reason = $r.Reason }
}
$capable = @($report | Where-Object Ok)

if (-not $capable.Count) {
    $lines = $report | ForEach-Object { "    {0,-16} REJECTED: {1}" -f $_.Version, $_.Reason }
    throw @"
No ARM32-capable MSVC toolset is installed under:
    $msvcRoot

Installed toolsets and why each was rejected:
$($lines -join "`n")

Apotheosis targets Windows 10 Mobile / Lumia 950 (ARM32, UWP). MSVC 14.51 and later
permanently removed 32-bit ARM; their vadefs.h #errors on _M_ARM.

To fix: in the Visual Studio Installer, open 'Individual components' and install
  'MSVC v143 - VS 2022 C++ ARM build tools (v14.44-17.14)'
(and keep it installed — do NOT let an update remove it). Also required:
  Windows 10 SDK $SdkVersion  (SDK 10.0.26100+ no longer ships Lib\...\um\arm)
"@
}

# 优先用钉住的版本;否则取通过检测的最新一个(**不是** PATH 里/目录里最新的那个)。
$chosen = $capable | Where-Object { $_.Version -eq $Preferred } | Select-Object -First 1
$pinnedMissing = $false
if (-not $chosen) {
    $pinnedMissing = $true
    $chosen = $capable[0]
}

# --- SDK -------------------------------------------------------------------
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdkMust = @(
    (Join-Path $sdkRoot "Include\$SdkVersion\um\Windows.h"),
    (Join-Path $sdkRoot "Lib\$SdkVersion\um\arm\WindowsApp.lib"),
    (Join-Path $sdkRoot "Lib\$SdkVersion\ucrt\arm")
)
$sdkMissing = @($sdkMust | Where-Object { -not (Test-Path -LiteralPath $_) })
if ($sdkMissing.Count) {
    throw @"
Windows SDK $SdkVersion is not usable for ARM32 UWP. Missing:
$($sdkMissing | ForEach-Object { "    $_" } | Out-String)
Install 'Windows 10 SDK ($SdkVersion)' from the Visual Studio Installer.
Note: SDK 10.0.26100 and later no longer ship ARM32 (arm) libraries at all.
"@
}

$clangCl = 'C:\Program Files\LLVM\bin\clang-cl.exe'
$lldLink = 'C:\Program Files\LLVM\bin\lld-link.exe'

$result = @{
    VSInstall   = $vsInstall
    MSVCVersion = $chosen.Version
    MSVCRoot    = $chosen.Path
    ClExe       = Join-Path $chosen.Path 'bin\Hostx64\arm\cl.exe'
    LinkExe     = Join-Path $chosen.Path 'bin\Hostx64\arm\link.exe'
    LibExe      = Join-Path $chosen.Path 'bin\Hostx64\arm\lib.exe'
    ClangCl     = if (Test-Path -LiteralPath $clangCl) { $clangCl } else { $null }
    LldLink     = if (Test-Path -LiteralPath $lldLink) { $lldLink } else { $null }
    SdkRoot     = $sdkRoot
    SdkVersion  = $SdkVersion
    TargetArch  = 'ARM (ARM32 / Thumb-2), UWP App Container'
}

if (-not $Quiet) {
    Write-Host '=== ARM32 toolchain ===================================' -ForegroundColor Cyan
    Write-Host ("  VS install    : {0}" -f $result.VSInstall)
    Write-Host ("  MSVC toolset  : {0}   (ARM32 verified: arm\cl.exe + lib\arm + vadefs.h ok)" -f $result.MSVCVersion) -ForegroundColor Green
    Write-Host ("  cl.exe        : {0}" -f $result.ClExe)
    Write-Host ("  link.exe      : {0}" -f $result.LinkExe)
    Write-Host ("  clang-cl      : {0}" -f ($(if ($result.ClangCl) { $result.ClangCl } else { '<not installed>' })))
    Write-Host ("  lld-link      : {0}" -f ($(if ($result.LldLink) { $result.LldLink } else { '<not installed>' })))
    Write-Host ("  Windows SDK   : {0}  ({1})" -f $result.SdkVersion, $result.SdkRoot)
    Write-Host ("  Target        : {0}" -f $result.TargetArch)
    $rejected = @($report | Where-Object { -not $_.Ok })
    foreach ($r in $rejected) {
        Write-Host ("  rejected      : {0,-16} {1}" -f $r.Version, $r.Reason) -ForegroundColor DarkYellow
    }
    if ($pinnedMissing) {
        Write-Host ("  NOTE: preferred toolset $Preferred is not installed; fell back to $($chosen.Version).") -ForegroundColor Yellow
    }
    Write-Host '=======================================================' -ForegroundColor Cyan
}

$result
