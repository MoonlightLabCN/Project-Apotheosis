$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$pfn='EdgeHTMLReborn.Harness_0.2.5.29_arm__x0rqga78m2mgc'
$pfnE=[uri]::EscapeDataString($pfn)
function Get-F($name){
  & curl.exe -sk -b $jar "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState&filename=$name"
}
Write-Host '===== stage.txt (full) ====='
Get-F 'stage.txt'
Write-Host '===== console.txt (last 25) ====='
$c=Get-F 'console.txt'
($c -split "`n") | Select-Object -Last 25 | ForEach-Object { Write-Host $_ }
