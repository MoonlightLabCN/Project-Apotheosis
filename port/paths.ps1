# ============================================================================
#  paths.ps1 — 统一路径解析:让同一套构建脚本在主目录与 git worktree 里都能跑。
#
#  背景:两条开发线(媒体/硬解、UX 质量)并行时,各占一个 worktree 才不会互相覆盖
#  工作区。但脚本里大量 'E:\Apotheosis\...' 硬编码,在 worktree 里全部指错。
#
#  点源用法(在脚本顶部):
#      . "$PSScriptRoot\paths.ps1"        # port\ 下的脚本
#      . "$PSScriptRoot\..\port\paths.ps1" # tools\ 下的脚本
#
#  解析规则:
#    $RepoRoot  = 当前 worktree 的根(脚本所在目录的上一级)      —— 源码/脚本/补丁用它
#    $SharedRoot = 共享的"重产物"根 = 主 checkout(非 worktree)  —— 引擎/ANGLE/依赖用它
#
#  为什么引擎产物要共享而不各自一份:
#    build-clang-gpu 是 GB 级、全量构建 ~1h45m。每个 worktree 各建一份既费时又费盘
#    (本机 16GB 内存,一次构建已经吃满)。两条线改的东西大多不同,共享同一棵引擎树
#    是刻意的取舍 —— 若某条线改了 WebKit\ 源码,必须重新 ninja 后**另一条线也要重链**,
#    这是共享的代价,写在这里免得下次踩。
# ============================================================================

$ErrorActionPreference = 'Stop'

# --- 当前 worktree 根:脚本目录的上一级(port\ 或 tools\ 都在根下一层) ---
$scriptDir = $PSScriptRoot
$RepoRoot  = Split-Path -Parent $scriptDir

# --- 共享产物根:优先用环境变量覆盖(测试/特殊布局),否则问 git ---
if ($env:APOTHEOSIS_SHARED_ROOT -and (Test-Path $env:APOTHEOSIS_SHARED_ROOT)) {
    $SharedRoot = $env:APOTHEOSIS_SHARED_ROOT
} else {
    $SharedRoot = $RepoRoot
    try {
        # --git-common-dir 在 worktree 里指向主 checkout 的 .git,在主目录里就是 .git
        $common = & git -C $RepoRoot rev-parse --path-format=absolute --git-common-dir 2>$null
        if ($LASTEXITCODE -eq 0 -and $common) {
            $mainRoot = Split-Path -Parent $common.Trim()
            if ($mainRoot -and (Test-Path $mainRoot)) { $SharedRoot = $mainRoot }
        }
    } catch {
        # 没有 git(或不是仓库):退化成"共享 = 当前根",行为与改造前一致
    }
}

# --- 常用组合路径 ---
$PortDir      = Join-Path $RepoRoot   'port'
$HarnessDir   = Join-Path $RepoRoot   'harness'
$ToolsDir     = Join-Path $RepoRoot   'tools'
$DocsDir      = Join-Path $RepoRoot   'docs'
$PatchesDir   = Join-Path $RepoRoot   'patches'

# 引擎/依赖产物(共享)
$EngineBuildDir = Join-Path $SharedRoot 'build-clang-gpu'
$AngleDir       = Join-Path $SharedRoot 'angle'
$AngleLibDir    = Join-Path $AngleDir   'arm'
# WebKit\ 上游源也是 GB 级且 gitignored:worktree 里没有,共享主 checkout 的那一份。
# 注意:共享它意味着"两条线改的是同一棵上游树" —— 改 WebKit\ 前必须确认另一条线没在
# 同文件上工作,改完必须重新生成补丁(见 AGENTS.md 铁律)。
$WebKitRoot     = Join-Path $SharedRoot 'WebKit'

function Write-PathSummary {
    Write-Host "[paths] repo   = $RepoRoot"
    if ($SharedRoot -ne $RepoRoot) {
        Write-Host "[paths] shared = $SharedRoot   (worktree: 引擎/ANGLE 产物从主 checkout 借用)"
    }
    Write-Host "[paths] engine = $EngineBuildDir"
}
