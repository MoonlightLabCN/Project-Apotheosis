$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
Remove-Item $jar -ErrorAction SilentlyContinue
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$pfn='EdgeHTMLReborn.Harness_0.2.5.30_arm__x0rqga78m2mgc'
$pfnE=[uri]::EscapeDataString($pfn)
function Get-F($name){
  $r = & curl.exe -sk -b $jar -w "`n@@%{http_code}" "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState&filename=$name"
  $r
}
foreach($f in 'autodiag.txt','autodump.txt','settings.txt','mem.txt'){
  Write-Host "===== $f ====="
  $c = Get-F $f
  if($c -match '@@200'){
    $body = ($c -replace "`n@@200$","")
    if($body.Length -gt 1500){ Write-Host $body.Substring(0,1500); Write-Host "...(truncated, total $($body.Length) chars)" } else { Write-Host $body }
  } else { Write-Host "(not present: $($c -replace "`n","|"))" }
}
