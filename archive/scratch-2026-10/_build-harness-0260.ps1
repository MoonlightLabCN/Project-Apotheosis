$ErrorActionPreference = 'Continue'
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy','NO_PROXY','no_proxy')) {
    Remove-Item ("Env:" + $n) -ErrorAction SilentlyContinue
}
$p = [System.Diagnostics.Process]::GetCurrentProcess()
$p.PriorityClass = "BelowNormal"
Set-Location E:\Apotheosis
Write-Output "=== build harness (0.2.5.10) ==="
pwsh -File E:\Apotheosis\port\build-harness.ps1
$rc = $LASTEXITCODE
Write-Output "harness build exit=$rc"
exit $rc
