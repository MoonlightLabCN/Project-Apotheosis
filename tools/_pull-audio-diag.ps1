$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
Remove-Item $jar -ErrorAction SilentlyContinue
& curl.exe -sk -c $jar -o NUL "$base/api/os/info" 2>$null
$pkgs=(& curl.exe -sk --max-time 15 -b $jar "$base/api/app/packagemanager/packages")|ConvertFrom-Json
$pkg=$pkgs.InstalledPackages | Where-Object { $_.PackageFullName -match 'Harness' } | Sort-Object { $v=($_.PackageFullName -split '_')[1]; $p=$v -split '\.'; [int]$p[0]*1000000+[int]$p[1]*10000+[int]$p[2]*100+[int]$p[3] } -Descending | Select-Object -First 1
$pfnE=[uri]::EscapeDataString($pkg.PackageFullName)
Write-Host "installed: $($pkg.PackageFullName)"
function Get-F($name){ & curl.exe -sk --max-time 20 -b $jar "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState&filename=$name" }
$ma = Get-F 'mediaaudio.txt'
Write-Host '===== media-diag lines (driver pcm counters) ====='
($ma -split "`n") | Where-Object { $_ -match 'media-diag' } | Select-Object -Last 12 | ForEach-Object { Write-Host $_ }
Write-Host '===== idle-stop execution (deferred Stop actually ran?) ====='
($ma -split "`n") | Where-Object { $_ -match 'executing deferred|stop requested' } | Group-Object { ($_ -split ' ')[1] } | ForEach-Object { "  $($_.Name): $($_.Count) line(s)" }
Write-Host '===== crash.txt: any entry after 00:30? ====='
$cr = Get-F 'crash.txt'
$cl = ($cr -split "`n") | Where-Object { $_.Trim() }
$cl | Where-Object { $_ -match '^==== crash 2026-10-(19|20) 0[0-9]:' } | Select-Object -Last 6 | ForEach-Object { Write-Host "  $_" }
Write-Host "  (crash.txt total lines: $($cl.Count))"