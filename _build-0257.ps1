[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"
Get-ChildItem Env: | Where-Object { $_.Name -match "(?i)proxy" } | ForEach-Object { Remove-Item ("Env:" + $_.Name) }
pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1 *> E:\Apotheosis\build-0257.log
if($LASTEXITCODE -ne 0){ "FATAL harness" | Out-File -Append E:\Apotheosis\build-0257.log; exit 1 }
for($i=0;$i -lt 720;$i++){ if(Test-Connection 192.168.3.159 -Count 2 -Quiet){ Start-Sleep 8; if(Test-Connection 192.168.3.159 -Count 1 -Quiet){ break } }; Start-Sleep 10 }
pwsh -NoProfile -File E:\Apotheosis\tools\deploy-launch.ps1 -Ip 192.168.3.159 -Ver 0.2.5.7 *>> E:\Apotheosis\build-0257.log
"ALL-DONE" | Out-File -Append E:\Apotheosis\build-0257.log
