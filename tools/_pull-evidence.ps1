# 拉取媒体轮证据:stage.txt(media 行)+ 服务器日志(设备请求)+ console.txt 尾 + crash.txt 行数。
$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
Remove-Item $jar -ErrorAction SilentlyContinue
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$pkgs=(& curl.exe -sk -b $jar "$base/api/app/packagemanager/packages")|ConvertFrom-Json
$pkg=$pkgs.InstalledPackages | Where-Object { $_.PackageFullName -match 'Harness' } | Sort-Object { $v=($_.PackageFullName -split '_')[1]; $p=$v -split '\.'; [int]$p[0]*1000000+[int]$p[1]*10000+[int]$p[2]*100+[int]$p[3] } -Descending | Select-Object -First 1
$pfn=$pkg.PackageFullName
$pfnE=[uri]::EscapeDataString($pfn)
Write-Host "installed: $pfn"
function Get-F($name){ & curl.exe -sk -b $jar "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState&filename=$name" }
$stage=Get-F 'stage.txt'
Write-Host "`n===== stage.txt: media line(s) ====="
$m=($stage -split "`n")|Where-Object{$_ -match '^\s*media '}
if($m){$m|ForEach-Object{$_.TrimEnd()}}else{Write-Host '(no media line yet)'}
Write-Host "`n===== stage.txt: media-state ====="
($stage -split "`n")|Where-Object{$_ -match 'media-state'}|Select-Object -Last 1|ForEach-Object{$_.TrimEnd()}
Write-Host "`n===== stage.txt: timeline / before-after-load / static-render ====="
($stage -split "`n")|Where-Object{$_ -match '^timeline |before-load|after-load|static-render|gpu-surface'}|Select-Object -Last 8|ForEach-Object{$_.TrimEnd()}
Write-Host "`n===== server log (device requests, last 25) ====="
$log='E:\Apotheosis\port\_mediatest\server.log'
Get-Content $log | Where-Object { $_ -match [regex]::Escape($ip) } | Select-Object -Last 25 | ForEach-Object { $_ }
Write-Host "`n===== console.txt (val- entries, last 10) ====="
$con=Get-F 'console.txt'
($con -split "`n")|Where-Object{$_ -match 'val-|MEDIADIAG'}|Select-Object -Last 10|ForEach-Object{$_.TrimEnd()}
Write-Host "`n===== mediaaudio.txt (harness audio graph diagnostics) ====="
$ma=Get-F 'mediaaudio.txt'
if($ma -and $ma.Trim()){ ($ma -split "`n") | Select-Object -Last 20 | ForEach-Object{$_.TrimEnd()} } else { Write-Host '(empty or absent)' }
Write-Host "`n===== crash.txt ====="
$cr=Get-F 'crash.txt'
$cl=($cr -split "`n")|Where-Object{$_.Trim()}
Write-Host "total lines: $($cl.Count)"
$cl|Select-Object -Last 4|ForEach-Object{$_.TrimEnd()}
