$ErrorActionPreference='Continue'
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy','NO_PROXY','no_proxy')) { Remove-Item ("Env:" + $n) -ErrorAction SilentlyContinue }
$Ip='192.168.3.159'
$base="https://${Ip}:443"
Add-Type -AssemblyName System.Net.Http
$h=[System.Net.Http.HttpClientHandler]::new()
$h.ServerCertificateCustomValidationCallback=[System.Net.Http.HttpClientHandler]::DangerousAcceptAnyServerCertificateValidator
$h.CookieContainer=[System.Net.CookieContainer]::new()
$cli=[System.Net.Http.HttpClient]::new($h); $cli.Timeout=[TimeSpan]::FromMinutes(2)
$script:Csrf=$null
function Sync($r){ $sc=$null; if($r.Headers.TryGetValues('Set-Cookie',[ref]$sc)){ foreach($l in $sc){ if($l -match 'CSRF-Token=([^;,\s]+)'){ $script:Csrf=$Matches[1]; try{$h.CookieContainer.Add([Uri]$base,[System.Net.Cookie]::new('CSRF-Token',$script:Csrf,'/',$Ip))}catch{} } } } }
function Wdp($m,$p,$c=$null){ $req=[System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::new($m),"$base$p"); if($script:Csrf){$req.Headers.Add('X-CSRF-Token',$script:Csrf)}; if($c){$req.Content=$c}; $r=$cli.SendAsync($req).GetAwaiter().GetResult(); Sync $r; return $r }
function Txt($r){ $r.Content.ReadAsStringAsync().Result }
Sync ($cli.GetAsync("$base/api/os/info").GetAwaiter().GetResult())

Write-Host "=== packages matching Harness ===" -ForegroundColor Cyan
$pk = Txt (Wdp GET '/api/app/packagemanager/packages') | ConvertFrom-Json
$pk.InstalledPackages | Where-Object { $_.PackageFullName -like '*EdgeHTMLReborn.Harness*' } |
  ForEach-Object { "  $($_.PackageFullName)  ver=$($_.Version)" }

Write-Host "=== install state ===" -ForegroundColor Cyan
try {
  $st = Txt (Wdp GET '/api/app/packagemanager/state')
  ($st | ConvertFrom-Json).Packages | Where-Object { $_.PackageFullName -like '*EdgeHTMLReborn.Harness*' } |
    ForEach-Object { "  $($_.PackageFullName)  status=$($_.Status)  err=$($_.ErrorCode)" }
} catch { "  state endpoint: $_" }

Write-Host "=== assert 0.2.5.14 is the installed 13/14 ===" -ForegroundColor Cyan
$ours = $pk.InstalledPackages | Where-Object { $_.PackageFullName -like '*EdgeHTMLReborn.Harness*' }
if ($ours | Where-Object { $_.PackageFullName -like '*0.2.5.14*' }) { Write-Host "PASS: 0.2.5.14 present" -ForegroundColor Green }
else { Write-Host "FAIL: 0.2.5.14 NOT in packages list" -ForegroundColor Red }
$fn = ($ours | Where-Object { $_.PackageFullName -like '*0.2.5.14*' } | Select-Object -First 1).PackageFullName
if ($fn) {
  $f = "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$([uri]::EscapeDataString($fn))&path=%5CLocalState&filename="
  foreach($n in @('jitdiag.txt','testurl.txt','settings.ini')){
    $r = Wdp GET ($f+$n)
    if([int]$r.StatusCode -eq 200){ "  $n = $((Txt $r).Trim())" } else { "  $n = HTTP $([int]$r.StatusCode)" }
  }
}
Write-Host "DONE"
