Get-ChildItem Env: | Where-Object { $_.Name -match "(?i)proxy" } | ForEach-Object { Remove-Item ("Env:" + $_.Name) }
[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"   # Apotheosis: 子进程继承优先级,构建不抢前台
