param(
    [string]$Ip='192.168.3.159',
    [string]$Ver='0.2.5.9',
    [string]$Pub='x0rqga78m2mgc',
    [string]$Jit='0'
)
$ErrorActionPreference='Continue'
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy','NO_PROXY','no_proxy')) { Remove-Item ("Env:" + $n) -ErrorAction SilentlyContinue }
$base="https://${Ip}:443"
$FN="EdgeHTMLReborn.Harness_${Ver}_arm__${Pub}"
$FNe=[uri]::EscapeDataString($FN)
Add-Type -AssemblyName System.Net.Http
$h=[System.Net.Http.HttpClientHandler]::new()
$h.ServerCertificateCustomValidationCallback=[System.Net.Http.HttpClientHandler]::DangerousAcceptAnyServerCertificateValidator
$h.CookieContainer=[System.Net.CookieContainer]::new()
$cli=[System.Net.Http.HttpClient]::new($h); $cli.Timeout=[TimeSpan]::FromMinutes(3)
$script:Csrf=$null
function Sync-Csrf($resp){ $sc=$null; if($resp.Headers.TryGetValues('Set-Cookie',[ref]$sc)){ foreach($l in $sc){ if($l -match 'CSRF-Token=([^;,\s]+)'){ $script:Csrf=$Matches[1]; try{$h.CookieContainer.Add([Uri]$base,[System.Net.Cookie]::new('CSRF-Token',$script:Csrf,'/',$Ip))}catch{} } } } }
function Wdp([string]$m,[string]$p,[System.Net.Http.HttpContent]$c){
  for($t=0;$t -lt 8;$t++){
    try {
      $req=[System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::new($m),"$base$p")
      if($script:Csrf){$req.Headers.Add('X-CSRF-Token',$script:Csrf)}
      if($c){$req.Content=$c}
      $r=$cli.SendAsync($req).GetAwaiter().GetResult(); Sync-Csrf $r; return $r
    } catch { if($t -eq 7){throw}; Start-Sleep 3 }
  }
}
function Txt($r){ return $r.Content.ReadAsStringAsync().Result }
for($i=0;$i -lt 20;$i++){ if(Test-Connection $Ip -Count 1 -Quiet){ break }; Start-Sleep 3 }
Sync-Csrf ($cli.GetAsync("$base/api/os/info").GetAwaiter().GetResult())

# read current settings.ini
$cur = Txt (Wdp GET "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$FNe&path=%5CLocalState&filename=settings.ini" $null)
Write-Host "current settings.ini:" -ForegroundColor Cyan
Write-Host $cur

# rewrite jit= line
$lines = $cur -split "`r?`n" | Where-Object { $_ -ne "" }
$out = New-Object System.Collections.Generic.List[string]
$found=$false
foreach($l in $lines){
  if($l -match '^jit='){ $out.Add("jit=$Jit"); $found=$true }
  else { $out.Add($l) }
}
if(-not $found){ $out.Add("jit=$Jit") }
$body = ($out -join "`r`n") + "`r`n"
Write-Host "new settings.ini:" -ForegroundColor Cyan
Write-Host $body

# upload via WDP multipart POST
$mp=[System.Net.Http.MultipartFormDataContent]::new()
$bc=[System.Net.Http.ByteArrayContent]::new([Text.Encoding]::UTF8.GetBytes($body))
$bc.Headers.ContentDisposition=[System.Net.Http.Headers.ContentDispositionHeaderValue]::new('form-data')
$bc.Headers.ContentDisposition.Name='"file"'
$bc.Headers.ContentDisposition.FileName='"settings.ini"'
$bc.Headers.ContentType=[System.Net.Http.Headers.MediaTypeHeaderValue]::new('application/octet-stream')
$mp.Add($bc)
$up=Wdp POST "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$FNe&path=%5CLocalState&filename=settings.ini" $mp
Write-Host "upload: $([int]$up.StatusCode) $((Txt $up).Substring(0,[Math]::Min(200,(Txt $up).Length)))"
Start-Sleep 2
$verify = Txt (Wdp GET "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$FNe&path=%5CLocalState&filename=settings.ini" $null)
if($verify -match 'jit=0'){ Write-Host "VERIFIED jit=0 on device" -ForegroundColor Green } else { Write-Host "verify failed:"; Write-Host $verify }
Write-Host "DONE"
