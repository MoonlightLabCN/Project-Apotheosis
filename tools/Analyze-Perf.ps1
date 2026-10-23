#Requires -Version 7.0
<#
.SYNOPSIS
  性能曲线分析:把真机 perf.csv / mem 日志变成"卡在哪一段"的结论。

.DESCRIPTION
  用户抱怨"贼卡"时,第一个问题永远是"卡在哪一段":网络?样式布局?栅格?合成?呈现?
  内存压力?本脚本把 LocalState 下的证据读成一张按阶段排序的账,再给按严重度排序的结论。

  数据源(全部来自设备,见 tools\_pull-perf.ps1):
    perf.csv    — 引擎侧每完成一次顶层操作(nav/scroll/tick/click)一行,含各阶段毫秒数与计数器
    mem.txt     — 内存 tick(仅 perf 开关打开时写)
    browse-log.txt — 每次导航一行摘要

  用法:
    pwsh -File tools\Analyze-Perf.ps1 -Csv <perf.csv> [-TopN 15] [-Json out.json]

  关键列(见 port\WebCoreDriver.cpp 的 kPerfHeader):
    kind            操作类型 nav | scroll | tick | click
    ms_total        该操作总耗时
    ms_net_commit   网络:到 commit
    ms_net_load     网络:到 load
    ms_settle       加载后静置判定(settle_why 说明是谁结束的)
    ms_style_layout 样式 + 布局
    ms_render_update 渲染更新(图层树)
    ms_flush        合成 flush
    ms_backing      后备存储更新
    ms_paint        绘制(栅格)
    ms_readback     离屏读回(软呈现才有)
    ms_swap         eglSwapBuffers(GPU 呈现)
    ms_blit         贴图到 WriteableBitmap(软呈现才有)
    frames          解码帧数
    dirty_full/partial  全量重画 vs 局部重画的图层数
    raster_*        分块栅格:posted/cancelled/done/blocked/deferred
    wake_count      本次操作触发的呈现唤醒数
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Csv,
    [int]$TopN = 15,
    [string]$Json = ''
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Csv)) { Write-Host "找不到 $Csv"; exit 1 }

# ---- 读 CSV(表头是引擎写的,数值列可能是空串) ----
$rows = Import-Csv -Path $Csv
if (-not $rows -or $rows.Count -eq 0) { Write-Host "perf.csv 为空(性能记录开关没开?)"; exit 1 }
Write-Host "读入 $($rows.Count) 行`n"

function Num($v) {
    if ($null -eq $v) { return $null }
    $s = "$v".Trim()
    if ($s -eq '') { return $null }
    $d = 0.0
    if ([double]::TryParse($s, [ref]$d)) { return $d }
    return $null
}
# 取某列的非空值统计
function Stats($rows, [string]$col) {
    $vals = @()
    foreach ($r in $rows) { $n = Num $r.$col; if ($null -ne $n) { $vals += $n } }
    if ($vals.Count -eq 0) { return $null }
    $sorted = $vals | Sort-Object
    $sum = ($vals | Measure-Object -Sum).Sum
    [pscustomobject]@{
        col   = $col
        n     = $vals.Count
        sum   = [math]::Round($sum, 1)
        mean  = [math]::Round($sum / $vals.Count, 2)
        p50   = [math]::Round($sorted[[int]($sorted.Count * 0.50)], 2)
        p90   = [math]::Round($sorted[[int]([math]::Min($sorted.Count - 1, $sorted.Count * 0.90))], 2)
        p99   = [math]::Round($sorted[[int]([math]::Min($sorted.Count - 1, $sorted.Count * 0.99))], 2)
        max   = [math]::Round($sorted[-1], 2)
    }
}

# ---- 按 kind 分组 ----
$kinds = $rows | Group-Object kind | Sort-Object Count -Descending
Write-Host "=== 操作类型分布 ==="
foreach ($k in $kinds) {
    $t = Stats $k.Group 'ms_total'
    Write-Host ("  {0,-8} {1,5} 次   总耗时中位 {2,8} ms   最慢 {3,8} ms" -f $k.Name, $k.Count, $t.p50, $t.max)
}

# ---- 阶段账:按 p90 排序,找出真正贵的阶段 ----
$phaseCols = 'ms_net_commit','ms_net_load','ms_settle','ms_style_layout','ms_render_update',
             'ms_flush','ms_backing','ms_paint','ms_readback','ms_swap','ms_blit'
