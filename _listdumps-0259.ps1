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
Sync-Csrf ($cli.GetAsync("$base/api/os/info").GetAwaiter().GetResult())
$d=Txt (Wdp GET '/api/debug/dump/usermode/dumps' $null)
[IO.File]::WriteAllText("E:\Apotheosis\crash\dumps-$Ver.json",$d)
$j=$d | ConvertFrom-Json
$j.CrashDumps | Sort-Object FileDate -Descending | Select-Object -First 8 | ForEach-Object { "$($_.FileDate)  $($_.FileName)  $($_.PackageFullName)" }
foreach($dd in $j.CrashDumps){
  if($dd.PackageFullName -like "*$Ver*"){
    $dest="E:\Apotheosis\crash\$($dd.FileName)"
    if(Test-Path $dest){ "$($dd.FileName) already local"; continue }
    $r=Wdp GET "/api/debug/dump/usermode/crashdump?packageFullName=$FNe&fileName=$($dd.FileName)" $null
    if([int]$r.StatusCode -eq 200){ [IO.File]::WriteAllBytes($dest,$r.Content.ReadAsByteArrayAsync().Result); "downloaded $($dd.FileName)" }
    else { "$($dd.FileName) : HTTP $([int]$r.StatusCode)" }
  }
}
Write-Host "DONE"
