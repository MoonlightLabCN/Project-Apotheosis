Get-ChildItem Env: | Where-Object { $_.Name -match "(?i)proxy" } | ForEach-Object { Remove-Item ("Env:" + $_.Name) }
[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"
. E:\Apotheosis\port\arm32-uwp-env.ps1 *> $null
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" -C E:\Apotheosis\build-clang-gpu WebCore *> E:\Apotheosis\build-0255.log
if($LASTEXITCODE -ne 0){ "FATAL ninja" | Out-File -Append E:\Apotheosis\build-0255.log; exit 1 }
pwsh -NoProfile -File E:\Apotheosis\port\link-driver-gpu.ps1 *>> E:\Apotheosis\build-0255.log
pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1 *>> E:\Apotheosis\build-0255.log
if($LASTEXITCODE -ne 0){ "FATAL harness" | Out-File -Append E:\Apotheosis\build-0255.log; exit 1 }
for($i=0;$i -lt 360;$i++){ if(Test-Connection 192.168.3.159 -Count 2 -Quiet){ Start-Sleep 8; if(Test-Connection 192.168.3.159 -Count 1 -Quiet){ break } }; Start-Sleep 10 }
pwsh -NoProfile -File E:\Apotheosis\tools\deploy-launch.ps1 -Ip 192.168.3.159 -Ver 0.2.5.5 *>> E:\Apotheosis\build-0255.log
Start-Sleep 45
pwsh -NoProfile -File E:\Apotheosis\tools\pull-file.ps1 -Ip 192.168.3.159 -Ver 0.2.5.5 -File stage.txt *>> E:\Apotheosis\build-0255.log
"ALL-DONE" | Out-File -Append E:\Apotheosis\build-0255.log
