# 0.2.5.38:harness-only 构建(音频:显式绑定 PrimaryRenderDevice + 端点详情 + 修延迟停止)。
$ErrorActionPreference='Continue'
$log='E:\Apotheosis\port\build-0.2.5.38.log'
function Say($m){ $line="[$(Get-Date -Format 'HH:mm:ss')] $m"; Write-Host $line; Add-Content -Path $log -Value $line }
"===== 0.2.5.38 harness-only start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') =====" | Set-Content $log
foreach($n in 'NO_PROXY','no_proxy','HTTP_PROXY','http_proxy','HTTPS_PROXY','https_proxy','ALL_PROXY','all_proxy','FTP_PROXY','ftp_proxy'){
  if(Test-Path "Env:$n"){ Remove-Item "Env:$n" -ErrorAction SilentlyContinue }
}
Say 'build-harness (audio: explicit PrimaryRenderDevice binding)'
& pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1 *>> $log
$rc=$LASTEXITCODE
Say "build-harness rc=$rc"
$appx='E:\Apotheosis\harness\AppPackages\Harness\Harness_0.2.5.38_ARM_Test\Harness_0.2.5.38_ARM.appx'
if($rc -eq 0 -and (Test-Path $appx)){ Say "DONE: $appx ($([int]((Get-Item $appx).Length/1MB)) MB)" } else { Say 'FAILED'; exit 1 }
