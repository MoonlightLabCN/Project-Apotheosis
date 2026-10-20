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
$pfnE=[uri]::EscapeDataString($pfn)
function Wdp($m,$p){
  & curl.exe -sk -b $jar -c $jar -o "$env:TEMP\wdp-resp.txt" -w '%{http_code}' -X $m -H "X-CSRF-Token: $tok" -H 'Content-Length: 0' "$base$p"
}
Write-Host '--- processes (Harness?) ---'
$pr=& curl.exe -sk -b $jar "$base/api/resourcemanager/processes"
if($pr){ ($pr | ConvertFrom-Json).Processes | Where-Object { $_.AppName -match 'Harness|EdgeHTML' -or $_.ProcessName -match 'Harness' } | ForEach-Object { Write-Host "PROC: $(($_ | ConvertTo-Json -Compress))" } } else { Write-Host '(empty)' }
Write-Host "--- enable crashcontrol: $(Wdp POST "/api/debug/dump/usermode/crashcontrol?packageFullName=$pfnE") ---"
$d0=& curl.exe -sk -b $jar "$base/api/debug/dump/usermode/dumps?packageFullName=$pfnE"
Write-Host "dumps before: $d0"
Write-Host "launch: $(Wdp POST "/api/taskmanager/app?appid=$([uri]::EscapeDataString($aumidB64))&package=$([uri]::EscapeDataString($pfnB64))")"
Start-Sleep -Seconds 20
Write-Host '--- processes after launch ---'
$pr2=& curl.exe -sk -b $jar "$base/api/resourcemanager/processes"
if($pr2){ ($pr2 | ConvertFrom-Json).Processes | Where-Object { $_.AppName -match 'Harness|EdgeHTML' } | ForEach-Object { Write-Host "PROC: $(($_ | ConvertTo-Json -Compress))" } } else { Write-Host '(empty)' }
$d1=& curl.exe -sk -b $jar "$base/api/debug/dump/usermode/dumps?packageFullName=$pfnE"
Write-Host "dumps after: $d1"
Write-Host '--- server log tail ---'
