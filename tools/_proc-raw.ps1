$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$pr=& curl.exe -sk -b $jar "$base/api/resourcemanager/processes"
Write-Host "raw length: $($pr.Length)"
try{
  $j=$pr|ConvertFrom-Json
  Write-Host "process count: $($j.Processes.Count)"
  $j.Processes | Sort-Object WorkingSet -Descending | Select-Object -First 15 | ForEach-Object { Write-Host "$($_.ProcessName) pid=$($_.ProcessId) ws=$([math]::Round($_.WorkingSet/1MB))MB pkg=$($_.PackageFullName)" }
}catch{ Write-Host "parse fail: $($_.Exception.Message)"; Write-Host $pr.Substring(0,[Math]::Min(500,$pr.Length)) }
