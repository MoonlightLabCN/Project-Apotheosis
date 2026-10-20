Get-ChildItem Env: | Where-Object { $_.Name -match "(?i)proxy" } | ForEach-Object { Remove-Item ("Env:" + $_.Name) }
[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"
pwsh -NoProfile -File E:\Apotheosis\port\link-driver-gpu.ps1 *> E:\Apotheosis\build-0254.log
pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1 *>> E:\Apotheosis\build-0254.log
"HARNESS-EXIT=$LASTEXITCODE" | Out-File -Append E:\Apotheosis\build-0254.log
for($i=0;$i -lt 360;$i++){ if(Test-Connection 192.168.3.159 -Count 2 -Quiet){ Start-Sleep 8; if(Test-Connection 192.168.3.159 -Count 1 -Quiet){ break } }; Start-Sleep 10 }
pwsh -NoProfile -File E:\Apotheosis\tools\deploy-launch.ps1 -Ip 192.168.3.159 -Ver 0.2.5.4 *>> E:\Apotheosis\build-0254.log
Start-Sleep 45
pwsh -NoProfile -File E:\Apotheosis\tools\pull-file.ps1 -Ip 192.168.3.159 -Ver 0.2.5.4 -File stage.txt *>> E:\Apotheosis\build-0254.log
"ALL-DONE" | Out-File -Append E:\Apotheosis\build-0254.log
