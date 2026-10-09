#Requires -Version 7.0
<#
.SYNOPSIS
    Drive one <video>/<audio> test round against the phone over Windows Device Portal and
    print the evidence.

.DESCRIPTION
    The whole loop for a media round is: point the app at a page, restart it so
    LocalState\testurl.txt is read, wait, then read back the two places where the truth
    lands - the LAN test server's request log (the page is served by
    tools\Test-MediaServer.ps1, which the page can report into by requesting a uniquely
    named resource) and LocalState\stage.txt (the engine's own `media ...` diagnostic).

    Doing that by hand needs the WDP dance (CSRF token, multipart upload, terminate,
    launch) and the package identity, which changes with every version. This script
    discovers the installed package itself and does the rest.

    Prerequisites: tools\Test-MediaServer.ps1 running, and any small H.264/AAC mp4 at
    <server root>\test.mp4 (the server prints its root at startup).

.EXAMPLE
    pwsh -File tools\Test-Media.ps1                       # probe.html, the self-reporting page
    pwsh -File tools\Test-Media.ps1 -PageFactory          # the element-factory discriminator
    pwsh -File tools\Test-Media.ps1 -Path /video.html     # plain markup, no JS at all
    pwsh -File tools\Test-Media.ps1 -NoRestart            # just re-pull the evidence
#>
param(
    [string]$Ip = '192.168.3.159',
    [string]$Path = '/probe.html',
    [switch]$PageFactory,
    [string]$Url,
    [int]$Port = 8099,
    [int]$WaitSeconds = 45,
    [switch]$NoRestart,
    [string]$ServerLog = 'E:\Apotheosis\port\_mediatest\server.log',
    [string]$LocalFolder = 'LocalAppData'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http

# 1. Work out what to navigate to. The LAN address is derived, not assumed: the phone and
#    this PC are on the same subnet, and hardcoding a stale IP is how these rounds get
#    wasted.
if ($PageFactory) { $Path = '/factory.html' }
if (-not $Url) {
    $subnet = ($Ip -split '\.')[0..2] -join '.'
    $localIp = (Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -like "$subnet.*" -and $_.IPAddress -ne $Ip } |
        Select-Object -First 1).IPAddress
    if (-not $localIp) { throw "no local IPv4 on $subnet.0/24 to serve from (pass -Url)" }
    $Url = "http://${localIp}:${Port}${Path}"
}

# 2. WDP plumbing: HTTPS with a self-signed cert, no proxy (a local proxy silently breaks
#    device access), CSRF token via cookie -> header.
$handler = [System.Net.Http.HttpClientHandler]::new()
$handler.ServerCertificateCustomValidationCallback = [System.Net.Http.HttpClientHandler]::DangerousAcceptAnyServerCertificateValidator
$handler.CookieContainer = [System.Net.CookieContainer]::new()
$handler.UseProxy = $false
$client = [System.Net.Http.HttpClient]::new($handler)
$client.Timeout = [TimeSpan]::FromMinutes(5)
$base = "https://$Ip"
$script:Csrf = $null

function Sync-Csrf($response) {
    $cookies = $null
    if ($response -and $response.Headers.TryGetValues('Set-Cookie', [ref]$cookies)) {
        foreach ($line in $cookies) {
            if ($line -match 'CSRF-Token=([^;,\s]+)') {
                $script:Csrf = $Matches[1]
                try { $handler.CookieContainer.Add([Uri]$base, [System.Net.Cookie]::new('CSRF-Token', $script:Csrf, '/', $Ip)) } catch { }
            }
        }
    }
}

function Invoke-Wdp([string]$method, [string]$path, $content) {
    $request = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::new($method), "$base$path")
    if ($script:Csrf) { $request.Headers.Add('X-CSRF-Token', $script:Csrf) }
    if ($content) { $request.Content = $content }
    $response = $client.SendAsync($request).GetAwaiter().GetResult()
    Sync-Csrf $response
    return $response
}

Write-Host "target page : $Url"
Sync-Csrf ($client.GetAsync("$base/api/os/info").GetAwaiter().GetResult())

# 3. Discover the installed package instead of hardcoding a package full name (the version
#    inside it changes on every build).
$packages = (Invoke-Wdp GET '/api/app/packagemanager/packages' $null).Content.ReadAsStringAsync().Result | ConvertFrom-Json
$package = $packages.InstalledPackages | Where-Object { $_.PackageFullName -match 'Harness' } | Select-Object -First 1
if (-not $package) { throw "no Harness package installed on $Ip" }
$pfn = $package.PackageFullName
# Derive the family name from the package full name, which is authoritative:
#   Name_Version_Arch__PublisherId  ->  Name_PublisherId
# The API's own PackageFamilyName field reported "EdgeHTMLReborn.Harness" (no publisher hash) on
# 15254, and using it made the launch call fail with 500, so it is only a fallback.
$parts = $pfn -split '_'
$family = if ($parts.Count -ge 5) { "$($parts[0])_$($parts[-1])" }
          elseif ($package.PackageFamilyName) { $package.PackageFamilyName }
          else { throw "cannot derive a package family name from '$pfn'" }
