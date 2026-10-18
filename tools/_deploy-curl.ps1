# _deploy-curl.ps1 — curl 版非破坏部署(等设备 -> 传 appx+cer -> 轮询落盘 -> 启动)。
# 为什么不用 install-only/Deploy-Robust:两者都走 .NET HttpClient,这台机器上它的 TLS
# 握手间歇性失败(curl/Invoke-WebRequest 每次都通),54MB 上传正是最怕抽签的时候。
param([string]$Ip='192.168.3.159',[string]$Ver='0.2.5.34',[int]$WaitMinutes=240)
$ErrorActionPreference='Continue'
$base="https://${Ip}:443"
$jar="$env:TEMP\wdp-jar.txt"
$out='E:\Apotheosis\port\_mediatest\auto-install-result.txt'
function Say($m){ $line="[$(Get-Date -Format 'HH:mm:ss')] $m"; Write-Host $line; Add-Content -Path $out -Value $line }
"===== curl deploy start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ver=$Ver =====" | Set-Content $out

$dir="E:\Apotheosis\harness\AppPackages\Harness\Harness_${Ver}_ARM_Test"
$appx="$dir\Harness_${Ver}_ARM.appx"
$cer="$dir\Harness_${Ver}_ARM.cer"
if(-not (Test-Path $appx)){ Say "appx not found: $appx"; exit 1 }
$appxName=[System.IO.Path]::GetFileName($appx)
$cerName=[System.IO.Path]::GetFileName($cer)
$FN="EdgeHTMLReborn.Harness_${Ver}_arm__x0rqga78m2mgc"

function DevUp(){ try{ $r=& curl.exe -sk --max-time 8 "$base/api/os/info" 2>$null; return ($LASTEXITCODE -eq 0 -and $r -match 'ComputerName') }catch{ return $false } }
# 稳定在线:隔 8s 两次探活都成才算。设备在后台唤醒时是秒级抖动的,在抖动窗口里起传必断。
function StableUp(){
  if(-not (DevUp)){ return $false }
  Start-Sleep -Seconds 8
  return (DevUp)
}
function FreshToken(){
  for($k=0;$k -lt 3;$k++){
    Remove-Item $jar -ErrorAction SilentlyContinue
    & curl.exe -sk --max-time 15 -c $jar -o NUL "$base/api/os/info" 2>$null
    $m1=Get-Content $jar -ErrorAction SilentlyContinue | Where-Object { $_ -match 'CSRF-Token\s+(\S+)' } | Select-Object -Last 1
    if($m1){ return (($m1 -split '\s+')[6]) }
    Start-Sleep -Seconds 3
  }
  return ''
}

Say "waiting for device (up to ${WaitMinutes} min)..."
$up=$false
for($i=0;$i -lt ($WaitMinutes*2);$i++){
  Start-Sleep -Seconds 30
  if(DevUp){ $up=$true; Say "device ONLINE (waited ~$([int](($i+1)*0.5)) min)"; break }
  if($i % 4 -eq 3){ Say "  ...still offline ($([int](($i+1)*0.5)) min)" }
}
if(-not $up){ Say 'device never came online; giving up'; exit 1 }

# 已装则跳过
$pkgs=(& curl.exe -sk --max-time 25 -b $jar "$base/api/app/packagemanager/packages" 2>$null)|ConvertFrom-Json
if($pkgs.InstalledPackages | Where-Object { $_.PackageFullName -eq $FN }){ Say "already installed: $FN" }
else {
  $uploaded=$false
  for($t=0;$t -lt 8 -and -not $uploaded;$t++){
    # 等一个稳定窗口(秒级抖动的后台唤醒不值得起传);每次探活结果只取一次,避免双重调用误判。
    $up=$false; $waited=0
    while(-not $up -and $waited -lt 1500){
      if($t -eq 0 -and $waited -eq 0){ $up = StableUp }          # 第 0 次:外层刚确认过,直接验稳定性
      else { Start-Sleep -Seconds 15; $waited+=15; $up = StableUp }
    }
    if(-not $up){ Say "no stable device window for >25min before attempt $t; giving up"; exit 1 }
    # 清掉上一轮的响应文件:HTTP 000(curl 根本没拿到响应)时会残留旧 body,日志会撒谎。
    Remove-Item "$env:TEMP\wdp-install.json" -ErrorAction SilentlyContinue
    $tok=FreshToken
    if(-not $tok){ Say "no CSRF token (attempt $t)"; Start-Sleep -Seconds 5; continue }
    $code = & curl.exe -sk -b $jar -c $jar -o "$env:TEMP\wdp-install.json" -w '%{http_code}' `
      --max-time 600 --speed-limit 2048 --speed-time 45 `
      -X POST -H "X-CSRF-Token: $tok" -H "Expect:" `
      -F "$appxName=@$appx;type=application/octet-stream" `
      -F "$cerName=@$cer;type=application/octet-stream" `
      "$base/api/app/packagemanager/package?package=$([uri]::EscapeDataString($appxName))" 2>$null
    $body = (Get-Content "$env:TEMP\wdp-install.json" -Raw -ErrorAction SilentlyContinue)
    if($null -eq $body){ $body = '' }
    Say "upload attempt $t : HTTP $code $($body.Substring(0,[Math]::Min(120,$body.Length)))"
    if($code -eq '200' -or $code -eq '202'){ $uploaded=$true }
    else { Start-Sleep -Seconds 5 }
  }
  if(-not $uploaded){ Say 'upload failed after retries; giving up'; exit 1 }
  # 轮询落盘(54MB 解包+注册要一会)
  $done=$false
  for($i=0;$i -lt 90;$i++){
    Start-Sleep -Seconds 5
    if(-not (DevUp)){ continue }
    $pkgs=(& curl.exe -sk --max-time 25 -b $jar "$base/api/app/packagemanager/packages" 2>$null)|ConvertFrom-Json
    if($pkgs.InstalledPackages | Where-Object { $_.PackageFullName -eq $FN }){ $done=$true; break }
  }
  if(-not $done){ Say 'upload accepted but package never appeared; giving up'; exit 1 }
  Say "installed: $FN"
}
# 启动
$tok=FreshToken
$parts=$FN -split '_'
$aumid="$($parts[0])_$($parts[-1])!App"
$pfnB64=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($FN))
$aumidB64=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($aumid))
$lc = & curl.exe -sk -b $jar -c $jar -o NUL -w '%{http_code}' --max-time 60 -X POST -H "X-CSRF-Token: $tok" -H 'Content-Length: 0' `
  "$base/api/taskmanager/app?appid=$([uri]::EscapeDataString($aumidB64))&package=$([uri]::EscapeDataString($pfnB64))" 2>$null
Say "launch: $lc"
Say 'READY - user: open the app, type http://192.168.3.108:8099/values.html in the address bar, wait ~15s, then say done'
