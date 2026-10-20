# 0.2.5.34:harness-only 构建(920/930 适配 —— 设备身份门)。引擎/驱动未动。
$ErrorActionPreference='Continue'
$log='E:\Apotheosis\port\build-0.2.5.34.log'
function Say($m){ $line="[$(Get-Date -Format 'HH:mm:ss')] $m"; Write-Host $line; Add-Content -Path $log -Value $line }
"===== 0.2.5.34 harness-only start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') =====" | Set-Content $log
foreach($n in 'NO_PROXY','no_proxy','HTTP_PROXY','http_proxy','HTTPS_PROXY','https_proxy','ALL_PROXY','all_proxy','FTP_PROXY','ftp_proxy'){
  if(Test-Path "Env:$n"){ Remove-Item "Env:$n" -ErrorAction SilentlyContinue }
}
Say 'build-harness (harness-only; 920/930 device gate)'
& pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1 *>> $log
$rc=$LASTEXITCODE
Say "build-harness rc=$rc"
$appx='E:\Apotheosis\harness\AppPackages\Harness\Harness_0.2.5.34_ARM_Test\Harness_0.2.5.34_ARM.appx'
if($rc -eq 0 -and (Test-Path $appx)){ Say "DONE: $appx ($([int]((Get-Item $appx).Length/1MB)) MB)" } else { Say 'FAILED'; exit 1 }
