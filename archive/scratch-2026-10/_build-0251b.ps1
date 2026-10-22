Get-ChildItem Env: | Where-Object { $_.Name -match "(?i)proxy" } | ForEach-Object { Remove-Item ("Env:" + $_.Name) }
function Die($m){ "FATAL: $m" | Out-File -Append E:\Apotheosis\build-0251b.log; exit 1 }
. E:\Apotheosis\port\arm32-uwp-env.ps1 *> $null
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" -C E:\Apotheosis\build-clang-gpu WebCore *> E:\Apotheosis\build-0251b.log
if($LASTEXITCODE -ne 0){ Die "ninja" }
pwsh -NoProfile -File E:\Apotheosis\port\link-driver-gpu.ps1 *>> E:\Apotheosis\build-0251b.log
pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1 *>> E:\Apotheosis\build-0251b.log
if($LASTEXITCODE -ne 0){ Die "harness" }
pwsh -NoProfile -File E:\Apotheosis\tools\deploy-launch.ps1 -Ip 192.168.3.159 -Ver 0.2.5.1 *>> E:\Apotheosis\build-0251b.log
"ALL-DONE" | Out-File -Append E:\Apotheosis\build-0251b.log
