$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
$pfn='EdgeHTMLReborn.Harness_0.2.5.29_arm__x0rqga78m2mgc'
$pfnE=[uri]::EscapeDataString($pfn)
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$m1=Get-Content $jar | Where-Object { $_ -match 'CSRF-Token\s+(\S+)' } | Select-Object -Last 1
$tok=($m1 -split '\s+')[6]
$b=[System.Guid]::NewGuid().ToString('N')
$body="--$b`r`nContent-Disposition: form-data; name=`"testurl.txt`"; filename=`"testurl.txt`"`r`nContent-Type: application/octet-stream`r`n`r`nhttp://192.168.3.108:8099/values.html`r`n--$b--`r`n"
$tmp="$env:TEMP\testurl-body.txt"
[System.IO.File]::WriteAllText($tmp,$body)
function Try-Push($label,$path){
  $code=& curl.exe -sk -b $jar -c $jar -o "$env:TEMP\wdp-resp.txt" -w '%{http_code}' -X POST -H "X-CSRF-Token: $tok" -H "Content-Type: multipart/form-data; boundary=$b" --data-binary "@$tmp" "$base$path"
  $resp=(Get-Content "$env:TEMP\wdp-resp.txt" -Raw)
  Write-Host "$label => $code  $($resp.Substring(0,[Math]::Min(120,$resp.Length)))"
}
Try-Push 'path=%5CLocalState'      "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState"
Try-Push 'path=LocalState'         "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=LocalState"
Try-Push 'path=%5CLocalState%5C'   "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState%5C"
Try-Push 'with filename param'     "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState&filename=testurl.txt"
Try-Push 'LocalState no kfid'      "/api/filesystem/apps/file?packagefullname=$pfnE&path=%5CLocalState"
