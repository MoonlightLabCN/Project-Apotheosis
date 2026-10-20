# 0.2.5.35 全链构建:WebCore(-j2)-> GPU 驱动重链 -> appx。
# 改动:DXVA 实验重构(主 reader 回软件路 + 一次性探针 QI IMFDXGIBuffer)、白名单补 RM-1104。
$ErrorActionPreference='Continue'
$log='E:\Apotheosis\port\build-0.2.5.35.log'
function Say($m){ $line="[$(Get-Date -Format 'HH:mm:ss')] $m"; Write-Host $line; Add-Content -Path $log -Value $line }
"===== 0.2.5.35 build start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') =====" | Set-Content $log
foreach($n in 'NO_PROXY','no_proxy','HTTP_PROXY','http_proxy','HTTPS_PROXY','https_proxy','ALL_PROXY','all_proxy','FTP_PROXY','ftp_proxy'){
  if(Test-Path "Env:$n"){ Remove-Item "Env:$n" -ErrorAction SilentlyContinue }
}
$ninja='C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
Say 'step 1/3: ninja WebCore (-j 2)'
. E:\Apotheosis\port\arm32-uwp-env.ps1 *> $null
& $ninja -j 2 -C E:\Apotheosis\build-clang-gpu WebCore *>> $log
$rc1=$LASTEXITCODE; Say "ninja WebCore rc=$rc1"
if($rc1 -ne 0){ Say 'ABORT: WebCore failed'; exit 1 }
Say 'step 2/3: link-driver-gpu.ps1'
& pwsh -NoProfile -File E:\Apotheosis\port\link-driver-gpu.ps1 *>> $log
$rc2=$LASTEXITCODE; Say "link-driver-gpu rc=$rc2"
if($rc2 -ne 0){ Say 'ABORT: driver link failed'; exit 1 }
Say 'step 3/3: build-harness.ps1'
& pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1 *>> $log
$rc3=$LASTEXITCODE; Say "build-harness rc=$rc3"
if($rc3 -ne 0){ Say 'ABORT: harness failed'; exit 1 }
$appx='E:\Apotheosis\harness\AppPackages\Harness\Harness_0.2.5.35_ARM_Test\Harness_0.2.5.35_ARM.appx'
if(Test-Path $appx){ Say "DONE: $appx ($([int]((Get-Item $appx).Length/1MB)) MB)" } else { Say 'DONE but appx missing' }
