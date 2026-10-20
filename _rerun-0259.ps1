$ErrorActionPreference='Continue'
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy','NO_PROXY','no_proxy')) { Remove-Item ("Env:" + $n) -ErrorAction SilentlyContinue }
[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"
$Ip='192.168.3.159'; $Ver='0.2.5.9'; $Pub='x0rqga78m2mgc'
$base="https://${Ip}:443"
$FN="EdgeHTMLReborn.Harness_${Ver}_arm__${Pub}"
$AUMID="EdgeHTMLReborn.Harness_${Pub}!App"
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
Write-Host "device ok" -ForegroundColor Green

$cc=Wdp POST "/api/debug/dump/usermode/crashcontrol?packageFullName=$FNe" $null
Write-Host "crashcontrol: $([int]$cc.StatusCode)"

$pkg=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($FN))
$aid=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($AUMID))
# baseline dump set
$before=@()
$d0=Txt (Wdp GET '/api/debug/dump/usermode/dumps' $null)
[IO.File]::WriteAllText("E:\Apotheosis\crash\dumps-before.json",$d0)
foreach($m in [regex]::Matches($d0,'"FileName" : "([^"]+)"')){ $before += $m.Groups[1].Value }
Write-Host "dumps before: $($before -join ', ')"

$d=Wdp DELETE "/api/taskmanager/app?package=$([uri]::EscapeDataString($pkg))" $null
Write-Host "kill: $([int]$d.StatusCode)"
Start-Sleep 4
$l=Wdp POST "/api/taskmanager/app?appid=$([uri]::EscapeDataString($aid))&package=$([uri]::EscapeDataString($pkg))" $null
Write-Host "launch: $([int]$l.StatusCode)" -ForegroundColor Green

$found=$null
for($s=0;$s -lt 240;$s+=15){
  Start-Sleep 15
  try {
    $d=Txt (Wdp GET '/api/debug/dump/usermode/dumps' $null)
    [IO.File]::WriteAllText("E:\Apotheosis\crash\dumps-poll.json",$d)
    foreach($m in [regex]::Matches($d,'"FileName" : "([^"]+)"')){
      $nm=$m.Groups[1].Value
      if($nm -notlike "*Old*" -and ($before -notcontains $nm) -and $nm -like "Harness.exe*"){ $found=$nm; Write-Host "  t=$($s+15)s NEW DUMP: $nm" -ForegroundColor Yellow }
    }
    if($found){ break }
    $g=Txt (Wdp GET "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$FNe&path=%5CLocalState&filename=crash.txt" $null)
    $last=([regex]::Matches($g,'==== crash (\S+ \S+)') | Select-Object -Last 1).Groups[1].Value
    Write-Host "  t=$($s+15)s last-crash=$last"
  } catch { Write-Host "  t=$($s+15)s poll failed: $_" }
}
if($found){
  $r=Wdp GET "/api/debug/dump/usermode/crashdump?packageFullName=$FNe&fileName=$found" $null
  if([int]$r.StatusCode -eq 200){ $dest="E:\Apotheosis\crash\$found"; [IO.File]::WriteAllBytes($dest,$r.Content.ReadAsByteArrayAsync().Result); "downloaded $found -> $dest" }
  else { "$found : HTTP $([int]$r.StatusCode)" }
} else { "no new dump found in window" }
Write-Host "DONE"
