$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$pr=& curl.exe -sk -b $jar "$base/api/resourcemanager/processes"
$j=$pr|ConvertFrom-Json
Write-Host "--- first 3 raw entries ---"
$j.Processes | Select-Object -First 3 | ForEach-Object { Write-Host ($_ | ConvertTo-Json -Compress) }
