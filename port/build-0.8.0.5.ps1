# 0.8.0.5:全链构建(基线 0.8.0.0)。
# 修复:WTF::RedBlackTree::remove() 未清 unlinked 节点自身的 CheckedPtr(m_left/m_right),
# 导致 ScheduledTask 被删除时报 "1 CheckedPtr outstanding" -> SIGABRT。
#   checked-ptr: ScheduledTask deleted with 1 CheckedPtr(s) outstanding (zeroed and leaked)
#   checked-ptr: released a CheckedPtr to an already-deleted object
#   signal 22 (SIGABRT)   [真机 2026-10-22 22:59:05]
# 上游只在下次 insert() 时清(x->reset()),对"移除后即销毁"的节点(如 ~TimerBase)漏了。
# 相关改动在头文件 RedBlackTree.h,会重编 WTF/JSC/WebCore 大量 TU,故走全链。
$ErrorActionPreference='Continue'
$log='E:\Apotheosis\port\build-0.8.0.5.log'
function Say($m){ $line="[$(Get-Date -Format 'HH:mm:ss')] $m"; Write-Host $line; Add-Content -Path $log -Value $line }
"===== 0.8.0.5 full chain start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') =====" | Set-Content $log
foreach($n in 'NO_PROXY','no_proxy','HTTP_PROXY','http_proxy','HTTPS_PROXY','https_proxy','ALL_PROXY','all_proxy','FTP_PROXY','ftp_proxy'){
  if(Test-Path "Env:$n"){ Remove-Item "Env:$n" -ErrorAction SilentlyContinue }
}
$ninja='C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
Say 'step 1/3: ninja WebCore+JavaScriptCore (-j 2)'
. E:\Apotheosis\port\arm32-uwp-env.ps1 *> $null
& $ninja -j 2 -C E:\Apotheosis\build-clang-gpu WebCore JavaScriptCore *>> $log
$rc1=$LASTEXITCODE; Say "ninja rc=$rc1"
if($rc1 -ne 0){ Say 'ABORT: ninja failed'; exit 1 }
Say 'step 2/3: link-driver-gpu.ps1'
& pwsh -NoProfile -File E:\Apotheosis\port\link-driver-gpu.ps1 *>> $log
$rc2=$LASTEXITCODE; Say "link-driver-gpu rc=$rc2"
if($rc2 -ne 0){ Say 'ABORT: driver link failed'; exit 1 }
Say 'step 3/3: build-harness.ps1'
& pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1 *>> $log
$rc3=$LASTEXITCODE; Say "build-harness rc=$rc3"
if($rc3 -ne 0){ Say 'ABORT: harness failed'; exit 1 }
$appx='E:\Apotheosis\harness\AppPackages\Harness\Harness_0.8.0.5_ARM_Test\Harness_0.8.0.5_ARM.appx'
if(Test-Path $appx){ Say "DONE: $appx ($([int]((Get-Item $appx).Length/1MB)) MB)" } else {
  Say 'DONE but appx missing - errors:'
  Select-String -Path $log -Pattern 'error ' | Select-Object -First 8 | ForEach-Object { Say "  $($_.Line.Trim())" }
  exit 1
}
