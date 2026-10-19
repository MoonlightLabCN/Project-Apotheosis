$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
Remove-Item $jar -ErrorAction SilentlyContinue
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$pfn='EdgeHTMLReborn.Harness_0.2.5.31_arm__x0rqga78m2mgc'
$pfnE=[uri]::EscapeDataString($pfn)
$ma = & curl.exe -sk -b $jar "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState&filename=mediaaudio.txt"
Write-Host "===== mediaaudio.txt (all $((($ma -split "`n").Count)) lines) ====="
$ma -split "`n" | Where-Object { $_ -notmatch 'quantum #' } | ForEach-Object { Write-Host $_ }
Write-Host '--- quantum summary ---'
$q = $ma -split "`n" | Where-Object { $_ -match 'quantum #' }
Write-Host "quantum lines: $($q.Count); first: $($q[0]); last: $($q[-1])"
$under = $q | Where-Object { $_ -match 'taken=0' }
Write-Host "quanta with taken=0: $($under.Count) / $($q.Count)"
