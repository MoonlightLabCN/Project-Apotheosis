$base='https://192.168.3.159:443'
$ws=New-Object Microsoft.PowerShell.Commands.WebRequestSession
foreach($path in @('/api/os/info','/')){
  Write-Host "=== GET $path ==="
  try{
    $r=Invoke-WebRequest -Uri "$base$path" -Method GET -SkipCertificateCheck -NoProxy -UseBasicParsing -WebSession $ws -TimeoutSec 30
    Write-Host "status: $([int]$r.StatusCode)"
    foreach($k in $r.Headers.Keys){ Write-Host "HDR $k : $($r.Headers[$k] -join ' | ')" }
  }catch{ Write-Host "FAIL: $($_.Exception.Message)" }
}
Write-Host '--- session cookies now ---'
$ws.Cookies.GetCookies([Uri]$base) | ForEach-Object { Write-Host "$($_.Name)=$($_.Value) domain=$($_.Domain) path=$($_.Path) secure=$($_.Secure) httponly=$($_.HttpOnly)" }
Write-Host "--- raw curl -v to see Set-Cookie ---"
& curl.exe -sk -v --max-time 15 "$base/api/os/info" 2>&1 | Select-String -Pattern '< Set-Cookie|< HTTP|Cookie:'
