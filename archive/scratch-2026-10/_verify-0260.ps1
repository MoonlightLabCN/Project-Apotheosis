$ErrorActionPreference = 'Continue'
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy','NO_PROXY','no_proxy')) {
    Remove-Item ("Env:" + $n) -ErrorAction SilentlyContinue
}
[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"
Set-Location E:\Apotheosis
Write-Output "=== verify 0.2.5.10 ==="
pwsh -File E:\Apotheosis\_verify-0259.ps1 -Ip 192.168.3.159 -Ver 0.2.5.10 -WaitSec 180
$rc = $LASTEXITCODE
Write-Output "verify exit=$rc"
exit $rc
