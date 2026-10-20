$ErrorActionPreference='Continue'
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy','NO_PROXY','no_proxy')) { Remove-Item ("Env:" + $n) -ErrorAction SilentlyContinue }
$p=[System.Diagnostics.Process]::GetCurrentProcess(); $p.PriorityClass="BelowNormal"
Set-Location E:\Apotheosis
pwsh -NoProfile -File E:\Apotheosis\port\link-driver-gpu.ps1
$rc=$LASTEXITCODE
"link exit=$rc"
"driver lib: " + (Get-Item E:\Apotheosis\port\WebCoreDriver-gpu.lib).LastWriteTime
exit $rc
