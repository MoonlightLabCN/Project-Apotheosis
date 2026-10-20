$base='https://192.168.3.159:443'
$ws=New-Object Microsoft.PowerShell.Commands.WebRequestSession
$r=Invoke-WebRequest -Uri "$base/api/os/info" -Method GET -SkipCertificateCheck -NoProxy -UseBasicParsing -WebSession $ws -TimeoutSec 30
Write-Host "status: $([int]$r.StatusCode)"
Write-Host '--- Set-Cookie headers ---'
$r.Headers.GetValues('Set-Cookie') | ForEach-Object { Write-Host $_ }
Write-Host '--- session cookies ---'
$ws.Cookies.GetCookies([Uri]$base) | ForEach-Object { Write-Host "$($_.Name)=$($_.Value) httponly=$($_.HttpOnly) secure=$($_.Secure)" }
Write-Host '--- try POST with cookie only ---'
try{
  $b=[System.Guid]::NewGuid().ToString('N')
  $body="--$b`r`nContent-Disposition: form-data; name=`"testurl.txt`"; filename=`"testurl.txt`"`r`nContent-Type: application/octet-stream`r`n`r`nhttp://192.168.3.108:8099/values.html`r`n--$b--`r`n"
  $bytes=[System.Text.Encoding]::UTF8.GetBytes($body)
  $r2=Invoke-WebRequest -Uri "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=EdgeHTMLReborn.Harness_0.2.5.29_arm__x0rqga78m2mgc&path=%5CLocalState" -Method POST -Body $bytes -ContentType "multipart/form-data; boundary=$b" -SkipCertificateCheck -NoProxy -UseBasicParsing -WebSession $ws -TimeoutSec 60
  Write-Host "POST cookie-only: $([int]$r2.StatusCode)"
}catch{ Write-Host "POST cookie-only FAIL: $($_.Exception.Message)" }
Write-Host '--- try POST with header too ---'
try{
  $ct=$ws.Cookies.GetCookies([Uri]$base)['CSRF-Token'].Value
  Write-Host "csrf from session: $ct"
  $b=[System.Guid]::NewGuid().ToString('N')
  $body="--$b`r`nContent-Disposition: form-data; name=`"testurl.txt`"; filename=`"testurl.txt`"`r`nContent-Type: application/octet-stream`r`n`r`nhttp://192.168.3.108:8099/values.html`r`n--$b--`r`n"
  $bytes=[System.Text.Encoding]::UTF8.GetBytes($body)
  $r3=Invoke-WebRequest -Uri "$base/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=EdgeHTMLReborn.Harness_0.2.5.29_arm__x0rqga78m2mgc&path=%5CLocalState" -Method POST -Body $bytes -ContentType "multipart/form-data; boundary=$b" -Headers @{'X-CSRF-Token'=$ct} -SkipCertificateCheck -NoProxy -UseBasicParsing -WebSession $ws -TimeoutSec 60
  Write-Host "POST header+cookie: $([int]$r3.StatusCode)"
}catch{ Write-Host "POST header+cookie FAIL: $($_.Exception.Message)" }
