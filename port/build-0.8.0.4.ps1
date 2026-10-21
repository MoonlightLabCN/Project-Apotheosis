# 0.8.0.4:harness-only 构建(基线 0.8.0.0)。
# ① 端点选择不再依赖 IsDefault(此机两个端点 IsDefault 全 0 → 绑定扑空 → null sink);
# ② ★ 补上 AudioDeviceOutputNode + AddOutgoingConnection(无声真正根因,0.8.0.2 已真机确认有声);
# ③ 空转自停窗口 3s→20s + Reset 复位停止状态;# ④ 摘除 441Hz 蜂鸣测试(出货版不留);# ⑤ 修 JIT/UI 线程空指针崩溃:AudioLog 里 ApplicationData::Current/LocalFolder 无守卫解引用(EnsureStarted+0x231, r0=0)。
$ErrorActionPreference='Continue'
$log='E:\Apotheosis\port\build-0.8.0.4.log'
function Say($m){ $line="[$(Get-Date -Format 'HH:mm:ss')] $m"; Write-Host $line; Add-Content -Path $log -Value $line }
"===== 0.8.0.4 harness-only start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') =====" | Set-Content $log
foreach($n in 'NO_PROXY','no_proxy','HTTP_PROXY','http_proxy','HTTPS_PROXY','https_proxy','ALL_PROXY','all_proxy','FTP_PROXY','ftp_proxy'){
  if(Test-Path "Env:$n"){ Remove-Item "Env:$n" -ErrorAction SilentlyContinue }
}
Say 'build-harness (audio endpoint selection + tone test)'
& pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1 *>> $log
$rc=$LASTEXITCODE
Say "build-harness rc=$rc"
$appx='E:\Apotheosis\harness\AppPackages\Harness\Harness_0.8.0.4_ARM_Test\Harness_0.8.0.4_ARM.appx'
if($rc -eq 0 -and (Test-Path $appx)){ Say "DONE: $appx ($([int]((Get-Item $appx).Length/1MB)) MB)" } else {
  Say 'FAILED - errors:'
  Select-String -Path $log -Pattern 'error ' | Select-Object -First 8 | ForEach-Object { Say "  $($_.Line.Trim())" }
  exit 1
}
