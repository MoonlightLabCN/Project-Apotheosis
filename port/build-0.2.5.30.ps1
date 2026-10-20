# 0.2.5.30 链式构建:WebCore 全量 -> GPU 驱动重链 -> appx 打包。
# 触发原因:MediaPlayerPrivateWinUWP::notifyFrameReady() 的 readyState 修复(播放中不再被
# 每帧压到 HAVE_CURRENT_DATA -> 不再每帧触发 waiting/stalled)。
$ErrorActionPreference = 'Continue'
$log = 'E:\Apotheosis\port\build-0.2.5.30.log'
function Say($m){ $line = "[$(Get-Date -Format 'HH:mm:ss')] $m"; Write-Host $line; Add-Content -Path $log -Value $line }

# MSBuild 的 ToolTask 用大小写不敏感的 Hashtable 组装子进程环境:环境里同时存在
# NO_PROXY 和 no_proxy(后台作业被沙箱注入大写版)会直接抛 ArgumentException,
# 表现为 "CL.exe 的命令行开关无效"。构建不需要任何代理,全清掉。
foreach($n in 'NO_PROXY','no_proxy','HTTP_PROXY','http_proxy','HTTPS_PROXY','https_proxy','ALL_PROXY','all_proxy','FTP_PROXY','ftp_proxy'){
  if(Test-Path "Env:$n"){ Remove-Item "Env:$n" -ErrorAction SilentlyContinue; Say "scrubbed env $n" }
}

"===== 0.2.5.30 build start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') =====" | Set-Content $log
$ninja = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'

Say 'step 1/3: ninja WebCore (resume with -j 2; 16GB machine OOMed at -j 4)'
. E:\Apotheosis\port\arm32-uwp-env.ps1 *> $null
& $ninja -j 2 -C E:\Apotheosis\build-clang-gpu WebCore *>> $log
$rc1 = $LASTEXITCODE
Say "ninja WebCore rc=$rc1"
if ($rc1 -ne 0) { Say 'ABORT: WebCore build failed'; exit 1 }

Say 'step 2/3: link-driver-gpu.ps1'
& pwsh -NoProfile -File E:\Apotheosis\port\link-driver-gpu.ps1 *>> $log
$rc2 = $LASTEXITCODE
Say "link-driver-gpu rc=$rc2"
if ($rc2 -ne 0) { Say 'ABORT: driver link failed'; exit 1 }

Say 'step 3/3: build-harness.ps1'
& pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1 *>> $log
$rc3 = $LASTEXITCODE
Say "build-harness rc=$rc3"
if ($rc3 -ne 0) { Say 'ABORT: harness build failed'; exit 1 }

$appx = 'E:\Apotheosis\harness\AppPackages\Harness\Harness_0.2.5.30_ARM_Test\Harness_0.2.5.30_ARM.appx'
if (Test-Path $appx) {
    Say "DONE: $appx ($([int]((Get-Item $appx).Length/1MB)) MB)"
} else {
    Say 'DONE but appx not found at expected path'
}
