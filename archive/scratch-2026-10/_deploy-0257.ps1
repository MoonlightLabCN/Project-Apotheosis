[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"
Get-ChildItem Env: | Where-Object { $_.Name -match "(?i)proxy" } | ForEach-Object { Remove-Item ("Env:" + $_.Name) }
$done = $false
for($i=0;$i -lt 480 -and -not $done;$i++){
  if(Test-Connection 192.168.3.159 -Count 2 -Quiet){
    pwsh -NoProfile -File E:\Apotheosis\tools\deploy-launch.ps1 -Ip 192.168.3.159 -Ver 0.2.5.7 *>> E:\Apotheosis\deploy-0257.log
    if(Select-String -Path E:\Apotheosis\deploy-0257.log -Pattern "已安装.*0.2.5.7" -Quiet){ $done=$true; break }
  }
  Start-Sleep 15
}
"WATCH-END done=$done" | Out-File -Append E:\Apotheosis\deploy-0257.log
