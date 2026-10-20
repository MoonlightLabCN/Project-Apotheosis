# _verify-0259.ps1 — post-fix verification on device: baseline crash.txt, cold launch, poll, compare.
param(
    [string]$Ip='192.168.3.159',
    [string]$Ver='0.2.5.9',
    [string]$Pub='x0rqga78m2mgc',
    [int]$WaitSec=150,
    [switch]$SkipPull
)
[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"
Get-ChildItem Env: | Where-Object { $_.Name -match "(?i)proxy" } | ForEach-Object { Remove-Item ("Env:" + $_.Name) }
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Net.Http
$base="https://${Ip}:443"
$FN="EdgeHTMLReborn.Harness_${Ver}_arm__${Pub}"
$AUMID="EdgeHTMLReborn.Harness_${Pub}!App"
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
function Last-CrashStamp($body){
  $ms=[regex]::Matches($body,'==== crash (\S+ \S+)')
  if($ms.Count -eq 0){ return '(none)' }
  return ($ms | Select-Object -Last 1).Groups[1].Value
}
$FNe=[uri]::EscapeDataString($FN)
$pkg=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($FN))
$aid=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($AUMID))
$out="E:\Apotheosis\crash"; New-Item -ItemType Directory -Force $out | Out-Null

for($i=0;$i -lt 20;$i++){ if(Test-Connection $Ip -Count 1 -Quiet){ break }; Start-Sleep 3 }
Sync-Csrf ($cli.GetAsync("$base/api/os/info").GetAwaiter().GetResult())
Write-Host ("device: " + (Txt (Wdp GET '/api/os/info' $null))) -ForegroundColor Green

# baseline crash.txt (pre-launch)
$baseline='(unreadable)'
try {
  $g=Txt (Wdp GET "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$FNe&path=%5CLocalState&filename=crash.txt" $null)
  [IO.File]::WriteAllText("$out\verify-$Ver-pre-crash.txt",$g)
  $baseline=Last-CrashStamp $g
} catch { Write-Host "pre crash.txt: unreadable (treated as no prior crash)" }
Write-Host "baseline last-crash=$baseline" -ForegroundColor Cyan

# also record whether the app is even installed
$pk=Txt (Wdp GET '/api/app/packagemanager/packages' $null) | ConvertFrom-Json
$inst=$pk.InstalledPackages | Where-Object { $_.PackageFullName -eq $FN }
if(-not $inst){ Write-Host "NOT INSTALLED: $FN (deploy first)" -ForegroundColor Red; exit 2 }

$cc=Wdp POST "/api/debug/dump/usermode/crashcontrol?packageFullName=$FNe" $null
Write-Host "crashcontrol enable: $([int]$cc.StatusCode)"
$d=Wdp DELETE "/api/taskmanager/app?package=$([uri]::EscapeDataString($pkg))" $null
Write-Host "kill: $([int]$d.StatusCode)"
Start-Sleep 4
$l=Wdp POST "/api/taskmanager/app?appid=$([uri]::EscapeDataString($aid))&package=$([uri]::EscapeDataString($pkg))" $null
Write-Host "launch: $([int]$l.StatusCode)" -ForegroundColor Green

$newCrash=$false
for($s=0;$s -lt $WaitSec;$s+=10){
  Start-Sleep 10
  try {
    $g=Txt (Wdp GET "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$FNe&path=%5CLocalState&filename=crash.txt" $null)
    $last=Last-CrashStamp $g
    if($last -ne $baseline){ Write-Host "  t=$($s+10)s *** NEW CRASH RECORD: $last" -ForegroundColor Red; $newCrash=$true; break }
    Write-Host "  t=$($s+10)s ok (last=$last)"
  } catch { Write-Host "  t=$($s+10)s crash.txt read failed" }
}

# post-run pulls
foreach($f in @('crash.txt','stage.txt','browse-log.txt','settings.ini','jitresult.txt')){
  try{
    $g=Wdp GET "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$FNe&path=%5CLocalState&filename=$f" $null
    if([int]$g.StatusCode -eq 200){ [IO.File]::WriteAllText("$out\verify-$Ver-$f",(Txt $g)); Write-Host "pulled $f ($((Txt $g).Length)B)" }
    else { Write-Host "$f : HTTP $([int]$g.StatusCode)" }
  } catch { Write-Host "$f : ERR $_" }
}
$d=Txt (Wdp GET '/api/debug/dump/usermode/dumps' $null)
[IO.File]::WriteAllText("$out\verify-$Ver-dumps.json",$d)
$m=[regex]::Matches($d,'"FileName" : "(Harness\.exe\.\d+\.dmp)"')
Write-Host "Harness dumps present: $($m.Count)"
if($newCrash){ Write-Host "RESULT=CRASH_STILL_PRESENT" -ForegroundColor Red } else { Write-Host "RESULT=NO_NEW_CRASH" -ForegroundColor Green }
Write-Host "DONE"
