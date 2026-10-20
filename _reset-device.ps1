$ErrorActionPreference='Continue'
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy','NO_PROXY','no_proxy')) { Remove-Item ("Env:" + $n) -ErrorAction SilentlyContinue }
$Ip='192.168.3.159'; $Ver='0.2.5.15'
$FN="EdgeHTMLReborn.Harness_${Ver}_arm__x0rqga78m2mgc"; $fNe=[uri]::EscapeDataString($FN)
$base="https://${Ip}:443"
Add-Type -AssemblyName System.Net.Http
$h=[System.Net.Http.HttpClientHandler]::new()
$h.ServerCertificateCustomValidationCallback=[System.Net.Http.HttpClientHandler]::DangerousAcceptAnyServerCertificateValidator
$h.CookieContainer=[System.Net.CookieContainer]::new()
$cli=[System.Net.Http.HttpClient]::new($h); $cli.Timeout=[TimeSpan]::FromMinutes(2)
$script:Csrf=$null
function Sync($r){ $sc=$null; if($r.Headers.TryGetValues('Set-Cookie',[ref]$sc)){ foreach($l in $sc){ if($l -match 'CSRF-Token=([^;,\s]+)'){ $script:Csrf=$Matches[1]; try{$h.CookieContainer.Add([Uri]$base,[System.Net.Cookie]::new('CSRF-Token',$script:Csrf,'/',$Ip))}catch{} } } } }
function Wdp($m,$p,$c=$null){
  $req=[System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::new($m),"$base$p")
  if($script:Csrf){$req.Headers.Add('X-CSRF-Token',$script:Csrf)}
  if($c){$req.Content=$c}
  $r=$cli.SendAsync($req).GetAwaiter().GetResult(); Sync $r; return $r
}
function Txt($r){ $r.Content.ReadAsStringAsync().Result }
for($i=0;$i -lt 20;$i++){ if(Test-Connection $Ip -Count 1 -Quiet){ break }; Start-Sleep 3 }
Sync ($cli.GetAsync("$base/api/os/info").GetAwaiter().GetResult())
$files="/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$fNe&path=%5CLocalState&filename="

# 1) write jitdiag.txt = 1  (diagnostics ON by default: JIT thunks, baseline off)
$mp=[System.Net.Http.MultipartFormDataContent]::new()
$bc=[System.Net.Http.ByteArrayContent]::new([Text.Encoding]::ASCII.GetBytes("1`n"))
$bc.Headers.ContentDisposition=[System.Net.Http.Headers.ContentDispositionHeaderValue]::new('form-data')
$bc.Headers.ContentDisposition.Name='"file"'
$bc.Headers.ContentDisposition.FileName='"jitdiag.txt"'
$bc.Headers.ContentType=[System.Net.Http.Headers.MediaTypeHeaderValue]::new('application/octet-stream')
$mp.Add($bc)
$up=Wdp POST ($files+'jitdiag.txt') $mp
"write jitdiag.txt: $([int]$up.StatusCode)"
Start-Sleep 1
"  readback = '" + (Txt (Wdp GET ($files+'jitdiag.txt'))).Trim() + "'"

# 2) delete testurl.txt (only existed for the diagnostic runs)
$del=Wdp DELETE ($files+'testurl.txt')
"delete testurl.txt: $([int]$del.StatusCode)"
$chk=Wdp GET ($files+'testurl.txt')
"  testurl.txt now: HTTP $([int]$chk.StatusCode) (404 = gone)"

# 3) restart the app so it picks up the config on cold start
$pkg=[uri]::EscapeDataString([Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($FN)))
$aid=[uri]::EscapeDataString([Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes("EdgeHTMLReborn.Harness_x0rqga78m2mgc!App")))
$k=Wdp DELETE "/api/taskmanager/app?package=$pkg" $null
"kill: $([int]$k.StatusCode)"
Start-Sleep 4
$l=Wdp POST "/api/taskmanager/app?appid=$aid&package=$pkg" $null
"relaunch: $([int]$l.StatusCode)"
Start-Sleep 12
$st=Txt (Wdp GET ($files+'stage.txt'))
"stage jit-diag: " + ((($st -split "`n") | Select-String -Pattern 'jit-diag|diagnosticMode' | Select-Object -Last 1).Line)
Write-Host "DONE"
exit 0
