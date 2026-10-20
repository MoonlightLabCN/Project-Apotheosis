$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$pfn='EdgeHTMLReborn.Harness_0.2.5.29_arm__x0rqga78m2mgc'
$pfnE=[uri]::EscapeDataString($pfn)
function Get-F($name){
  $r=& curl.exe -sk -b $jar -w "`n@@%{http_code}" "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState&filename=$name"
  $r
}
foreach($f in 'mem.txt','gpuinit.txt','gpu-crash.flag','settings.txt','testurl.txt'){
  Write-Host "===== $f ====="
  Get-F $f
}
