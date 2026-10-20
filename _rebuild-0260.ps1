$ErrorActionPreference = 'Continue'
Set-Location E:\Apotheosis
Write-Output "=== [1/2] ninja WTF/WebCore with -j 4 (OOM lesson: default -j12 OOMs) ==="
. E:\Apotheosis\port\arm32-uwp-env.ps1
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" -j 4 -C E:\Apotheosis\build-clang-gpu WTF WebCore
$rc = $LASTEXITCODE
Write-Output "ninja exit=$rc"
if ($rc -ne 0) { Write-Output "BUILD_FAILED_NINJA"; exit $rc }
Write-Output "=== [2/2] link driver ==="
pwsh -File E:\Apotheosis\port\link-driver-gpu.ps1
$rc2 = $LASTEXITCODE
Write-Output "link exit=$rc2"
exit $rc2
