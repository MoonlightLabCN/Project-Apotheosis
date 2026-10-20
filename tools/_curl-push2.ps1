$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"; $hd="$env:TEMP\wdp-hdr.txt"
$pfn='EdgeHTMLReborn.Harness_0.2.5.29_arm__x0rqga78m2mgc'
$parts=$pfn -split '_'
$family="$($parts[0])_$($parts[-1])"
$aumid="$family!App"
$pfnB64=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($pfn))
$aumidB64=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($aumid))
function Wdp($m,$p,$bodyFile,$ctype){
  & curl.exe -sk -b $jar -c $jar -D $hd -o "$env:TEMP\wdp-resp.txt" -w '%{http_code}' -X $m -H "X-CSRF-Token: $($script:Tok)" @(if($bodyFile){"-H","Content-Type: $ctype"; "--data-binary","@$bodyFile"}) "$base$p"
}
# 初次握手
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$m1=Get-Content $jar | Where-Object { $_ -match 'CSRF-Token\s+(\S+)' } | Select-Object -Last 1
$script:Tok=($m1 -split '\s+')[6]
Write-Host "tok=$($script:Tok)"
# 终止 app
$code=Wdp DELETE "/api/taskmanager/app?package=$([uri]::EscapeDataString($pfnB64))" $null $null
Write-Host "terminate: $code"
Start-Sleep -Seconds 3
# 刷新 token 后再推
& curl.exe -sk -b $jar -c $jar -o NUL "$base/api/os/info"
$m1=Get-Content $jar | Where-Object { $_ -match 'CSRF-Token\s+(\S+)' } | Select-Object -Last 1
$script:Tok=($m1 -split '\s+')[6]
Write-Host "tok2=$($script:Tok)"
$b=[System.Guid]::NewGuid().ToString('N')
$body="--$b`r`nContent-Disposition: form-data; name=`"testurl.txt`"; filename=`"testurl.txt`"`r`nContent-Type: application/octet-stream`r`n`r`nhttp://192.168.3.108:8099/values.html`r`n--$b--`r`n"
$tmp="$env:TEMP\testurl-body.txt"
[System.IO.File]::WriteAllText($tmp,$body)
$code=Wdp POST "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$([uri]::EscapeDataString($pfn))&path=%5CLocalState" $tmp "multipart/form-data; boundary=$b"
Write-Host "push: $code"
Get-Content "$env:TEMP\wdp-resp.txt" -Raw
