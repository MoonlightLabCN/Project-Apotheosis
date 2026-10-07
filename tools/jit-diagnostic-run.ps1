param([ValidateSet(0,1,2)][int]$Mode, [string]$Ip='192.168.3.159', [string]$Ver='0.2.5.13', [int]$WaitSec=120, [string]$TestUrl='')
$ErrorActionPreference='Stop'
$base="https://${Ip}:443"
$handler=[System.Net.Http.HttpClientHandler]::new()
$handler.UseProxy=$false
$handler.ServerCertificateCustomValidationCallback=[System.Net.Http.HttpClientHandler]::DangerousAcceptAnyServerCertificateValidator
$handler.CookieContainer=[System.Net.CookieContainer]::new()
$client=[System.Net.Http.HttpClient]::new($handler)
$client.Timeout=[TimeSpan]::FromSeconds(30)
$script:csrf=$null
function Wdp($method,$path,$content=$null) {
 $req=[System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::new($method),"$base$path")
 if($script:csrf){$req.Headers.Add('X-CSRF-Token',$script:csrf)}
 if($content){$req.Content=$content}
 $res=$client.SendAsync($req).GetAwaiter().GetResult()
 $cookies=$null
 if($res.Headers.TryGetValues('Set-Cookie',[ref]$cookies)){foreach($cookie in $cookies){if($cookie -match 'CSRF-Token=([^;,\s]+)'){$script:csrf=$Matches[1]}}}
 return $res
}
function Body($response){$response.Content.ReadAsStringAsync().GetAwaiter().GetResult()}
function Stamp($body){$matches=[regex]::Matches($body,'==== crash (\S+ \S+)');if($matches.Count){$matches[$matches.Count-1].Groups[1].Value}else{'none'}}
$info=Wdp GET '/api/os/info';$info.EnsureSuccessStatusCode()|Out-Null
$packages=Body (Wdp GET '/api/app/packagemanager/packages')|ConvertFrom-Json
$target=$packages.InstalledPackages|Where-Object {$_.PackageFullName -like "EdgeHTMLReborn.Harness_${Ver}_arm__*"}|Select-Object -First 1
if(-not $target){throw "Version $Ver not installed"}
$fullname=$target.PackageFullName
$fn=[uri]::EscapeDataString($fullname)
$pkg=[uri]::EscapeDataString([Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($fullname)))
$praid=$target.PackageRelativeId
if(-not $praid){$praid="$($fullname.Split('_')[0])_$($fullname.Split('__')[-1])!App"}
$aid=[uri]::EscapeDataString([Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($praid)))
$files="/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$fn&path=%5CLocalState&filename="
$root=Split-Path -Parent $PSScriptRoot
$out=Join-Path $root "crash\jit-analysis-20261007\mode$Mode-$(Get-Date -Format yyyyMMdd-HHmmss)"
New-Item -ItemType Directory -Path $out -Force|Out-Null
Write-Host "OUT=$out"
foreach($name in @('crash.txt','stage.txt','settings.ini','jitdiag.txt','testurl.txt','autodiag.txt')) {
 $response=Wdp GET ($files+$name)
 if([int]$response.StatusCode -eq 200){[IO.File]::WriteAllText((Join-Path $out "pre-$name"),(Body $response))}
}
$prePath=Join-Path $out 'pre-crash.txt'
$baseline=if(Test-Path $prePath){Stamp ([IO.File]::ReadAllText($prePath))}else{'none'}
$kill=Wdp DELETE "/api/taskmanager/app?package=$pkg"
Write-Host "kill=$([int]$kill.StatusCode) baseline=$baseline"
Start-Sleep -Seconds 3
$multipart=[System.Net.Http.MultipartFormDataContent]::new()
$content=[System.Net.Http.ByteArrayContent]::new([Text.Encoding]::ASCII.GetBytes("$Mode`n"))
$content.Headers.ContentDisposition=[System.Net.Http.Headers.ContentDispositionHeaderValue]::new('form-data')
$content.Headers.ContentDisposition.Name='"file"'
$content.Headers.ContentDisposition.FileName='"jitdiag.txt"'
$content.Headers.ContentType=[System.Net.Http.Headers.MediaTypeHeaderValue]::new('application/octet-stream')
$multipart.Add($content)
$upload=Wdp POST ($files+'jitdiag.txt') $multipart
$upload.EnsureSuccessStatusCode()|Out-Null
$readback=Body (Wdp GET ($files+'jitdiag.txt'))
if($readback.Trim() -ne "$Mode"){throw "Mode readback mismatch: $readback"}
if($TestUrl) {
 $form=[System.Net.Http.MultipartFormDataContent]::new()
 $data=[System.Net.Http.ByteArrayContent]::new([Text.Encoding]::UTF8.GetBytes("$TestUrl`n"))
 $data.Headers.ContentDisposition=[System.Net.Http.Headers.ContentDispositionHeaderValue]::new('form-data')
 $data.Headers.ContentDisposition.Name='"file"'
 $data.Headers.ContentDisposition.FileName='"testurl.txt"'
 $data.Headers.ContentType=[System.Net.Http.Headers.MediaTypeHeaderValue]::new('application/octet-stream')
 $form.Add($data)
 $response=Wdp POST ($files+'testurl.txt') $form
 $response.EnsureSuccessStatusCode()|Out-Null
 if((Body (Wdp GET ($files+'testurl.txt'))).Trim() -ne $TestUrl){throw 'test URL readback failed'}
}
$control=Wdp POST "/api/debug/dump/usermode/crashcontrol?packageFullName=$fn"
$launch=Wdp POST "/api/taskmanager/app?appid=$aid&package=$pkg"
$launch.EnsureSuccessStatusCode()|Out-Null
Write-Host "mode=$Mode launch=$([int]$launch.StatusCode)"
for($s=0;$s -lt $WaitSec;$s+=10) {
 Start-Sleep -Seconds 10
 $response=Wdp GET ($files+'crash.txt')
 $body=Body $response
 $stage=Body (Wdp GET ($files+'stage.txt'))
 $diag=[regex]::Matches($stage,'jit-diag[^\r\n]+')
 if($diag.Count){Write-Host $diag[$diag.Count-1].Value}
 $stamp=Stamp $body
 Write-Host "t=$($s+10)s last=$stamp"
 if([int]$response.StatusCode -eq 200 -and $stamp -ne $baseline){Write-Host 'NEW_CRASH';break}
}
foreach($name in @('crash.txt','stage.txt','browse-log.txt','settings.ini','jitresult.txt','jitdiag.txt')) {
 $response=Wdp GET ($files+$name)
 if([int]$response.StatusCode -eq 200){$body=Body $response;[IO.File]::WriteAllText((Join-Path $out $name),$body);Write-Host "pulled $name ($($body.Length) bytes)"}
}
[IO.File]::WriteAllText((Join-Path $out 'dumps.json'),(Body (Wdp GET '/api/debug/dump/usermode/dumps')))
if($Mode -gt 0) {
 $staged=[IO.File]::ReadAllText((Join-Path $out 'stage.txt'))
 if($staged -notmatch "useJIT=1[^\r\n]*diagnosticMode=$Mode baseline=$($Mode-1)"){throw "Diagnostic mode NOT proven active; exclude this run: $out"}
}
Write-Host "DONE=$out"
