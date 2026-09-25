# run-psl-tests.ps1 — build + run the Public Suffix List unit test on the x64 host.
#
# The engine only runs on the ARM32 device, so port/PublicSuffixLookup.h is kept
# WebKit-free precisely so its logic can be verified here, on every change, in a
# second — instead of via a 20-minute appx round trip to a Lumia.
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$out  = Join-Path $here 'psl-test.exe'

$clang = 'C:\Program Files\LLVM\bin\clang++.exe'
if (-not (Test-Path -LiteralPath $clang)) { throw "clang++ not found: $clang" }

# This script must not inherit the ARM32 UWP LIB/INCLUDE from arm32-uwp-env.ps1
# (those point clang++ at ARM libcmt and the link then fails with "machine type
# arm conflicts with x64"). Host unit tests are x64.
$env:LIB = $null
$env:LIBPATH = $null
$env:INCLUDE = $null

$obj = Join-Path $here 'psl-test.obj'
Write-Host '=== building psl-test (x64 host) ===' -ForegroundColor Cyan
& $clang -std=c++20 -O1 -Wall -Wextra -c -o $obj (Join-Path $here 'psl-test.cpp')
if ($LASTEXITCODE -ne 0) { throw "psl-test failed to compile (exit $LASTEXITCODE)" }
& $clang -o $out $obj
if ($LASTEXITCODE -ne 0) { throw "psl-test failed to link (exit $LASTEXITCODE)" }
Remove-Item -LiteralPath $obj -ErrorAction SilentlyContinue

Write-Host '=== running psl-test ===' -ForegroundColor Cyan
& $out
exit $LASTEXITCODE
