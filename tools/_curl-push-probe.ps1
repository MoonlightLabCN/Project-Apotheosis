$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"; $hd="$env:TEMP\wdp-hdr.txt"
Remove-Item $jar,$hd -ErrorAction SilentlyContinue
# 1) 建立会话拿 CSRF cookie
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$csrf=(Get-Content $jar | Where-Object { $_ -match 'CSRF-Token\s+(\S+)' } | Select-Object -Last 1)
$tok = if($csrf){ $csrf -split '\s+' | Select-Object -Last 1 } else { '' }
Write-Host "token: $tok"
# 2) 带 cookie+header POST testurl.txt
$b=[System.Guid]::NewGuid().ToString('N')
$body="--$b`r`nContent-Disposition: form-data; name=`"testurl.txt`"; filename=`"testurl.txt`"`r`nContent-Type: application/octet-stream`r`n`r`nhttp://192.168.3.108:8099/values.html`r`n--$b--`r`n"
$tmp="$env:TEMP\testurl-body.txt"
[System.IO.File]::WriteAllText($tmp,$body)
$pfn='EdgeHTMLReborn.Harness_0.2.5.29_arm__x0rqga78m2mgc'
$url="$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$([uri]::EscapeDataString($pfn))&path=%5CLocalState"
$code = & curl.exe -sk -b $jar -c $jar -D $hd -o "$env:TEMP\wdp-resp.txt" -w '%{http_code}' -X POST -H "X-CSRF-Token: $tok" -H "Content-Type: multipart/form-data; boundary=$b" --data-binary "@$tmp" $url
Write-Host "POST status: $code"
Write-Host '--- response headers ---'
Get-Content $hd | Select-Object -First 8
Write-Host '--- response body ---'
Get-Content "$env:TEMP\wdp-resp.txt" -Raw | Select-Object -First 1
