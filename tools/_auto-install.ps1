# 等设备上线 -> 装 0.2.5.32 -> 完成即退(导航仍需用户手动:testurl 钩子失效)。
param([string]$Ip='192.168.3.159',[string]$Ver='0.2.5.32',[int]$WaitMinutes=240)
$base="https://${Ip}:443"
$out='E:\Apotheosis\port\_mediatest\auto-install-result.txt'
function Say($m){ $line="[$(Get-Date -Format 'HH:mm:ss')] $m"; Write-Host $line; Add-Content -Path $out -Value $line }
"===== auto-install start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ver=$Ver =====" | Set-Content $out
function DevUp(){ try{ $r=& curl.exe -sk --max-time 8 "$base/api/os/info" 2>$null; return ($LASTEXITCODE -eq 0 -and $r -match 'ComputerName') }catch{ return $false } }
Say "waiting for device (up to ${WaitMinutes} min)..."
for($i=0;$i -lt ($WaitMinutes*2);$i++){
  Start-Sleep -Seconds 30
  if(DevUp){ Say "device ONLINE (waited ~$([int](($i+1)*0.5)) min)"; break }
  if($i % 4 -eq 3){ Say "  ...still offline ($([int](($i+1)*0.5)) min)" }
}
if(-not (DevUp)){ Say 'device never came online; giving up'; exit 1 }
Say "installing (Deploy-Robust: streaming upload + 4x retry + status polling)..."
& pwsh -NoProfile -File E:\Apotheosis\tools\Deploy-Robust.ps1 -Ip $Ip *>> $out
Say "deploy rc=$LASTEXITCODE"
Say 'READY - user: open the app, type http://192.168.3.108:8099/values.html in the address bar, wait ~15s, then say done'
