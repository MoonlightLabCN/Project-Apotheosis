$ErrorActionPreference = 'Continue'
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy','NO_PROXY','no_proxy')) {
    Remove-Item ("Env:" + $n) -ErrorAction SilentlyContinue
}
$p = [System.Diagnostics.Process]::GetCurrentProcess(); $p.PriorityClass = "BelowNormal"
Set-Location E:\Apotheosis
Write-Output "=== deploy 0.2.5.11 ==="
pwsh -File E:\Apotheosis\tools\deploy-launch.ps1 -Ip 192.168.3.159 -Ver 0.2.5.11
$rc = $LASTEXITCODE
Write-Output "deploy exit=$rc"
if ($rc -ne 0) { exit $rc }
Write-Output "=== verify 0.2.5.11 ==="
pwsh -File E:\Apotheosis\_verify-0259.ps1 -Ip 192.168.3.159 -Ver 0.2.5.11 -WaitSec 180
$rc2 = $LASTEXITCODE
Write-Output "verify exit=$rc2"
exit $rc2
