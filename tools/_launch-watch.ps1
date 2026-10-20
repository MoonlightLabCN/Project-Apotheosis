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
Write-Host "launch: $(Wdp POST "/api/taskmanager/app?appid=$([uri]::EscapeDataString($aumidB64))&package=$([uri]::EscapeDataString($pfnB64))")"
foreach($s in 3,5,10){
  Start-Sleep -Seconds $s
  $r=& curl.exe -sk -b $jar -w "`nHTTP:%{http_code}" "$base/api/taskmanager/runningapps"
  Write-Host "runningapps(+${s}s cum): $($r.Substring(0,[Math]::Min(300,$r.Length)))"
}
Write-Host '--- try screenshot ---'
$sc=& curl.exe -sk -b $jar -o "$env:TEMP\dev-shot.png" -w '%{http_code}' "$base/api/osscreenshot"
$len=(Get-Item "$env:TEMP\dev-shot.png" -ErrorAction SilentlyContinue).Length
Write-Host "osscreenshot: $sc size=$len"
