Get-ChildItem Env: | Where-Object { $_.Name -match "(?i)proxy" } | ForEach-Object { Remove-Item ("Env:" + $_.Name) }
pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1 *> E:\Apotheosis\build-0250.log
"HARNESS-EXIT=$LASTEXITCODE" | Out-File -Append E:\Apotheosis\build-0250.log
