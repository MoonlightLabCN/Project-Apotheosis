# ============================================================================
# probe-compile.ps1 — compile ONE WebCore source with the exact flags ninja uses.
#
# WHY THIS EXISTS: every `ninja WebCore` on this tree is ~1007 steps / 60-80 minutes,
# because the CMake precompiled header is regenerated and invalidates every translation
# unit. Iterating on a compile error through the real build therefore costs an hour per
# round. This reconstructs a single compile command out of ninja's own database and runs
# it against a scratch object, which takes about 40 seconds.
#
# Workflow: probe-compile until the file is clean, then run the real build once.
#
#   pwsh -File E:\Apotheosis\port\probe-compile.ps1 `
#       -Object 'Source\WebCore\CMakeFiles\WebCore.dir\platform\graphics\win\Foo.cpp.obj' `
#       -Source 'E:\Apotheosis\WebKit\Source\WebCore\platform\graphics\win\Foo.cpp'
#
# -Object is the ninja target path exactly as build.ninja spells it (backslashes); find it
# with:  Select-String -Path build-clang-gpu\build.ninja -Pattern 'Foo\.cpp\.obj:'
#
# Notes:
#  * The ARM32 UWP environment has to be injected first: the precompiled header is keyed to
#    the MSVC toolset version, and clang-cl resolves that from the environment. Without it
#    the PCH is rejected with "Microsoft Visual C/C++ Version differs".
#  * The compile command is taken verbatim, so it stays correct as the build config changes;
#    only the input source and the output object are swapped.
# ============================================================================
param(
    [Parameter(Mandatory = $true)][string]$Object,
    [Parameter(Mandatory = $true)][string]$Source,
    [string]$Build = 'E:\Apotheosis\build-clang-gpu',
    [string]$ScratchObject = 'E:\Apotheosis\port\probe.obj'
)

$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\arm32-uwp-env.ps1" *> $null

$ninja = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
if (-not (Test-Path $ninja)) { throw "ninja not found at $ninja" }
if (-not (Test-Path $Source)) { throw "source not found: $Source" }

# -t commands also prints every dependency's command, so the target's own line is the last one.
$lines = & $ninja -C $Build -t commands $Object 2>&1
if (-not $lines) { throw "ninja -t commands produced nothing for $Object" }
$cmd = $lines[-1]

$bareObject = Split-Path $Object -Leaf
if ($cmd -notlike "*$bareObject*") {
    throw "the last command does not mention $bareObject - is the target spelled exactly as build.ninja has it?"
}

# Swap the output object (it appears as /Fo, -clang:-MT and -clang:-MF) ...
$cmd = $cmd.Replace($Object, $ScratchObject)
# ... and the input source, which is always the final token after `-c --`.
$separator = ' -c -- '
$index = $cmd.LastIndexOf($separator)
if ($index -lt 0) { throw "could not find '-c --' in the compile command" }
$cmd = $cmd.Substring(0, $index + $separator.Length) + $Source

# cmd.exe re-parses the line, so it has to go through a file rather than -Command.
$bat = Join-Path $PSScriptRoot 'probe-compile.tmp.bat'
Set-Content -Path $bat -Value ('@echo off' + "`r`n" + $cmd.TrimEnd()) -Encoding ASCII

try {
    Push-Location $Build
    & cmd /c $bat
    $code = $LASTEXITCODE
}
finally {
    Pop-Location
    Remove-Item $bat -ErrorAction SilentlyContinue
}

if ($code -eq 0) {
    Write-Host "[probe-compile] OK  $Source" -ForegroundColor Green
} else {
    Write-Host "[probe-compile] FAILED (exit $code)  $Source" -ForegroundColor Red
}
exit $code
