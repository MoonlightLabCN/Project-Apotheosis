$ErrorActionPreference = 'Continue'
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy','NO_PROXY','no_proxy')) { Remove-Item ("Env:" + $n) -ErrorAction SilentlyContinue }
$p = [System.Diagnostics.Process]::GetCurrentProcess(); $p.PriorityClass = "BelowNormal"
Set-Location E:\Apotheosis

# archive evidence (do not overwrite 0.2.5.14)
$dst = 'E:\Apotheosis\crash\jit-analysis-20261007\0.2.5.15'
New-Item -ItemType Directory -Force $dst | Out-Null
Copy-Item 'E:\Apotheosis\harness\ARM\Release\Harness\Harness.exe' $dst -Force
Copy-Item 'E:\Apotheosis\harness\ARM\Release\Harness\Harness.pdb' $dst -Force -ErrorAction SilentlyContinue
Copy-Item 'E:\Apotheosis\harness\AppPackages\Harness\Harness_0.2.5.15_ARM_Test\Harness_0.2.5.15_ARM.appx' $dst -Force
Copy-Item 'E:\Apotheosis\harness\AppPackages\Harness\Harness_0.2.5.15_ARM_Test\Harness_0.2.5.15_ARM.appxsym' $dst -Force -ErrorAction SilentlyContinue
"archived to $dst"
"EXE sha256: $((Get-FileHash 'E:\Apotheosis\harness\ARM\Release\Harness\Harness.exe' -Algorithm SHA256).Hash)"

Write-Output "=== install 0.2.5.15 (non-destructive) ==="
pwsh -NoProfile -File E:\Apotheosis\tools\install-only.ps1 -Ip 192.168.3.159 -Ver 0.2.5.15 2>&1 | Select-Object -Last 5
"install exit=$LASTEXITCODE"

Write-Output "=== verify install state ==="
pwsh -NoProfile -File E:\Apotheosis\_verify-installed-14.ps1 2>&1 | Select-Object -First 8
exit 0