$aumid = "$family!App"
Write-Host "installed   : $pfn"
Write-Host "aumid       : $aumid"

if (-not $NoRestart) {
    # testurl.txt is read once at startup, so the app has to restart for it to take effect.
    $urlField = [System.Net.Http.MultipartFormDataContent]::new()
    $bytes = [System.Net.Http.ByteArrayContent]::new([System.Text.Encoding]::UTF8.GetBytes("$Url`n"))
    $bytes.Headers.ContentDisposition = [System.Net.Http.Headers.ContentDispositionHeaderValue]::new('form-data')
    $bytes.Headers.ContentDisposition.Name = '"testurl.txt"'
    $bytes.Headers.ContentDisposition.FileName = '"testurl.txt"'
    $bytes.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::new('application/octet-stream')
    $urlField.Add($bytes)
    $push = Invoke-Wdp POST "/api/filesystem/apps/file?knownfolderid=$LocalFolder&packagefullname=$([uri]::EscapeDataString($pfn))&path=%5CLocalState" $urlField
    Write-Host "push        : $([int]$push.StatusCode)"

    $pfnB64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($pfn))
    $aumidB64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($aumid))
    try { $null = Invoke-Wdp DELETE "/api/taskmanager/app?package=$([uri]::EscapeDataString($pfnB64))" $null } catch { }
    Start-Sleep -Seconds 3
    $launch = Invoke-Wdp POST "/api/taskmanager/app?appid=$([uri]::EscapeDataString($aumidB64))&package=$([uri]::EscapeDataString($pfnB64))" $null
    Write-Host "launch      : $([int]$launch.StatusCode); waiting ${WaitSeconds}s"
    Start-Sleep -Seconds $WaitSeconds
}

function Get-LocalState([string]$name) {
    $path = "/api/filesystem/apps/file?knownfolderid=$LocalFolder&packagefullname=$pfn&path=%5CLocalState&filename=$([uri]::EscapeDataString($name))"
    try { return (Invoke-WebRequest -Uri "$base$path" -NoProxy -SkipCertificateCheck -TimeoutSec 90 -UseBasicParsing).Content }
    catch { return "ERR: $($_.Exception.Message)" }
}

# 4. Evidence. The request log is the ground truth for "did anything try to fetch the media",
#    including the Range header (Media Foundation's own SourceReader does its own HTTP).
Write-Host "`n===== LAN server log, device requests ====="
if (Test-Path $ServerLog) {
    Get-Content $ServerLog | Where-Object { $_ -match [regex]::Escape($Ip) } | Select-Object -Last 15 | ForEach-Object { $_ }
} else {
    Write-Host "(no log at $ServerLog - is tools\Test-MediaServer.ps1 running?)"
}

Write-Host "`n===== stage.txt: media line(s) ====="
$stage = Get-LocalState 'stage.txt'
$media = ($stage -split "`n") | Where-Object { $_ -match '^\s*media ' }
if ($media) { $media | Select-Object -Last 3 | ForEach-Object { $_.TrimEnd() } } else { Write-Host "(no media line yet)" }
Write-Host "readers=0 means the engine was never asked to load anything; out=0x16 means the Video Processor returned RGB32."

Write-Host "`n===== stage.txt: last timeline ====="
($stage -split "`n") | Where-Object { $_ -match '^timeline ' } | Select-Object -Last 1 | ForEach-Object { $_.TrimEnd() }

Write-Host "`n===== console.txt: page self-reports ====="
$console = Get-LocalState 'console.txt'
$reports = ($console -split "`n") | Where-Object { $_ -match 'MEDIADIAG|PROBE' }
if ($reports) { $reports | Select-Object -Last 15 | ForEach-Object { $_.TrimEnd() } }
else { Write-Host "(none - console mirroring is unreliable for these pages; trust the server log and stage.txt)" }

Write-Host "`n===== crash.txt: new entries? ====="
$crash = Get-LocalState 'crash.txt'
$crashLines = ($crash -split "`n") | Where-Object { $_.Trim() }
if ($crashLines) { Write-Host "$($crashLines.Count) lines; last:"; $crashLines | Select-Object -Last 5 | ForEach-Object { $_.TrimEnd() } } else { Write-Host "(empty)" }
