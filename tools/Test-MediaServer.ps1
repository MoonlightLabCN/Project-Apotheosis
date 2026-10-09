# ============================================================================
# Test-MediaServer.ps1 — LAN HTTP server for exercising the engine's media path
# on the device without depending on the internet.
#
# WHY: the device's network reachability is the least controllable variable when
# testing <video>. Serving both the test page and the media file from this PC over
# the LAN removes it, and it lets the request log show exactly what fetched what.
#
# RANGE SUPPORT IS NOT OPTIONAL. WebCore's loader and (separately)
# MediaPlayerPrivateWinUWP's own Media Foundation SourceReader both fetch the media
# themselves; MF issues byte-range requests. A server that ignores Range returns the
# whole file to a ranged request and the reader gives up.
#
# Usage:
#   pwsh -File tools\Test-MediaServer.ps1 -Port 8099 -Root E:\Apotheosis\port\_mediatest
# then push LocalState\testurl.txt = http://<this-PC-LAN-IP>:8099/diag.html and restart
# the app (see docs/MEDIA-MF-IMPLEMENTATION.md).
#
# Files served from -Root; /diag.html and /video.html are generated here. Every request
# is logged with its Range header, which is how you tell "the element never asked" from
# "the fetch failed".
# ============================================================================
param(
    [int]$Port = 8099,
    [string]$Root = 'E:\Apotheosis\port\_mediatest',
    [string]$Log = 'E:\Apotheosis\port\_mediatest\server.log'
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $Root | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Log) | Out-Null

function Write-Log([string]$text)
{
    $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss.fff'), $text
    Write-Host $line
    Add-Content -Path $Log -Value $line
}

# A page that reports the media stack's state through the JS console, which the port
# mirrors into LocalState\console.txt and can be pulled over WDP.
$diagHtml = @'
<!DOCTYPE html><html><head><meta charset="utf-8"><title>media diag</title></head>
<body style="margin:0;background:#101418;color:#e8e8e8;font:16px sans-serif">
<h2 style="padding:12px">media diag</h2>
<video id="v" controls autoplay style="width:100%;max-width:420px"></video>
<script>
function L(m){ console.log('MEDIADIAG ' + m); }
L('HME=' + (typeof HTMLMediaElement) + ' MS=' + (typeof MediaSource) + ' MP=' + (typeof MediaPlayer));
var p = document.createElement('video');
L('canPlayType video/mp4=[' + (p.canPlayType ? p.canPlayType('video/mp4') : 'NOFN') + ']');
L('canPlayType video/webm=[' + (p.canPlayType ? p.canPlayType('video/webm') : '-') + ']');
L('UA=' + navigator.userAgent);
var v = document.getElementById('v');
['loadstart','progress','loadedmetadata','loadeddata','canplay','canplaythrough','playing','error','stalled','waiting','ended','durationchange'].forEach(function(e){
  v.addEventListener(e, function(){
    L('evt ' + e + ' rs=' + v.readyState + ' ns=' + v.networkState
      + ' dur=' + (isFinite(v.duration) ? v.duration.toFixed(2) : '?')
      + ' err=' + (v.error ? ('code' + v.error.code + ' ' + (v.error.message || '')) : '-')
      + (v.videoWidth ? (' ' + v.videoWidth + 'x' + v.videoHeight) : ''));
  });
});
v.src = 'test.mp4';
v.load();
setTimeout(function(){ L('t5 rs=' + v.readyState + ' ns=' + v.networkState + ' ct=' + v.currentTime.toFixed(2) + ' paused=' + v.paused); }, 5000);
setTimeout(function(){ L('t12 rs=' + v.readyState + ' ns=' + v.networkState + ' ct=' + v.currentTime.toFixed(2) + ' paused=' + v.paused); }, 12000);
</script>
</body></html>
'@

# The plainest possible case: a page whose only content is a video element.
$videoHtml = @'
<!DOCTYPE html><html><head><meta charset="utf-8"></head>
<body style="margin:0;background:#000">
<video src="test.mp4" controls autoplay style="width:100%"></video>
</body></html>
'@

# The element-factory discriminator that found the mediaEnabled(false) bug. In the generated
# HTMLElementFactory, `source` has no mediaEnabled() check (only #if ENABLE(VIDEO)) while
# video/audio/track have both, so the two answers together say which of the two is failing.
$factoryHtml = @'
<!DOCTYPE html><html><head><meta charset="utf-8"><title>factory probe</title></head>
<body style="margin:0;background:#101418;color:#e8e8e8;font:16px sans-serif">
<h3 style="padding:10px">factory probe</h3>
<script>
function ctor(name) { try { return document.createElement(name).constructor.name; } catch (e) { return 'THREW'; } }
var parts = [
  'video=' + ctor('video'), 'audio=' + ctor('audio'), 'source=' + ctor('source'),
  'track=' + ctor('track'), 'canvas=' + ctor('canvas'), 'div=' + ctor('div'),
  'bogus=' + ctor('bogus-tag-xyz'), 'img=' + ctor('img'),
  'parsedVideo=' + (function () { try { var d = document.createElement('div'); d.innerHTML = '<video></video>'; return d.firstChild.constructor.name; } catch (e) { return 'THREW'; } })(),
  'winHVE=' + (typeof HTMLVideoElement)
];
// Reported through the request log, not the JS console: console.txt mirroring produced
// nothing for these pages even though their timers demonstrably ran.
try { var img = new Image(); img.src = 'fac-' + parts.join('~') + '.txt'; }
catch (e) { try { var i2 = new Image(); i2.src = 'fac-SENDEXC.txt'; } catch (e2) { } }
</script>
</body></html>
'@

