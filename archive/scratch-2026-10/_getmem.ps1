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
$cli.Timeout=[TimeSpan]::FromMinutes(2)
$script:Csrf=$null
function Sync($r){ $sc=$null; if($r.Headers.TryGetValues('Set-Cookie',[ref]$sc)){ foreach($l in $sc){ if($l -match 'CSRF-Token=([^;,\s]+)'){ $script:Csrf=$Matches[1]; try{$h.CookieContainer.Add([Uri]$base,[System.Net.Cookie]::new('CSRF-Token',$script:Csrf,'/',$Ip))}catch{} } } } }
function Wdp($m,$p,$c=$null){
  $req=[System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::new($m),"$base$p")
  if($script:Csrf){$req.Headers.Add('X-CSRF-Token',$script:Csrf)}
  if($c){$req.Content=$c}
  $r=$cli.SendAsync($req).GetAwaiter().GetResult(); Sync $r; return $r
}
Sync ($cli.GetAsync("$base/api/os/info").GetAwaiter().GetResult())
foreach($nm in @('mem.txt','perf.csv')){
  $r = Wdp GET "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$fNe&path=%5CLocalState&filename=$nm"
  if([int]$r.StatusCode -eq 200){
    $b=$r.Content.ReadAsStringAsync().Result
    [IO.File]::WriteAllText("E:\Apotheosis\crash\now-$nm",$b)
    "pulled $nm ($($b.Length) bytes)"
  } else { "$nm HTTP $([int]$r.StatusCode)" }
}
$m = Get-Content 'E:\Apotheosis\crash\now-mem.txt' -ErrorAction SilentlyContinue
if($m){
  "=== mem.txt last 25 lines ==="
  $m | Select-Object -Last 25
  "=== peak used/limit and lowest avail_virt ==="
  $peak=0; $lowVirt=99999
  foreach($l in $m){
    if($l -match 'mem=(\d+)/(\d+)MB\((\d+)%\)'){ $u=[int]$Matches[1]; $lim=[int]$Matches[2]; if($u -gt $peak){$peak=$u; $peakLim=$lim} }
    if($l -match 'avail_virt=(\d+)MB'){ $v=[int]$Matches[1]; if($v -lt $lowVirt){$lowVirt=$v} }
  }
  "peak mem = $peak MB (limit $peakLim MB) ; lowest avail_virt = $lowVirt MB"
}
exit 0
