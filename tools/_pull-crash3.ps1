$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
Remove-Item $jar -ErrorAction SilentlyContinue
& curl.exe -sk -c $jar -o NUL "$base/api/os/info" 2>$null
$pkgs=(& curl.exe -sk --max-time 15 -b $jar "$base/api/app/packagemanager/packages")|ConvertFrom-Json
$pkg=$pkgs.InstalledPackages | Where-Object { $_.PackageFullName -match 'Harness' } | Sort-Object { $v=($_.PackageFullName -split '_')[1]; $p=$v -split '\.'; [int]$p[0]*1000000+[int]$p[1]*10000+[int]$p[2]*100+[int]$p[3] } -Descending | Select-Object -First 1
$pfnE=[uri]::EscapeDataString($pkg.PackageFullName)
function Get-F($name){ & curl.exe -sk --max-time 20 -b $jar "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState&filename=$name" }
$ma = Get-F 'mediaaudio.txt'
Write-Host '===== mediaaudio.txt: endpoint probe / idle-stop / graph lifecycle markers ====='
($ma -split "`n") | Where-Object { $_ -match 'endpoint probe|idle-stop|graph created|graph STARTED|Reset\(\)|early-return \(creating|FindAllAsync' } | ForEach-Object { Write-Host $_ }
Write-Host '===== mediaaudio.txt: last 6 non-quantum lines ====='
($ma -split "`n") | Where-Object { $_ -notmatch 'quantum #' -and $_.Trim() } | Select-Object -Last 6 | ForEach-Object { Write-Host $_ }
$cr = Get-F 'crash.txt'
$cl = ($cr -split "`n") | Where-Object { $_.Trim() }
Write-Host '===== crash.txt: entry headers (last 6) ====='
$cl | Where-Object { $_ -match '^==== crash' } | Select-Object -Last 6 | ForEach-Object { Write-Host $_ }
Write-Host '===== crash.txt: reason lines (last 6) ====='
$cl | Where-Object { $_ -match '^reason:' } | Select-Object -Last 6 | ForEach-Object { Write-Host $_ }
Write-Host '===== last crash entry in full ====='
$startIdx = ($cl | Select-String -Pattern '^==== crash' | Select-Object -Last 1).LineNumber
if($startIdx){ $cl[($startIdx-1)..([Math]::Min($cl.Count-1,$startIdx+10))] | ForEach-Object { Write-Host $_ } }
