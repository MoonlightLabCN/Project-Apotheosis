$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$m1=Get-Content $jar | Where-Object { $_ -match 'CSRF-Token\s+(\S+)' } | Select-Object -Last 1
$tok=($m1 -split '\s+')[6]
$pfn='EdgeHTMLReborn.Harness_0.2.5.29_arm__x0rqga78m2mgc'
$parts=$pfn -split '_'
$aumid="$($parts[0])_$($parts[-1])!App"
$pfnB64=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($pfn))
$aumidB64=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($aumid))
function Wdp($m,$p){
  & curl.exe -sk -b $jar -c $jar -o "$env:TEMP\wdp-resp.txt" -w '%{http_code}' -X $m -H "X-CSRF-Token: $tok" -H 'Content-Length: 0' "$base$p"
}
$log='E:\Apotheosis\port\_mediatest\server.log'
$before=(Get-Content $log | Measure-Object -Line).Lines
Write-Host "log lines before: $before"
Write-Host "launch#1: $(Wdp POST "/api/taskmanager/app?appid=$([uri]::EscapeDataString($aumidB64))&package=$([uri]::EscapeDataString($pfnB64))")"
Start-Sleep -Seconds 5
Write-Host "launch#2: $(Wdp POST "/api/taskmanager/app?appid=$([uri]::EscapeDataString($aumidB64))&package=$([uri]::EscapeDataString($pfnB64))")"
for($i=1;$i -le 12;$i++){
  Start-Sleep -Seconds 5
  $now=(Get-Content $log | Measure-Object -Line).Lines
  $h=(& curl.exe -sk -b $jar "$base/api/resourcemanager/processes" | ConvertFrom-Json).Processes | Where-Object { $_.ImageName -match 'Harness' }
  $ws = if($h){ [math]::Round(($h|Select-Object -First 1).WorkingSetSize/1MB) } else { -1 }
  Write-Host "t+$($i*5)s logLines=$now (delta $($now-$before)) harnessWS=${ws}MB"
}
Write-Host '--- new log lines ---'
Get-Content $log | Select-Object -Skip $before | ForEach-Object { Write-Host $_ }
