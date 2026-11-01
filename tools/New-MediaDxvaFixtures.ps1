# Generate local, reproducible H.264/AAC clips for the opt-in DXVA reader probe.
# No device access. Serve with Test-MediaServer.ps1, then open /dxva.html.
param(
    [string]$Root = 'E:\Apotheosis\port\_mediatest',
    [string]$FFmpeg = 'ffmpeg.exe'
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $Root -Force | Out-Null
foreach ($clip in @(
    @{ Name = 'test-720p.mp4'; Size = '1280x720'; Level = '3.1' },
    @{ Name = 'test-1080p.mp4'; Size = '1920x1080'; Level = '4.0' }
)) {
    $output = Join-Path $Root $clip.Name
    & $FFmpeg -hide_banner -loglevel error -nostdin -y `
        -f lavfi -i "testsrc2=size=$($clip.Size):rate=30:duration=20" `
        -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=20' `
        -c:v libx264 -threads 2 -preset veryfast -crf 28 -profile:v main -level:v $clip.Level `
        -pix_fmt yuv420p -g 60 -c:a aac -b:a 96k -ac 2 -movflags +faststart $output
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed for $($clip.Name): $LASTEXITCODE" }
    Write-Host "$output ($((Get-Item -LiteralPath $output).Length) bytes)"
}
