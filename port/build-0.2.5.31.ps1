# 0.2.5.31:harness-only 构建(MediaAudio.cpp 音频诊断)。引擎/驱动未动,直接复用 0.2.5.30 的产物。
$ErrorActionPreference='Continue'
$log='E:\Apotheosis\port\build-0.2.5.31.log'
function Say($m){ $line="[$(Get-Date -Format 'HH:mm:ss')] $m"; Write-Host $line; Add-Content -Path $log -Value $line }
"===== 0.2.5.31 harness-only start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') =====" | Set-Content $log
# MSBuild 起 CL.exe 时环境里 *_PROXY 大小写重复会炸(后台作业会被注入大写版),全清。
foreach($n in 'NO_PROXY','no_proxy','HTTP_PROXY','http_proxy','HTTPS_PROXY','https_proxy','ALL_PROXY','all_proxy','FTP_PROXY','ftp_proxy'){
  if(Test-Path "Env:$n"){ Remove-Item "Env:$n" -ErrorAction SilentlyContinue }
}
Say 'build-harness (harness-only; engine/driver unchanged)'
& pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1 *>> $log
$rc=$LASTEXITCODE
Say "build-harness rc=$rc"
$appx='E:\Apotheosis\harness\AppPackages\Harness\Harness_0.2.5.31_ARM_Test\Harness_0.2.5.31_ARM.appx'
if($rc -eq 0 -and (Test-Path $appx)){ Say "DONE: $appx ($([int]((Get-Item $appx).Length/1MB)) MB)" } else { Say 'FAILED'; exit 1 }
