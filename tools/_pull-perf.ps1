#Requires -Version 7.0
<#
.SYNOPSIS
  从真机拉性能证据(perf.csv / mem.txt / browse-log.txt / stage.txt),供 Analyze-Perf.ps1 分析。

.DESCRIPTION
  WDP 的两个已知坑都处理了:
    · 每个响应轮换 CSRF token —— 先 GET 拿 cookie,再带上本次响应的 X-CSRF-Token;
    · .NET HttpClient TLS 在本机间歇失败 —— 全程用 curl.exe。

  性能数据只有在设备上开了『性能诊断记录』(settings.ini perflog=1)后才有内容。
  本脚本会顺带报告每个文件是否为空,便于区分"没数据"和"拉取失败"。
#>
[CmdletBinding()]
param(
    [string]$Ip = '192.168.3.159',
    [string]$OutDir = "$env:TEMP\apo-perf",
    [string]$PullTool = "$PSScriptRoot\_pull-file.ps1"
)

$ErrorActionPreference = 'Continue'
$base = "https://${Ip}:443"
$jar = "$env:TEMP\wdp-perf-jar.txt"
Remove-Item $jar -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

Write-Host "=== 探活 ==="
& curl.exe -sk -c $jar -o NUL --max-time 15 "$base/api/os/info" 2>$null
if ($LASTEXITCODE -ne 0) { Write-Host "  设备不可达($base)"; exit 1 }
Write-Host "  在线"

Write-Host "=== 取包名 ==="
$raw = $null
for ($i = 1; $i -le 4; $i++) {
    $raw = & curl.exe -sk --max-time 25 -b $jar "$base/api/app/packagemanager/packages" 2>$null
    if ($raw -and $raw.Length -gt 100) { break }
    Write-Host "  第 $i 次失败,重试"
    Start-Sleep -Seconds 4
}
if (-not $raw) { Write-Host "  包列表拉取失败(设备忙?)"; exit 1 }
$pkgs = $raw | ConvertFrom-Json
$pkg = $pkgs.InstalledPackages |
    Where-Object { $_.PackageFullName -match 'Harness' } |
    Sort-Object { $v = ($_.PackageFullName -split '_')[1]; $p = $v -split '\.'; [int]$p[0]*1000000 + [int]$p[1]*10000 + [int]$p[2]*100 + [int]$p[3] } -Descending |
    Select-Object -First 1
if (-not $pkg) { Write-Host "  没找到 Harness 包"; exit 1 }
$pfnE = [uri]::EscapeDataString($pkg.PackageFullName)
Write-Host "  $($pkg.PackageFullName)"

Write-Host "`n=== 拉取 ==="
$wanted = 'perf.csv','mem.txt','browse-log.txt','stage.txt','mediaaudio.txt','crash.txt'
foreach ($f in $wanted) {
    $out = Join-Path $OutDir $f
    Remove-Item $out -ErrorAction SilentlyContinue
    & curl.exe -sk --max-time 40 -b $jar -o $out `
        "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState&filename=$f" 2>$null
    if ((Test-Path $out) -and (Get-Item $out).Length -gt 0) {
        $kb = [int]((Get-Item $out).Length / 1KB)
        Write-Host ("  {0,-16} {1,6} KB" -f $f, $kb)
    } else {
        Write-Host ("  {0,-16} 空/无" -f $f)
        Remove-Item $out -ErrorAction SilentlyContinue
    }
}

$csv = Join-Path $OutDir 'perf.csv'
Write-Host ""
if (Test-Path $csv) {
    $n = (Get-Content $csv | Measure-Object -Line).Lines
    Write-Host "perf.csv 有 $n 行 -> 分析:"
    Write-Host "  pwsh -File `"$PSScriptRoot\Analyze-Perf.ps1`" -Csv `"$csv`""
} else {
    Write-Host "perf.csv 无内容 —— 设备上需要先开设置页的『性能诊断记录』,再操作几页。"
}
Write-Host "输出目录: $OutDir"
