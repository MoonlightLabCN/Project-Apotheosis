#Requires -Version 7.0
<#
.SYNOPSIS
  校验上游补丁的完整性 —— "防丢失"检查。

.DESCRIPTION
  WebKit/ 不在仓库(GB 级,gitignored),所有上游改动只能靠补丁入库。本项目**已两次**
  因"补丁清单不全"而面临工作丢失:
    1) 媒体线的 4 个新文件
    2) 上一轮会话的 CheckedPtr 修复(CheckedRef.h / Assertions.cpp / FastMalloc.h / RunLoopGeneric.cpp)

  ★ 补丁是两层的,别混淆职责:
    · wk-winuwp.patch (仓库根, ~785KB / 167 文件)  = 平台移植全量(ARM32/JSC JIT/WebCore)
    · patches\2026-10-09-winuwp-media-mf.patch     = 媒体线增量(本次检查对象)

  本脚本检查三件事:
    A) 媒体补丁的文件清单 == .expected-files.txt        (防"重新生成时静默少文件")
    B) 媒体补丁非空且结构合法(git apply --numstat)
    C) 媒体补丁在 WebKit\ 里能反向应用(证明与工作区一致)

  退出码: 0 = 通过, 1 = 有问题
#>
[CmdletBinding()]
param(
    [string]$Patch    = "$PSScriptRoot\..\patches\2026-10-09-winuwp-media-mf.patch",
    [string]$Expected = "$PSScriptRoot\..\patches\2026-10-09-winuwp-media-mf.expected-files.txt",
    [string]$WebKitRoot = "$PSScriptRoot\..\WebKit"
)

$ErrorActionPreference = 'Stop'
$fail = @()

if (-not (Test-Path $Patch))    { Write-Host "::error::补丁不存在: $Patch"; exit 1 }
if (-not (Test-Path $Expected)) { Write-Host "::error::清单不存在: $Expected"; exit 1 }

$inPatch = Select-String -Path $Patch -Pattern '^diff --git a/(.+?) b/' |
    ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique

$inExpected = Get-Content $Expected |
    Where-Object { $_.Trim() -and -not $_.Trim().StartsWith('#') } |
    ForEach-Object { $_.Trim() } | Sort-Object -Unique

Write-Host "补丁覆盖: $($inPatch.Count) 文件; 期望清单: $($inExpected.Count) 文件"

# ==== A: 清单一致性 ====
$missing = $inExpected | Where-Object { $_ -notin $inPatch }
$extra   = $inPatch    | Where-Object { $_ -notin $inExpected }
if ($missing) { $fail += "补丁缺少清单中的文件(改动可能丢失):`n    " + ($missing -join "`n    ") }
if ($extra)   { $fail += "补丁含清单外的文件(请确认是有意新增,并更新 .expected-files.txt):`n    " + ($extra -join "`n    ") }
if (-not $missing -and -not $extra) { Write-Host "  [A] 补丁与期望清单一致 OK" }

# ==== B: 结构合法 ====
& git apply --numstat $Patch > $null 2>&1
if ($LASTEXITCODE -ne 0) { $fail += "补丁不是合法的 git diff: $Patch" }
else {
    $lines = (Select-String -Path $Patch -Pattern '^diff --git').Count
    Write-Host "  [B] 结构合法 OK ($lines 个文件块)"
}

# ==== C: 反向应用(证明与 WebKit\ 工作区一致,仅开发机) ====
if (Test-Path (Join-Path $WebKitRoot '.git')) {
    $rc = & git -C $WebKitRoot apply --check -R $Patch 2>&1
    # "needs merge" 是 WebKit\ 里 sparse checkout 遗留的 unmerged 条目造成的噪音,
    # 与补丁本身无关(本项目 WebKit\ 长期处于 33 个 UU 条目的状态)。
    $realIssues = $rc | Where-Object { $_ -notmatch 'needs merge' }
    if ($LASTEXITCODE -ne 0 -and $realIssues) {
        $fail += "补丁无法在 WebKit\ 反向应用(与工作区不一致,请重新生成):`n    " + ($realIssues -join "`n    ")
    } else {
        Write-Host "  [C] 反向应用 OK(与 WebKit\ 工作区一致)"
    }
} else {
    Write-Host "  [C] 跳过(WebKit\ 不存在,CI 环境正常)"
}

if ($fail) {
    Write-Host ""
    foreach ($f in $fail) { Write-Host "::error::$f" }
    Write-Host ""
    Write-Host "重新生成媒体补丁的正确做法(在开发机上):"
    Write-Host '  cd E:\Apotheosis\WebKit'
    Write-Host '  git add -N -- <新增文件>'
    Write-Host '  git diff HEAD -- <全部文件> > E:\Apotheosis\patches\2026-10-09-winuwp-media-mf.patch'
    Write-Host '  git reset -q -- <新增文件>'
    Write-Host '  # 然后同步更新 .expected-files.txt'
    exit 1
}
Write-Host ""
Write-Host "补丁完整性校验通过"
exit 0
