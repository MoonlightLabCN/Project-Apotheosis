# _watch-playback.ps1 — 等用户醒来亮屏(app 恢复播放),把服务器日志里新的设备 val-t 事件抓下来。
# 0.2.5.35 轮(06:39)app 被息屏挂起在中途,播放确认缺一份"活着"的证据。
param([int]$WaitMinutes=240)
$log='E:\Apotheosis\port\_mediatest\server.log'
$out='E:\Apotheosis\port\_mediatest\playback-watch.txt'
function Say($m){ $line="[$(Get-Date -Format 'HH:mm:ss')] $m"; Write-Host $line; Add-Content -Path $out -Value $line }
"===== playback watch start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') =====" | Set-Content $out
$base = (Get-Content $log | Measure-Object -Line).Lines
Say "baseline log lines: $base (watching for NEW device requests, up to ${WaitMinutes} min)"
for($i=0;$i -lt ($WaitMinutes*2);$i++){
  Start-Sleep -Seconds 30
  $now = (Get-Content $log | Measure-Object -Line).Lines
  if($now -gt $base){
    $new = Get-Content $log | Select-Object -Skip $base | Where-Object { $_ -match '192\.168\.3\.159' }
    if($new){
      Say "NEW device requests ($($new.Count) lines):"
      $new | ForEach-Object { Say "  $_" }
      $base = $now
      # 看到 playing/t12 之类就说明这条线闭环了,继续看到 ended 或够 20 行再退
      if($new -match 'evt-playing|t12|evt-ended'){
        Say 'PLAYBACK CONFIRMED (playing/t12 seen)'
        # 设备在线就把 stage.txt / mediaaudio.txt 也抓回来(音频端点探测的答案在里面)
        $ip='192.168.3.159'; $wbase="https://${ip}:443"; $jar="$env:TEMP\wdp-jar.txt"
        try{
          Remove-Item $jar -ErrorAction SilentlyContinue
          & curl.exe -sk --max-time 10 -c $jar -o NUL "$wbase/api/os/info" 2>$null
          $pkgs=(& curl.exe -sk --max-time 15 -b $jar "$wbase/api/app/packagemanager/packages" 2>$null)|ConvertFrom-Json
          $pkg=$pkgs.InstalledPackages | Where-Object { $_.PackageFullName -match 'Harness' } | Sort-Object { $v=($_.PackageFullName -split '_')[1]; $p=$v -split '\.'; [int]$p[0]*1000000+[int]$p[1]*10000+[int]$p[2]*100+[int]$p[3] } -Descending | Select-Object -First 1
          if($pkg){
            $pfnE=[uri]::EscapeDataString($pkg.PackageFullName)
            $st=(& curl.exe -sk --max-time 20 -b $jar "$wbase/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState&filename=stage.txt" 2>$null)
            Say '--- stage.txt media/media-state lines ---'
            ($st -split "`n") | Where-Object { $_ -match '^\s*media |media-state' } | Select-Object -Last 3 | ForEach-Object { Say "  $_" }
            $ma=(& curl.exe -sk --max-time 20 -b $jar "$wbase/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfnE&path=%5CLocalState&filename=mediaaudio.txt" 2>$null)
            Say '--- mediaaudio.txt (endpoint probe / idle-stop / quanta) ---'
            ($ma -split "`n") | Where-Object { $_ -notmatch 'quantum #' } | Select-Object -Last 12 | ForEach-Object { Say "  $_" }
            $qm=($ma -split "`n") | Where-Object { $_ -match 'quantum #' }
            if($qm){ Say "  quantum lines: $($qm.Count); last: $($qm[-1])" }
          }
        }catch{ Say "  (evidence pull failed: $($_.Exception.Message))" }
        # 再多看 60 秒收尾
        Start-Sleep -Seconds 60
        $more = Get-Content $log | Select-Object -Skip $base | Where-Object { $_ -match '192\.168\.3\.159' }
        $more | ForEach-Object { Say "  $_" }
        break
      }
    } else { $base = $now }
  }
}
Say 'watch done'
