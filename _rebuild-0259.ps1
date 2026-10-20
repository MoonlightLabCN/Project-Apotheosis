$ErrorActionPreference = 'Continue'
Set-Location E:\Apotheosis
Write-Output "=== [1/3] source arm32 env ==="
. E:\Apotheosis\port\arm32-uwp-env.ps1
Write-Output "=== [2/3] ninja WebCore (VM.cpp change) ==="
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" -C E:\Apotheosis\build-clang-gpu WebCore
$rc = $LASTEXITCODE
Write-Output "ninja exit=$rc"
if ($rc -ne 0) { Write-Output "BUILD_FAILED_NINJA"; exit $rc }
Write-Output "=== [3/3] link driver ==="
pwsh -File E:\Apotheosis\port\link-driver-gpu.ps1
$rc2 = $LASTEXITCODE
Write-Output "link exit=$rc2"
exit $rc2
