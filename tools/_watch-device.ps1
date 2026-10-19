# 设备在线看守:每 45s 探一次 WDP,连上就在状态文件里写 ONLINE 并退出。
$ip='192.168.3.159'
$out='E:\Apotheosis\port\_mediatest\watch-state.txt'
for($i=0;$i -lt 240;$i++){   # 最多 3 小时
  $ok=$false
  try{
    $r = & curl.exe -sk --max-time 6 "https://${ip}:443/api/os/info" 2>$null
    if($LASTEXITCODE -eq 0 -and $r -match 'ComputerName'){ $ok=$true }
  }catch{}
  if($ok){
    "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ONLINE $r" | Set-Content $out
    Write-Host "ONLINE at $(Get-Date -Format 'HH:mm:ss')"
    exit 0
  }
  "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') offline (try $i)" | Set-Content $out
  Start-Sleep -Seconds 45
}
'timeout' | Set-Content $out
Write-Host 'timeout'
