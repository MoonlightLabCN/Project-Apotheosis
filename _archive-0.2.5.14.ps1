$ErrorActionPreference = 'Continue'
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy','NO_PROXY','no_proxy')) {
    Remove-Item ("Env:" + $n) -ErrorAction SilentlyContinue
}
$p = [System.Diagnostics.Process]::GetCurrentProcess(); $p.PriorityClass = "BelowNormal"
$dst = 'E:\Apotheosis\crash\jit-analysis-20261007\0.2.5.14'
New-Item -ItemType Directory -Force $dst | Out-Null
Copy-Item 'E:\Apotheosis\harness\ARM\Release\Harness\Harness.exe' $dst -Force
Copy-Item 'E:\Apotheosis\harness\ARM\Release\Harness\Harness.pdb' $dst -Force -ErrorAction SilentlyContinue
Copy-Item 'E:\Apotheosis\harness\AppPackages\Harness\Harness_0.2.5.14_ARM_Test\Harness_0.2.5.14_ARM.appxsym' $dst -Force -ErrorAction SilentlyContinue
Copy-Item 'E:\Apotheosis\harness\AppPackages\Harness\Harness_0.2.5.14_ARM_Test\Harness_0.2.5.14_ARM.appx' $dst -Force
$h = Get-FileHash 'E:\Apotheosis\harness\ARM\Release\Harness\Harness.exe' -Algorithm SHA256
$ha = Get-FileHash 'E:\Apotheosis\harness\AppPackages\Harness\Harness_0.2.5.14_ARM_Test\Harness_0.2.5.14_ARM.appx' -Algorithm SHA256
"EXE sha256 : $($h.Hash)"
"APPX sha256: $($ha.Hash)"
"$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') archived to $dst"
Get-ChildItem $dst | Select-Object Name,Length | Format-Table -AutoSize
exit 0
