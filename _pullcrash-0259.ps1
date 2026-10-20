$ErrorActionPreference='Continue'
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy','NO_PROXY','no_proxy')) { Remove-Item ("Env:" + $n) -ErrorAction SilentlyContinue }
$Ip='192.168.3.159'; $Ver='0.2.5.9'; $Pub='x0rqga78m2mgc'
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
foreach($f in @('crash.txt','stage.txt')){
  $g=Txt (Wdp GET "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$FNe&path=%5CLocalState&filename=$f" $null)
  [IO.File]::WriteAllText("E:\Apotheosis\crash\verify-$Ver-$f",$g)
  "pulled $f ($($g.Length)B)"
}
# print the tail records
$g = [IO.File]::ReadAllText("E:\Apotheosis\crash\verify-$Ver-crash.txt")
$parts = $g -split "(?===== crash)"
"records: $($parts.Count)"
$parts | Select-Object -Last 8 | ForEach-Object { ($_ -split "`n" | Select-Object -First 4) -join "`n"; "" }
Write-Host "DONE"
