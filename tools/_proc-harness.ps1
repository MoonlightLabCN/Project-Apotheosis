$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$pr=& curl.exe -sk -b $jar "$base/api/resourcemanager/processes"
$j=$pr|ConvertFrom-Json
Write-Host "--- Harness processes ---"
$h=$j.Processes | Where-Object { $_.ImageName -match 'Harness' }
if($h){ $h | ForEach-Object { Write-Host "$($_.ImageName) pid=$($_.ProcessId) ws=$([math]::Round($_.WorkingSetSize/1MB))MB user=$($_.UserName)" } } else { Write-Host '(none)' }
Write-Host "--- top 12 by working set ---"
$j.Processes | Sort-Object WorkingSetSize -Descending | Select-Object -First 12 | ForEach-Object { Write-Host "$($_.ImageName) pid=$($_.ProcessId) ws=$([math]::Round($_.WorkingSetSize/1MB))MB" }
