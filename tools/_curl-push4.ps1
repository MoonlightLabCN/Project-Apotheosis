$ip='192.168.3.159'; $base="https://${ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
$pfn='EdgeHTMLReborn.Harness_0.2.5.29_arm__x0rqga78m2mgc'
$pfnE=[uri]::EscapeDataString($pfn)
& curl.exe -sk -c $jar -o NUL "$base/api/os/info"
$m1=Get-Content $jar | Where-Object { $_ -match 'CSRF-Token\s+(\S+)' } | Select-Object -Last 1
$tok=($m1 -split '\s+')[6]
# 1) 读现有 testurl.txt
$g=& curl.exe -sk -b $jar -w "`n%{http_code}" "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState&filename=testurl.txt"
Write-Host "GET testurl.txt: $g"
# 2) 换文件名推送
$b=[System.Guid]::NewGuid().ToString('N')
$body="--$b`r`nContent-Disposition: form-data; name=`"_probe.txt`"; filename=`"_probe.txt`"`r`nContent-Type: application/octet-stream`r`n`r`nhello`r`n--$b--`r`n"
$tmp="$env:TEMP\_probe-body.txt"
[System.IO.File]::WriteAllText($tmp,$body)
$code=& curl.exe -sk -b $jar -c $jar -o "$env:TEMP\wdp-resp.txt" -w '%{http_code}' -X POST -H "X-CSRF-Token: $tok" -H "Content-Type: multipart/form-data; boundary=$b" --data-binary "@$tmp" "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState"
Write-Host "POST _probe.txt: $code  $((Get-Content "$env:TEMP\wdp-resp.txt" -Raw))"
# 3) 目录列表
$l=& curl.exe -sk -b $jar "$base/api/filesystem/apps/list?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState"
Write-Host "LIST: $($l.Substring(0,[Math]::Min(600,$l.Length)))"
