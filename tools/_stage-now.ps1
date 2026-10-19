$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
Remove-Item $jar -ErrorAction SilentlyContinue
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$m1=Get-Content $jar | Where-Object { $_ -match 'CSRF-Token\s+(\S+)' } | Select-Object -Last 1
$tok=($m1 -split '\s+')[6]
$pkgs=(& curl.exe -sk -b $jar "$base/api/app/packagemanager/packages")|ConvertFrom-Json
$pkg=$pkgs.InstalledPackages | Where-Object { $_.PackageFullName -match 'Harness' } | Sort-Object { $v=($_.PackageFullName -split '_')[1]; $p=$v -split '\.'; [int]$p[0]*1000000+[int]$p[1]*10000+[int]$p[2]*100+[int]$p[3] } -Descending | Select-Object -First 1
$pfn=$pkg.PackageFullName
$pfnE=[uri]::EscapeDataString($pfn)
function Get-F($name){ & curl.exe -sk -b $jar "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState&filename=$name" }
Write-Host "installed: $pfn"
Write-Host '===== stage.txt (full) ====='
Get-F 'stage.txt'
Write-Host '===== gpuinit.txt ====='
Get-F 'gpuinit.txt'
Write-Host '===== gpu-crash.flag ====='
Get-F 'gpu-crash.flag'
Write-Host '===== Harness process ====='
$pr=(& curl.exe -sk -b $jar "$base/api/resourcemanager/processes")|ConvertFrom-Json
$h=$pr.Processes | Where-Object { $_.ImageName -match 'Harness' }
if($h){ $h | ForEach-Object { "pid=$($_.ProcessId) ws=$([int]($_.WorkingSetSize/1MB))MB cpu=$($_.CPUUsage)" } } else { Write-Host '(not running)' }
