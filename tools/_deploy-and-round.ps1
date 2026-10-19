# _deploy-and-round.ps1 — 设备在线后:安装指定版本 appx 并跑一轮媒体测试。
# 用法:pwsh -NoProfile -File E:\Apotheosis\tools\_deploy-and-round.ps1 -Ver 0.2.5.30
param([string]$Ip='192.168.3.159',[string]$Ver='0.2.5.30',[int]$WaitSeconds=45)
$base="https://${Ip}:443"
function DevUp(){
  try{ $r = & curl.exe -sk --max-time 8 "$base/api/os/info" 2>$null; return ($LASTEXITCODE -eq 0 -and $r -match 'ComputerName') }catch{ return $false }
}
if(-not (DevUp)){
  Write-Host "device offline; waiting (up to 60 min)..."
  for($i=0;$i -lt 120;$i++){
    Start-Sleep -Seconds 30
    if(DevUp){ Write-Host "device ONLINE after ~$([int](($i+1)*0.5)) min"; break }
  }
}
if(-not (DevUp)){ Write-Host 'device still offline; aborting'; exit 1 }
Write-Host '--- installing ---'
& pwsh -NoProfile -File E:\Apotheosis\tools\install-only.ps1 -Ip $Ip -Ver $Ver
if($LASTEXITCODE -ne 0){ Write-Host "install-only rc=$LASTEXITCODE"; exit 1 }
Start-Sleep -Seconds 3
Write-Host '--- media round ---'
& pwsh -NoProfile -File E:\Apotheosis\tools\_media-round-curl.ps1 -Ip $Ip -WaitSeconds $WaitSeconds
