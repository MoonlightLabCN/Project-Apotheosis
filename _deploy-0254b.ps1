[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"
Start-Sleep 4200   # 等主链的 60 分钟窗口过去
if(Test-Path E:\Apotheosis\build-0254.log){ if(Select-String -Path E:\Apotheosis\build-0254.log -Pattern "已安装" -Quiet){ exit } }
for($i=0;$i -lt 720;$i++){ if(Test-Connection 192.168.3.159 -Count 2 -Quiet){ Start-Sleep 8; if(Test-Connection 192.168.3.159 -Count 1 -Quiet){ break } }; Start-Sleep 10 }
pwsh -NoProfile -File E:\Apotheosis\tools\deploy-launch.ps1 -Ip 192.168.3.159 -Ver 0.2.5.4 *> E:\Apotheosis\deploy-0254b.log
Start-Sleep 45
pwsh -NoProfile -File E:\Apotheosis\tools\pull-file.ps1 -Ip 192.168.3.159 -Ver 0.2.5.4 -File stage.txt *>> E:\Apotheosis\deploy-0254b.log
"DEPLOY-DONE" | Out-File -Append E:\Apotheosis\deploy-0254b.log
