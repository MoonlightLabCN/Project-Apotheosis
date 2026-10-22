$ErrorActionPreference = 'Continue'
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy','NO_PROXY','no_proxy')) {
    Remove-Item ("Env:" + $n) -ErrorAction SilentlyContinue
}
$p = [System.Diagnostics.Process]::GetCurrentProcess(); $p.PriorityClass = "BelowNormal"
Set-Location E:\Apotheosis
Write-Output "=== link driver (port only) ==="
pwsh -File E:\Apotheosis\port\link-driver-gpu.ps1
$rc = $LASTEXITCODE
Write-Output "link exit=$rc"
if ($rc -ne 0) { exit $rc }
Write-Output "=== build harness (0.2.5.11) ==="
pwsh -File E:\Apotheosis\port\build-harness.ps1
$rc2 = $LASTEXITCODE
Write-Output "harness exit=$rc2"
exit $rc2
