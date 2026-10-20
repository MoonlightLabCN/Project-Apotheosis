param(
    [string]$Ip='192.168.3.159'
)
Add-Type -AssemblyName System.Net.Http
$h=[System.Net.Http.HttpClientHandler]::new()
$h.ServerCertificateCustomValidationCallback=[System.Net.Http.HttpClientHandler]::DangerousAcceptAnyServerCertificateValidator
$h.CookieContainer=[System.Net.Http.CookieContainer]::new()
Write-Host "OK $($h.CookieContainer -ne $null)"
