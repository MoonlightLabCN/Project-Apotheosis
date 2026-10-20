Get-ChildItem Env: | Where-Object { $_.Name -match "(?i)proxy" } | ForEach-Object { Remove-Item ("Env:" + $_.Name) }
for($i=0;$i -lt 720;$i++){
  $pkg = Test-Path "E:\Apotheosis\harness\AppPackages\Harness\Harness_0.2.5.2_ARM_Test\Harness_0.2.5.2_ARM.appx"
  if($pkg -and (Test-Connection 192.168.3.159 -Count 2 -Quiet)){ Start-Sleep 8; if(Test-Connection 192.168.3.159 -Count 1 -Quiet){ break } }
  Start-Sleep 10
}
pwsh -NoProfile -File E:\Apotheosis\tools\deploy-launch.ps1 -Ip 192.168.3.159 -Ver 0.2.5.2 *> E:\Apotheosis\deploy-0252.log
Start-Sleep 45
pwsh -NoProfile -File E:\Apotheosis\tools\pull-file.ps1 -Ip 192.168.3.159 -Ver 0.2.5.2 -File stage.txt *>> E:\Apotheosis\deploy-0252.log
"DEPLOY-DONE" | Out-File -Append E:\Apotheosis\deploy-0252.log
