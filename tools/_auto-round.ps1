# _auto-round.ps1 — 全自动媒体验证:等设备上线(最长 3 小时)-> 装 0.2.5.30 -> 跑测试轮 -> 证据落盘。
# 用法:pwsh -NoProfile -File E:\Apotheosis\tools\_auto-round.ps1 [-WaitMinutes 180]
param([string]$Ip='192.168.3.159',[string]$Ver='0.2.5.30',[int]$WaitMinutes=180,[int]$RoundWait=45)
$base="https://${Ip}:443"
$out='E:\Apotheosis\port\_mediatest\auto-round-result.txt'
function Say($m){ $line="[$(Get-Date -Format 'HH:mm:ss')] $m"; Write-Host $line; Add-Content -Path $out -Value $line }
"===== auto round start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ver=$Ver =====" | Set-Content $out
function DevUp(){ try{ $r=& curl.exe -sk --max-time 8 "$base/api/os/info" 2>$null; return ($LASTEXITCODE -eq 0 -and $r -match 'ComputerName') }catch{ return $false } }
Say "waiting for device (up to ${WaitMinutes} min)..."
$up=$false
for($i=0;$i -lt ($WaitMinutes*2);$i++){
  Start-Sleep -Seconds 30
  if(DevUp){ $up=$true; Say "device ONLINE (waited ~$([int](($i+1)*0.5)) min)"; break }
  if($i % 4 -eq 3){ Say "  ...still offline ($([int](($i+1)*0.5)) min)" }
}
if(-not $up){ Say 'device never came online; giving up'; exit 1 }
Say '--- installing 0.2.5.30 ---'
& pwsh -NoProfile -File E:\Apotheosis\tools\install-only.ps1 -Ip $Ip -Ver $Ver *>> $out
Say "install-only rc=$LASTEXITCODE"
if($LASTEXITCODE -ne 0){ Say 'install failed; aborting'; exit 1 }
Start-Sleep -Seconds 3
Say '--- media round ---'
& pwsh -NoProfile -File E:\Apotheosis\tools\_media-round-curl.ps1 -Ip $Ip -WaitSeconds $RoundWait *>> $out
Say "round rc=$LASTEXITCODE"
# 判读:最近 5 分钟服务器日志里有没有设备请求。没有 = app 没导航(极可能是息屏挂起:
# DisplayRequest 只防息屏不能点亮屏幕,息屏启动的 app 会被挂起,页面停在 t6)。
Start-Sleep -Seconds 2
$log='E:\Apotheosis\port\_mediatest\server.log'
if(Test-Path $log){
  $recent = Get-Content $log | Where-Object { $_ -match [regex]::Escape($Ip) -and $_ -match '^\d\d:\d\d:\d\d' } |
    Where-Object { $now=[datetime]::Today.Add([datetime]::Parse($_.Substring(0,8)).TimeOfDay); ($now -gt [datetime]::Now.AddMinutes(-5)) }
  if($recent){ Say "VERDICT: $($recent.Count) device requests in the last 5 min -> app navigated; read the media line above (frames=/pcm=)" }
  else { Say 'VERDICT: NO device requests in the last 5 min -> app did not navigate. Most likely the phone screen is OFF (app suspended at startup). Wake/unlock the phone and rerun _deploy-and-round.ps1' }
} else { Say 'VERDICT: no server log found - is Test-MediaServer.ps1 running?' }
Say 'DONE'
