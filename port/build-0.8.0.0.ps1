# 0.8.0.0 最终发布构建链:WebCore -> GPU 驱动 -> appx。
# 用法:
#   pwsh -File E:\Apotheosis\port\build-0.8.0.0.ps1            # 增量构建(默认)
#   pwsh -File E:\Apotheosis\port\build-0.8.0.0.ps1 -Configure  # 先重新 configure
#
# 前置(见 docs/HARNESS-BUILD.md 与 AGENTS.md):
#   · WebKit/ 已就位(独立 git repo), build-clang-gpu/ 已配置
#   · ARM32 UWP 工具链: MSVC v143 14.44.35207 + SDK 22621
#   · 本机 16GB 内存: ninja 必须 -j 2(-j 4 会 OOM)
param(
    [switch]$Configure
)
$ErrorActionPreference = 'Continue'
$ver = '0.8.0.0'
$root = 'E:\Apotheosis'
$log = Join-Path $root "port\build-$ver.log"
function Say($m){ $line = "[$(Get-Date -Format 'HH:mm:ss')] $m"; Write-Host $line; Add-Content -Path $log -Value $line }

# MSBuild 的 ToolTask 用大小写不敏感的 Hashtable 组装子进程环境:同时存在
# NO_PROXY 和 no_proxy 会直接抛 ArgumentException。构建不需要代理,全清掉。
foreach($n in 'NO_PROXY','no_proxy','HTTP_PROXY','http_proxy','HTTPS_PROXY','https_proxy','ALL_PROXY','all_proxy','FTP_PROXY','ftp_proxy'){
  if(Test-Path "Env:$n"){ Remove-Item "Env:$n" -ErrorAction SilentlyContinue; Say "scrubbed env $n" }
}

"===== $ver build start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') =====" | Set-Content $log
$ninja = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$buildDir = Join-Path $root 'build-clang-gpu'

if ($Configure) {
    Say 'step 0/4: configure-gpu.ps1'
    & pwsh -NoProfile -File (Join-Path $root 'port\configure-gpu.ps1') *>> $log
    $rc0 = $LASTEXITCODE
    Say "configure-gpu rc=$rc0"
    if ($rc0 -ne 0) { Say 'ABORT: configure failed'; exit 1 }
} elseif (-not (Test-Path (Join-Path $buildDir 'build.ninja'))) {
    Say "ABORT: $buildDir 未配置(先跑 -Configure 或 port\configure-gpu.ps1)"; exit 1
}

Say 'step 1/4: ninja WebCore (resume with -j 2; 16GB machine OOMed at -j 4)'
. (Join-Path $root 'port\arm32-uwp-env.ps1') *> $null
& $ninja -j 2 -C $buildDir WebCore *>> $log
$rc1 = $LASTEXITCODE
Say "ninja WebCore rc=$rc1"
if ($rc1 -ne 0) { Say 'ABORT: WebCore build failed'; exit 1 }

Say 'step 2/4: link-driver-gpu.ps1'
& pwsh -NoProfile -File (Join-Path $root 'port\link-driver-gpu.ps1') *>> $log
$rc2 = $LASTEXITCODE
Say "link-driver-gpu rc=$rc2"
if ($rc2 -ne 0) { Say 'ABORT: driver link failed'; exit 1 }

Say 'step 3/4: build-harness.ps1'
& pwsh -NoProfile -File (Join-Path $root 'port\build-harness.ps1') *>> $log
$rc3 = $LASTEXITCODE
Say "build-harness rc=$rc3"
if ($rc3 -ne 0) { Say 'ABORT: harness build failed'; exit 1 }

Say 'step 4/4: locate appx'
$appxDir = Join-Path $root "harness\AppPackages\Harness\Harness_${ver}_ARM_Test"
$appx = Join-Path $appxDir "Harness_${ver}_ARM.appx"
if (Test-Path $appx) {
    Say "DONE: $appx ($([int]((Get-Item $appx).Length/1MB)) MB)"
    Write-Host ""
    Write-Host "下一步:" -ForegroundColor Cyan
    Write-Host "  1) 真机安装: pwsh -File tools\install-only.ps1 -Ip <设备IP> -Ver $ver" -ForegroundColor Cyan
    Write-Host "  2) 打 tag:   git tag v$ver && git push origin v$ver" -ForegroundColor Cyan
    Write-Host "  3) 附资产:   gh release upload v$ver `"$appx`" <cer> --clobber" -ForegroundColor Cyan
} else {
    Say "DONE but appx not found at $appx"
    exit 1
}
