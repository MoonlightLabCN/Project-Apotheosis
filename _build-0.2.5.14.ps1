$ErrorActionPreference = 'Continue'
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy','NO_PROXY','no_proxy')) {
    Remove-Item ("Env:" + $n) -ErrorAction SilentlyContinue
}
$p = [System.Diagnostics.Process]::GetCurrentProcess(); $p.PriorityClass = "BelowNormal"
Set-Location E:\Apotheosis
Write-Output "=== [1/2] link driver ==="
pwsh -NoProfile -File E:\Apotheosis\port\link-driver-gpu.ps1
$rc = $LASTEXITCODE
Write-Output "link exit=$rc"
"driver lib: " + (Get-Item E:\Apotheosis\port\WebCoreDriver-gpu.lib).LastWriteTime
if ($rc -ne 0) { exit $rc }
Write-Output "=== [2/2] build harness (XamlMode default) ==="
pwsh -NoProfile -File E:\Apotheosis\port\build-harness.ps1
$rc2 = $LASTEXITCODE
Write-Output "harness exit=$rc2"
"now: " + (Get-Date -Format 'HH:mm:ss')
exit $rc2
