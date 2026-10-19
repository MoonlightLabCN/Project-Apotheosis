$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
Remove-Item $jar -ErrorAction SilentlyContinue
& curl.exe -sk -c $jar -o NUL "$base/api/os/info" 2>$null
$pkgs=(& curl.exe -sk --max-time 15 -b $jar "$base/api/app/packagemanager/packages")|ConvertFrom-Json
$pkg=$pkgs.InstalledPackages | Where-Object { $_.PackageFullName -match 'Harness' } | Sort-Object { $v=($_.PackageFullName -split '_')[1]; $p=$v -split '\.'; [int]$p[0]*1000000+[int]$p[1]*10000+[int]$p[2]*100+[int]$p[3] } -Descending | Select-Object -First 1
$pfnE=[uri]::EscapeDataString($pkg.PackageFullName)
Write-Host "installed: $($pkg.PackageFullName)"
function Get-F($name){ & curl.exe -sk --max-time 20 -b $jar "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState&filename=$name" }
Write-Host '===== mediaaudio.txt HEAD (first 25 non-quantum lines) ====='
$ma = Get-F 'mediaaudio.txt'
($ma -split "`n") | Where-Object { $_ -notmatch 'quantum #' } | Select-Object -First 25 | ForEach-Object { Write-Host $_ }
Write-Host '--- quantum summary ---'
$q = ($ma -split "`n") | Where-Object { $_ -match 'quantum #' }
if($q){
  Write-Host "quantum lines: $($q.Count); first: $($q[0]); last: $($q[-1])"
  $taken = $q | Where-Object { $_ -match 'taken=[1-9]' }
  Write-Host "quanta with taken>0: $($taken.Count)"
  if($taken){ Write-Host "  first taken>0: $($taken[0])"; Write-Host "  last  taken>0: $($taken[-1])" }
}
Write-Host '===== crash.txt (last 30 lines) ====='
$cr = Get-F 'crash.txt'
$cl = ($cr -split "`n") | Where-Object { $_.Trim() }
Write-Host "total lines: $($cl.Count)"
$cl | Select-Object -Last 30 | ForEach-Object { Write-Host $_ }
