$ErrorActionPreference='Continue'
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy','NO_PROXY','no_proxy')) { Remove-Item ("Env:" + $n) -ErrorAction SilentlyContinue }
$Ip='192.168.3.159'; $Ver='0.2.5.14'
$FN="EdgeHTMLReborn.Harness_${Ver}_arm__x0rqga78m2mgc"; $fNe=[uri]::EscapeDataString($FN)
$base="https://${Ip}:443"
Add-Type -AssemblyName System.Net.Http
$h=[System.Net.Http.HttpClientHandler]::new()
$h.ServerCertificateCustomValidationCallback=[System.Net.Http.HttpClientHandler]::DangerousAcceptAnyServerCertificateValidator
$h.CookieContainer=[System.Net.CookieContainer]::new()
$cli=[System.Net.Http.HttpClient]::new($h)
$cli.Timeout=[TimeSpan]::FromMinutes(5)
$script:Csrf=$null
function Sync($r){ $sc=$null; if($r.Headers.TryGetValues('Set-Cookie',[ref]$sc)){ foreach($l in $sc){ if($l -match 'CSRF-Token=([^;,\s]+)'){ $script:Csrf=$Matches[1]; try{$h.CookieContainer.Add([Uri]$base,[System.Net.Cookie]::new('CSRF-Token',$script:Csrf,'/',$Ip))}catch{} } } } }
function Wdp($m,$p,$c=$null){
  $req=[System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::new($m),"$base$p")
  if($script:Csrf){$req.Headers.Add('X-CSRF-Token',$script:Csrf)}
  if($c){$req.Content=$c}
  $r=$cli.SendAsync($req).GetAwaiter().GetResult(); Sync $r; return $r
}
Sync ($cli.GetAsync("$base/api/os/info").GetAwaiter().GetResult())
$dest='E:\Apotheosis\crash\jit-analysis-20261007\mode2-20261008-002236\Harness.exe.1932.dmp'
if(Test-Path $dest){ "already local: $((Get-Item $dest).Length) bytes" }
else {
  $r = Wdp GET "/api/debug/dump/usermode/crashdump?packageFullName=$fNe&fileName=Harness.exe.1932.dmp" $null
  if([int]$r.StatusCode -eq 200){
    [IO.File]::WriteAllBytes($dest, $r.Content.ReadAsByteArrayAsync().Result)
    "downloaded $((Get-Item $dest).Length) bytes"
  } else { "HTTP $([int]$r.StatusCode)" }
}
exit 0
