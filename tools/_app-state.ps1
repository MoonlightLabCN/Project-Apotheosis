$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$m1=Get-Content $jar | Where-Object { $_ -match 'CSRF-Token\s+(\S+)' } | Select-Object -Last 1
$tok=($m1 -split '\s+')[6]
$pfn='EdgeHTMLReborn.Harness_0.2.5.29_arm__x0rqga78m2mgc'
$pfnB64=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($pfn))
$h=@{ 'X-CSRF-Token'=$tok; 'Content-Length'='0' }
Write-Host '--- running apps ---'
$r=& curl.exe -sk -b $jar "$base/api/taskmanager/runningapps"
Write-Host ($r -replace '(.{200})', "$1`n")
Write-Host '--- packages (Harness only) ---'
$p=& curl.exe -sk -b $jar "$base/api/app/packagemanager/packages" | ConvertFrom-Json
$p.InstalledPackages | Where-Object { $_.PackageFullName -match 'Harness' } | ForEach-Object { Write-Host "$($_.PackageFullName)  status=$($_.Status)  isRunning=$($_.IsRunning)" }