$contentTypes = @{
    '.mp4' = 'video/mp4'; '.m4v' = 'video/mp4'; '.m4a' = 'audio/mp4'; '.mp3' = 'audio/mpeg'
    '.html' = 'text/html; charset=utf-8'; '.txt' = 'text/plain; charset=utf-8'
    '.png' = 'image/png'; '.jpg' = 'image/jpeg'; '.js' = 'text/javascript; charset=utf-8'
}

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Any, $Port)
$listener.Start()
Write-Log "media server listening on 0.0.0.0:$Port  root=$Root"

while ($true) {
    $client = $null
    try {
        $client = $listener.AcceptTcpClient()
        $remote = $client.Client.RemoteEndPoint.ToString()
        $stream = $client.GetStream()
        $stream.ReadTimeout = 5000

        # Read the request head (no body handling: the device only ever GETs/HEADs here).
        $head = ''
        $buffer = New-Object byte[] 4096
        while ($head -notmatch "`r`n`r`n") {
            $read = $stream.Read($buffer, 0, $buffer.Length)
            if ($read -le 0) { break }
            $head += [Text.Encoding]::ASCII.GetString($buffer, 0, $read)
            if ($head.Length -gt 32768) { break }
        }

        $lines = $head -split "`r`n"
        $requestLine = if ($lines.Count) { $lines[0] } else { '' }
        $parts = $requestLine -split ' '
        $method = if ($parts.Count -gt 0) { $parts[0] } else { 'GET' }
        $rawPath = if ($parts.Count -gt 1) { $parts[1] } else { '/' }
        $rangeHeader = ($lines | Where-Object { $_ -match '(?i)^Range:\s*(.+)$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)

        $path = ($rawPath -split '\?')[0].TrimStart('/')
        if ([string]::IsNullOrEmpty($path)) { $path = 'diag.html' }

        # Generated pages, else a file under -Root (no traversal).
        $body = $null
        $contentType = $null
        if ($path -eq 'diag.html') { $body = [Text.Encoding]::UTF8.GetBytes($diagHtml); $contentType = 'text/html; charset=utf-8' }
        elseif ($path -eq 'video.html') { $body = [Text.Encoding]::UTF8.GetBytes($videoHtml); $contentType = 'text/html; charset=utf-8' }
        elseif ($path -eq 'factory.html') { $body = [Text.Encoding]::UTF8.GetBytes($factoryHtml); $contentType = 'text/html; charset=utf-8' }
        else {
            $safe = $path -replace '\.\.', ''
            $file = Join-Path $Root $safe
            if (Test-Path -LiteralPath $file -PathType Leaf) {
                $body = [System.IO.File]::ReadAllBytes($file)
                $ext = [System.IO.Path]::GetExtension($file).ToLowerInvariant()
                $contentType = if ($contentTypes.ContainsKey($ext)) { $contentTypes[$ext] } else { 'application/octet-stream' }
            }
        }

        if ($null -eq $body) {
            $notFound = [Text.Encoding]::UTF8.GetBytes("not found: $path")
            $resp = "HTTP/1.1 404 Not Found`r`nContent-Type: text/plain`r`nContent-Length: $($notFound.Length)`r`nConnection: close`r`n`r`n"
            Write-Log "$remote $method $rawPath -> 404"
            $hb = [Text.Encoding]::ASCII.GetBytes($resp)
            $stream.Write($hb, 0, $hb.Length); $stream.Write($notFound, 0, $notFound.Length)
            $stream.Flush(); $stream.Close(); $client.Close(); continue
        }

        $total = $body.Length
        $start = 0
        $length = $total
        $status = '200 OK'
        $contentRange = ''

        if ($rangeHeader -and $rangeHeader -match '(?i)bytes=(\d*)-(\d*)') {
            $fromText = $Matches[1]
            $toText = $Matches[2]
            if ($fromText -ne '') {
                $start = [int]$fromText
                $length = if ($toText -ne '') { ([int]$toText - $start + 1) } else { ($total - $start) }
            } elseif ($toText -ne '') {
                # suffix range: last N bytes
                $length = [int]$toText
                $start = [Math]::Max(0, $total - $length)
            }
            if ($start -ge $total) { $start = 0; $length = $total }
            if (($start + $length) -gt $total) { $length = $total - $start }
            $status = '206 Partial Content'
            $contentRange = "Content-Range: bytes $start-$($start + $length - 1)/$total`r`n"
        }

        $respHead = "HTTP/1.1 $status`r`nContent-Type: $contentType`r`nContent-Length: $length`r`nAccept-Ranges: bytes`r`n${contentRange}Connection: close`r`n`r`n"
        # Log the FULL request target, query included: test pages report their diagnostics by
        # requesting a uniquely named resource, and the name is the message.
        Write-Log "$remote $method $rawPath -> $status (range=$($rangeHeader ? $rangeHeader : '-') bytes=$start+$length/$total)"

        $hb = [Text.Encoding]::ASCII.GetBytes($respHead)
        $stream.Write($hb, 0, $hb.Length)
        if ($method -ne 'HEAD') { $stream.Write($body, $start, $length) }
        $stream.Flush(); $stream.Close(); $client.Close()
    }
    catch {
        Write-Log "err: $($_.Exception.Message)"
        try { if ($client) { $client.Close() } } catch { }
    }
}
