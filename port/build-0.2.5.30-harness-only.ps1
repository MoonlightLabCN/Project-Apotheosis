# 只跑 harness 打包(WebCore.lib/驱动已在 11:38/11:41 含 readyState 修复重新产出)。
# 环境变量大小写重复(NO_PROXY/no_proxy 等)会让 MSBuild 起 CL.exe 时抛 ArgumentException,
# 必须先清干净——后台作业会被沙箱注入大写版。
$ErrorActionPreference='Continue'
$log='E:\Apotheosis\port\build-0.2.5.30.log'
function Say($m){ $line="[$(Get-Date -Format 'HH:mm:ss')] $m"; Write-Host $line; Add-Content -Path $log -Value $line }
foreach($n in 'NO_PROXY','no_proxy','HTTP_PROXY','http_proxy','HTTPS_PROXY','https_proxy','ALL_PROXY','all_proxy','FTP_PROXY','ftp_proxy'){
  if(Test-Path "Env:$n"){ Remove-Item "Env:$n" -ErrorAction SilentlyContinue }
}
Say 'harness-only packaging start'
& pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1 *>> $log
$rc=$LASTEXITCODE
Say "build-harness rc=$rc"
$appx='E:\Apotheosis\harness\AppPackages\Harness\Harness_0.2.5.30_ARM_Test\Harness_0.2.5.30_ARM.appx'
if($rc -eq 0 -and (Test-Path $appx)){ Say "DONE: $appx ($([int]((Get-Item $appx).Length/1MB)) MB)" } else { Say 'FAILED' ; exit 1 }
