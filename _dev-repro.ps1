# _dev-repro.ps1 — reproduce the 0.2.5.8 interactive crash on-device and capture a minidump.
# Steps: enable crashcontrol -> kill app -> launch -> wait -> pull crash.txt + dumps.
param(
    [string]$Ip='192.168.3.159',
    [string]$Ver='0.2.5.8',
    [string]$Pub='x0rqga78m2mgc',
    [int]$WaitSec=150,
    [switch]$SkipEnable
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
  for($t=0;$t -lt 6;$t++){
    try {
      $req=[System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::new($m),"$base$p")
      if($script:Csrf){$req.Headers.Add('X-CSRF-Token',$script:Csrf)}
      if($c){$req.Content=$c}
      $r=$cli.SendAsync($req).GetAwaiter().GetResult(); Sync-Csrf $r; return $r
    } catch { if($t -eq 5){throw}; Start-Sleep 3 }
  }
}
function Txt($r){ return $r.Content.ReadAsStringAsync().Result }
$FNe=[uri]::EscapeDataString($FN)
$out="E:\Apotheosis\crash"; New-Item -ItemType Directory -Force $out | Out-Null

for($i=0;$i -lt 20;$i++){ if(Test-Connection $Ip -Count 1 -Quiet){ break }; Start-Sleep 3 }
Sync-Csrf ($cli.GetAsync("$base/api/os/info").GetAwaiter().GetResult())
Write-Host ("设备: " + (Txt (Wdp GET '/api/os/info' $null))) -ForegroundColor Green

if(-not $SkipEnable){
  $cc=Wdp POST "/api/debug/dump/usermode/crashcontrol?packageFullName=$FNe" $null
  Write-Host "crashcontrol enable: $([int]$cc.StatusCode) $((Txt $cc).Substring(0,[Math]::Min(120,(Txt $cc).Length)))"
} else { Write-Host "crashcontrol: skipped (-SkipEnable)" }

$pkg=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($FN))
$aid=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($AUMID))
$d=Wdp DELETE "/api/taskmanager/app?package=$([uri]::EscapeDataString($pkg))" $null
Write-Host "kill: $([int]$d.StatusCode)"
Start-Sleep 4
$l=Wdp POST "/api/taskmanager/app?appid=$([uri]::EscapeDataString($aid))&package=$([uri]::EscapeDataString($pkg))" $null
Write-Host "launch: $([int]$l.StatusCode)" -ForegroundColor Green

for($s=0;$s -lt $WaitSec;$s+=10){
  Start-Sleep 10
  try {
    $g=Txt (Wdp GET "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$FNe&path=%5CLocalState&filename=crash.txt" $null)
    $last=($g -split "(?===== crash)") | Where-Object { $_ -match "==== crash" } | Select-Object -Last 1
    $stamp = if($last -match "==== crash (\S+ \S+)"){$Matches[1]}else{"(none)"}
    Write-Host ("  t={0}s last-crash={1}" -f ($s+10),$stamp)
  } catch { Write-Host "  t=$($s+10)s (crash.txt read failed)" }
}

# pull crash.txt + stage.txt
foreach($f in @('crash.txt','stage.txt','browse-log.txt')){
  try{
    $g=Wdp GET "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$FNe&path=%5CLocalState&filename=$f" $null
    if([int]$g.StatusCode -eq 200){ [IO.File]::WriteAllText("$out\repro-$Ver-$f",(Txt $g)); Write-Host "pulled $f ($((Txt $g).Length)B) -> $out\repro-$Ver-$f" }
    else { Write-Host "$f : HTTP $([int]$g.StatusCode)" }
  } catch { Write-Host "$f : ERR $_" }
}

# dump list + download new Harness dumps
$d=Txt (Wdp GET '/api/debug/dump/usermode/dumps' $null)
[IO.File]::WriteAllText("$out\repro-$Ver-dumps.json",$d)
$m=[regex]::Matches($d,'"FileName" : "(Harness\.exe\.\d+\.dmp)"')
Write-Host "dumps matching Harness: $($m.Count)"
foreach($mm in $m){
  $name=$mm.Groups[1].Value
  $dest="$out\$name"
  if(-not (Test-Path $dest)){
    try{
      $r=Wdp GET "/api/debug/dump/usermode/crashdump?packageFullName=$FNe&fileName=$name" $null
      if([int]$r.StatusCode -eq 200){ [IO.File]::WriteAllBytes($dest,$r.Content.ReadAsByteArrayAsync().Result); Write-Host "downloaded $name" }
      else { Write-Host "$name : HTTP $([int]$r.StatusCode)" }
    } catch { Write-Host "$name : ERR $_" }
  } else { Write-Host "$name already local" }
}
Write-Host "DONE"
