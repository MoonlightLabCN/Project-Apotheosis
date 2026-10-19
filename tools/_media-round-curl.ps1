# _media-round-curl.ps1 — curl 版媒体测试轮(本机 .NET HttpClient 的 TLS 握手不稳定,curl 每次都通)。
# 流程:发现设备上最新的 Harness 包 -> 终止 -> 启动(app 启动时读 LocalState\testurl.txt 导航)
#       -> 等 45s -> 拉 stage.txt(media/media-state/timeline 行)+ console.txt + crash.txt。
# 前提:tools\Test-MediaServer.ps1 -Port 8099 在跑;设备亮屏(息屏会挂起 app,页面停在 t6)。
param([string]$Ip='192.168.3.159',[int]$WaitSeconds=45)
$base="https://${Ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
Remove-Item $jar -ErrorAction SilentlyContinue
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$m1=Get-Content $jar | Where-Object { $_ -match 'CSRF-Token\s+(\S+)' } | Select-Object -Last 1
$tok=($m1 -split '\s+')[6]
# 自动发现最新 Harness 包(按版本号四段排序,避免写死)
$pkgs=(& curl.exe -sk -b $jar "$base/api/app/packagemanager/packages")|ConvertFrom-Json
$pkg=$pkgs.InstalledPackages | Where-Object { $_.PackageFullName -match 'Harness' } | Sort-Object {
  $v=($_.PackageFullName -split '_')[1]; $p=$v -split '\.'; [int]$p[0]*1000000+[int]$p[1]*10000+[int]$p[2]*100+[int]$p[3]
} -Descending | Select-Object -First 1
if(-not $pkg){ Write-Host 'no Harness package installed'; exit 1 }
$pfn=$pkg.PackageFullName
$parts=$pfn -split '_'
$aumid="$($parts[0])_$($parts[-1])!App"
$pfnB64=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($pfn))
$aumidB64=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($aumid))
Write-Host "installed: $pfn"
function Wdp($m,$p){
  & curl.exe -sk -b $jar -c $jar -o "$env:TEMP\wdp-resp.txt" -w '%{http_code}' -X $m -H "X-CSRF-Token: $tok" -H 'Content-Length: 0' "$base$p"
}
function Get-F($name){
  & curl.exe -sk -b $jar "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$([uri]::EscapeDataString($pfn))&path=%5CLocalState&filename=$name"
}
$c1=Wdp DELETE "/api/taskmanager/app?package=$([uri]::EscapeDataString($pfnB64))"
Write-Host "terminate: $c1"
Start-Sleep -Seconds 3
Write-Host "`n===== testurl.txt readback (app navigates to this at startup) ====="
Write-Host (Get-F 'testurl.txt')
$c2=Wdp POST "/api/taskmanager/app?appid=$([uri]::EscapeDataString($aumidB64))&package=$([uri]::EscapeDataString($pfnB64))"
Write-Host "launch: $c2"
Write-Host "waiting ${WaitSeconds}s..."
Start-Sleep -Seconds $WaitSeconds
$stage=Get-F 'stage.txt'
Write-Host "`n===== stage.txt: media line(s) ====="
$m=($stage -split "`n")|Where-Object{$_ -match '^\s*media '}
if($m){$m|Select-Object -Last 4|ForEach-Object{$_.TrimEnd()}}else{Write-Host '(no media line yet)'}
Write-Host "`n===== stage.txt: media-state ====="
($stage -split "`n")|Where-Object{$_ -match 'media-state'}|Select-Object -Last 1|ForEach-Object{$_.TrimEnd()}
Write-Host "`n===== stage.txt: timeline ====="
($stage -split "`n")|Where-Object{$_ -match '^timeline '}|Select-Object -Last 1|ForEach-Object{$_.TrimEnd()}
Write-Host "`n===== stage.txt: static-render / before-load ====="
($stage -split "`n")|Where-Object{$_ -match 'static-render|before-load|after-load'}|Select-Object -Last 4|ForEach-Object{$_.TrimEnd()}
Write-Host "`n===== console.txt: MEDIADIAG/PROBE tail ====="
$con=Get-F 'console.txt'
($con -split "`n")|Where-Object{$_ -match 'MEDIADIAG|PROBE|val-'}|Select-Object -Last 12|ForEach-Object{$_.TrimEnd()}
Write-Host "`n===== crash.txt tail ====="
$cr=Get-F 'crash.txt'
$cl=($cr -split "`n")|Where-Object{$_.Trim()}
Write-Host "total lines: $($cl.Count)"
$cl|Select-Object -Last 8|ForEach-Object{$_.TrimEnd()}
Write-Host "`n===== server log (device requests) ====="
$log='E:\Apotheosis\port\_mediatest\server.log'
if(Test-Path $log){ Get-Content $log | Where-Object { $_ -match [regex]::Escape($Ip) } | Select-Object -Last 15 | ForEach-Object { $_ } } else { Write-Host "(no server log)" }
