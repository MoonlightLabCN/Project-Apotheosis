# 0.8.0.2:harness-only 构建(基线 0.8.0.0)。
# ① 端点选择不再依赖 IsDefault(此机两个端点 IsDefault 全 0 → 绑定扑空 → null sink);
# ② ★ 补上 AudioDeviceOutputNode + AddOutgoingConnection —— 这才是无声的真正根因;
# ③ 空转自停窗口 3s→20s(3s 会在真实播放中误停) + Reset 复位停止状态(消除重复执行)。
$ErrorActionPreference='Continue'
$log='E:\Apotheosis\port\build-0.8.0.2.log'
function Say($m){ $line="[$(Get-Date -Format 'HH:mm:ss')] $m"; Write-Host $line; Add-Content -Path $log -Value $line }
"===== 0.8.0.2 harness-only start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') =====" | Set-Content $log
foreach($n in 'NO_PROXY','no_proxy','HTTP_PROXY','http_proxy','HTTPS_PROXY','https_proxy','ALL_PROXY','all_proxy','FTP_PROXY','ftp_proxy'){
  if(Test-Path "Env:$n"){ Remove-Item "Env:$n" -ErrorAction SilentlyContinue }
}
Say 'build-harness (audio endpoint selection + tone test)'
& pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1 *>> $log
$rc=$LASTEXITCODE
Say "build-harness rc=$rc"
$appx='E:\Apotheosis\harness\AppPackages\Harness\Harness_0.8.0.2_ARM_Test\Harness_0.8.0.2_ARM.appx'
if($rc -eq 0 -and (Test-Path $appx)){ Say "DONE: $appx ($([int]((Get-Item $appx).Length/1MB)) MB)" } else {
  Say 'FAILED - errors:'
  Select-String -Path $log -Pattern 'error ' | Select-Object -First 8 | ForEach-Object { Say "  $($_.Line.Trim())" }
  exit 1
}
