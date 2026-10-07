param([Parameter(Mandatory)][string]$CrashPath)
$ErrorActionPreference='Stop'
$body=[IO.File]::ReadAllText((Resolve-Path $CrashPath))
$events=$body -split '(?=\n==== crash )'
$event=$events|Where-Object {$_ -match 'memory lr-code' -and $_ -match 'access=8'}|Select-Object -Last 1
if(-not $event){throw 'No lr-code sample'}
$match=[regex]::Match($event,'memory lr-code address=0x([0-9a-fA-F]+)[^\r\n]*\r?\n((?:bytes[^\r\n]*\r?\n)+)')
if(-not $match.Success){throw 'No readable LR bytes'}
$address=[Convert]::ToUInt32($match.Groups[1].Value,16)
$bytes=foreach($row in ($match.Groups[2].Value -split '\r?\n')){if($row -match '^bytes 0x[0-9a-fA-F]+: (.*)'){foreach($value in ($Matches[1] -split ' ')){if($value){'0x'+$value}}}}
$stem=Join-Path (Split-Path -Parent (Resolve-Path $CrashPath)) 'lr-code'
[IO.File]::WriteAllText("$stem.s",".text`n.syntax unified`n.thumb`n.thumb_func`njit_evidence:`n.byte "+($bytes -join ',')+"`n")
& 'C:\Program Files\LLVM\bin\clang.exe' --target=thumbv7-unknown-windows-msvc -c "$stem.s" -o "$stem.obj"
if($LASTEXITCODE){throw 'Assembler failed'}
$output=& 'C:\Program Files\LLVM\bin\llvm-objdump.exe' -D --section=.text "--adjust-vma=$address" "$stem.obj"
$output=$output|ForEach-Object {if($_ -match '^\s+([0-9a-f]+):'){ $offset=[Convert]::ToUInt32($Matches[1],16); '{0:x8} {1}' -f ($address+$offset),$_.TrimStart() } else {$_}}
$output|Set-Content -LiteralPath "$stem.disasm.txt"
$output
