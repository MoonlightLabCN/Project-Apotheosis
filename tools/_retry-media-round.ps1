#Requires -Version 7.0
# 用 Invoke-WebRequest(该机上 .NET HttpClient 的 TLS 握手间歇失败,WdpR 稳定)推 testurl.txt 到
# 设备 LocalState,重启 app,等待,然后拉取媒体测试证据。
param([string]$Ip='192.168.3.159',[string]$Url,[int]$WaitSeconds=40)
$ErrorActionPreference='Stop'
$base="https://${Ip}:443"
function WdpR($m,$p,$body,$ctype){
  $hdr=@{}
  if($script:Csrf){ $hdr['X-CSRF-Token']=$script:Csrf }
  if($ctype){ $hdr['Content-Type']=$ctype }
  $par=@{
    Uri="$base$p"; Method=$m; SkipCertificateCheck=$true; NoProxy=$true
    TimeoutSec=90; UseBasicParsing=$true; WebSession=$script:Ws
  }
  if($hdr.Count){ $par['Headers']=$hdr }
  if($body -ne $null){ $par['Body']=$body }
  $r=Invoke-WebRequest @par
  $c=$script:Ws.Cookies.GetCookies([Uri]$base)['CSRF-Token']
  if($c){ $script:Csrf=$c.Value }
  return $r
}
$script:Ws=New-Object Microsoft.PowerShell.Commands.WebRequestSession
$script:Csrf=$null
# 发现包
$pkgs=(WdpR GET '/api/app/packagemanager/packages' $null $null).Content|ConvertFrom-Json
$pkg=$pkgs.InstalledPackages|Where-Object{$_.PackageFullName -match 'Harness'}|Select-Object -First 1
if(-not $pkg){ Write-Host 'no Harness package'; exit 1 }
$pfn=$pkg.PackageFullName
$parts=$pfn -split '_'
$family=if($parts.Count -ge 5){"$($parts[0])_$($parts[-1])"}else{$pkg.PackageFamilyName}
$aumid="$family!App"
$pfnB64=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($pfn))
$aumidB64=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($aumid))
$pfnE=[uri]::EscapeDataString($pfn)
Write-Host "installed: $pfn"
Write-Host "url     : $Url"

# 先终止 app,再推 testurl.txt(带重试)
function Stop-App(){ try{ $null=WdpR DELETE "/api/taskmanager/app?package=$([uri]::EscapeDataString($pfnB64))" $null $null }catch{} }
Stop-App; Start-Sleep -Seconds 2
function Push-Url($text){
  $b=[System.Guid]::NewGuid().ToString('N')
  $body="--$b`r`nContent-Disposition: form-data; name=`"testurl.txt`"; filename=`"testurl.txt`"`r`nContent-Type: application/octet-stream`r`n`r`n$text`r`n--$b--`r`n"
  $bytes=[System.Text.Encoding]::UTF8.GetBytes($body)
  return WdpR POST "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState" $bytes "multipart/form-data; boundary=$b"
}
$ok=$false
for($t=0;$t -lt 4 -and -not $ok;$t++){
  try{
    if($t -gt 0){
      Write-Host "  启动一次确保 LocalState 存在..."
      $null=WdpR POST "/api/taskmanager/app?appid=$([uri]::EscapeDataString($aumidB64))&package=$([uri]::EscapeDataString($pfnB64))" $null $null
      Start-Sleep -Seconds 8; Stop-App; Start-Sleep -Seconds 2
    }
    $r=Push-Url "$Url`n"
    Write-Host "push($t): $([int]$r.StatusCode)"
    if([int]$r.StatusCode -lt 400){ $ok=$true }
  }catch{ Write-Host "  push err($t): $($_.Exception.Message)" }
}
if(-not $ok){ Write-Host 'push 失败'; exit 1 }
# 启动并等待
$lr=WdpR POST "/api/taskmanager/app?appid=$([uri]::EscapeDataString($aumidB64))&package=$([uri]::EscapeDataString($pfnB64))" $null $null
Write-Host "launch: $([int]$lr.StatusCode); waiting ${WaitSeconds}s"
Start-Sleep -Seconds $WaitSeconds
# 拉证据
function Get-F($name){
  try{ return (WdpR GET "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState&filename=$([uri]::EscapeDataString($name))" $null $null).Content }catch{ return "ERR: $($_.Exception.Message)" }
}
Write-Host "`n===== testurl.txt readback ====="
Write-Host (Get-F 'testurl.txt')
$stage=Get-F 'stage.txt'
Write-Host "`n===== stage.txt: media line(s) ====="
$m=($stage -split "`n")|Where-Object{$_ -match '^\s*media '}
if($m){$m|Select-Object -Last 3|ForEach-Object{$_.TrimEnd()}}else{Write-Host '(no media line yet)'}
Write-Host "`n===== stage.txt: media-state ====="
($stage -split "`n")|Where-Object{$_ -match 'media-state'}|Select-Object -Last 1|ForEach-Object{$_.TrimEnd()}
Write-Host "`n===== stage.txt: timeline ====="
($stage -split "`n")|Where-Object{$_ -match '^timeline '}|Select-Object -Last 1|ForEach-Object{$_.TrimEnd()}
Write-Host "`n===== crash.txt tail ====="
$c=Get-F 'crash.txt'
$cl=($c -split "`n")|Where-Object{$_.Trim()}
Write-Host "total lines: $($cl.Count)"
$cl|Select-Object -Last 6|ForEach-Object{$_.TrimEnd()}

