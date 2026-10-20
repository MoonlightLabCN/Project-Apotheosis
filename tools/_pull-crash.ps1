$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
Remove-Item $jar -ErrorAction SilentlyContinue
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$pkgs=(& curl.exe -sk -b $jar "$base/api/app/packagemanager/packages")|ConvertFrom-Json
$pkg=$pkgs.InstalledPackages | Where-Object { $_.PackageFullName -match 'Harness' } | Sort-Object { $v=($_.PackageFullName -split '_')[1]; $p=$v -split '\.'; [int]$p[0]*1000000+[int]$p[1]*10000+[int]$p[2]*100+[int]$p[3] } -Descending | Select-Object -First 1
$pfnE=[uri]::EscapeDataString($pkg.PackageFullName)
$cr = & curl.exe -sk -b $jar "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState&filename=crash.txt"
$lines = $cr -split "`n"
Write-Host "total lines: $($lines.Count)"
Write-Host '--- first 12 lines (header/format) ---'
$lines | Select-Object -First 12 | ForEach-Object { Write-Host $_ }
Write-Host '--- lines containing 2026 or timestamps (newest entries) ---'
$lines | Select-String -Pattern '20[0-9]{2}|=====|Crash|pid' | Select-Object -Last 12 | ForEach-Object { $_.Line }
Write-Host '--- last 20 lines ---'
$lines | Select-Object -Last 20 | ForEach-Object { Write-Host $_ }