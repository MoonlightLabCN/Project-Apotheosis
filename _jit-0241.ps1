Get-ChildItem Env: | Where-Object { $_.Name -match "(?i)proxy" } | ForEach-Object { Remove-Item ("Env:" + $_.Name) }
pwsh -NoProfile -File E:\Apotheosis\port\link-driver-gpu.ps1 *> E:\Apotheosis\jit-0241.log
pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1 *>> E:\Apotheosis\jit-0241.log
pwsh -NoProfile -File E:\Apotheosis\tools\deploy-launch.ps1 -Ip 192.168.3.159 -Ver 0.2.4.1 *>> E:\Apotheosis\jit-0241.log
Start-Sleep 40
pwsh -NoProfile -File E:\Apotheosis\tools\pull-file.ps1 -Ip 192.168.3.159 -Ver 0.2.4.1 -File stage.txt *>> E:\Apotheosis\jit-0241.log
"ALL-DONE" | Out-File -Append E:\Apotheosis\jit-0241.log