Write-Host "`n=== 各阶段耗时(p90 降序 = 卡顿的主要来源) ==="
$phaseStats = @()
foreach ($c in $phaseCols) {
    $s = Stats $rows $c
    if ($s) { $phaseStats += $s }
}
$phaseStats | Sort-Object p90 -Descending | ForEach-Object {
    Write-Host ("  {0,-18} n={1,4}  总 {2,9} ms  p50 {3,8}  p90 {4,8}  p99 {5,8}  max {6,8}" -f `
        $_.col, $_.n, $_.sum, $_.p50, $_.p90, $_.p99, $_.max)
}

# ---- 最慢的 N 次操作 ----
Write-Host "`n=== 最慢的 $TopN 次操作 ==="
$rows | Where-Object { $null -ne (Num $_.ms_total) } |
    Sort-Object { Num $_.ms_total } -Descending | Select-Object -First $TopN |
    ForEach-Object {
        $u = $_.url; if ($u.Length -gt 46) { $u = $u.Substring(0, 46) + '…' }
        Write-Host ("  [{0,-6}] {1,9} ms  {2}" -f $_.kind, (Num $_.ms_total), $u)
        Write-Host ("            网commit={0} 布局={1} 渲染更新={2} flush={3} backing={4} 绘制={5} swap={6}" -f `
            $_.ms_net_commit, $_.ms_style_layout, $_.ms_render_update, $_.ms_flush, $_.ms_backing, $_.ms_paint, $_.ms_swap)
    }

# ---- 栅格健康度(分块栅格的三个危险信号) ----
Write-Host "`n=== 栅格健康度 ==="
$rp = Stats $rows 'raster_posted'; $rc = Stats $rows 'raster_cancelled'
$rb = Stats $rows 'raster_blocked'; $rd = Stats $rows 'raster_deferred'
$rf = Stats $rows 'dirty_full';     $rpart = Stats $rows 'dirty_partial'
if ($rp) {
    Write-Host ("  已投递 {0}  已取消 {1}  引擎线程阻塞等待 {2}  推迟上传 {3}" -f $rp.sum, $rc.sum, $rb.sum, $rd.sum)
    if ($rb -and $rb.sum -gt 0) {
        Write-Host "  ⚠️ raster_blocked > 0:引擎线程在等 worker 回填,离屏栅格反而比同步更慢" -ForegroundColor Yellow
    }
    if ($rc -and $rp -and $rc.sum -gt $rp.sum * 0.3) {
        Write-Host "  ⚠️ 取消率 > 30%:滚动太快,worker 来不及做完就被作废(白白烧 CPU)" -ForegroundColor Yellow
    }
}
if ($rf) {
    Write-Host ("  全量重画 {0} 层 / 局部重画 {1} 层" -f $rf.sum, $(if ($rpart) { $rpart.sum } else { 0 }))
    if ($rf.sum -gt 0 -and $rpart -and $rf.sum -gt $rpart.sum) {
        Write-Host "  ⚠️ 全量重画多于局部:脏矩形没起作用,每次都整棵树重栅格" -ForegroundColor Yellow
    }
}

# ---- 呈现开销:GPU vs 软呈现 ----
$gpuRows = $rows | Where-Object { (Num $_.gpu) -eq 1 }
$swRows  = $rows | Where-Object { (Num $_.gpu) -eq 0 }
Write-Host "`n=== 呈现路径 ==="
Write-Host ("  GPU 行 {0} / 软呈现行 {1}" -f $gpuRows.Count, $swRows.Count)
if ($gpuRows.Count) { $s = Stats $gpuRows 'ms_swap';  if ($s) { Write-Host ("  GPU swap:  p50 {0}  p90 {1}  max {2} ms" -f $s.p50, $s.p90, $s.max) } }
if ($swRows.Count)  { $s = Stats $swRows  'ms_blit';  if ($s) { Write-Host ("  软 blit:   p50 {0}  p90 {1}  max {2} ms" -f $s.p50, $s.p90, $s.max) } }

# ---- 帧数/唤醒(动画成本) ----
$wk = Stats $rows 'wake_count'
if ($wk) { Write-Host ("`n=== 呈现唤醒 ===`n  合计 {0}  单次操作最多 {1}" -f $wk.sum, $wk.max) }

# ---- 结论:按严重度给建议 ----
Write-Host "`n=== 结论 ==="
$findings = @()
$worst = $phaseStats | Sort-Object p90 -Descending | Select-Object -First 3
foreach ($w in $worst) {
    $share = if ($phaseStats | Measure-Object -Property sum -Sum) {
        [math]::Round(100.0 * $w.sum / (($phaseStats | Measure-Object -Property sum -Sum).Sum), 1)
    } else { 0 }
    $findings += ("{0}: p90 {1} ms,占总阶段耗时 {2}%" -f $w.col, $w.p90, $share)
}
$findings | ForEach-Object { Write-Host "  · $_" }
Write-Host ""
Write-Host "  提示:ms_paint/ms_backing 高 = 栅格是瓶颈(降低清晰度档位立竿见影);"
Write-Host "        ms_style_layout 高 = 页面自身重排(与我们无关);"
Write-Host "        ms_settle 高 = 等网络或等定时器(settle_why 说明是谁结束的);"
Write-Host "        ms_swap/ms_blit 高 = 呈现链路贵(考虑降到 720p 或关 GPU)。"

if ($Json) {
    [pscustomobject]@{
        rows      = $rows.Count
        kinds     = $kinds | ForEach-Object { [pscustomobject]@{ kind = $_.Name; count = $_.Count } }
        phases    = $phaseStats
        findings  = $findings
        raster    = [pscustomobject]@{ posted = $rp.sum; cancelled = $rc.sum; blocked = $rb.sum; deferred = $rd.sum }
        gpuRows   = $gpuRows.Count
        swRows    = $swRows.Count
    } | ConvertTo-Json -Depth 6 | Set-Content -Path $Json -Encoding UTF8
    Write-Host "`n  JSON 已写:$Json"
}
