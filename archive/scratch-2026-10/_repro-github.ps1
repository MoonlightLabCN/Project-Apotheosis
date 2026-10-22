[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"
Get-ChildItem Env: | Where-Object { $_.Name -match "(?i)proxy" } | ForEach-Object { Remove-Item ("Env:" + $_.Name) }
$cli = $null
function New-Cli { $h=[System.Net.Http.HttpClientHandler]::new(); $h.ServerCertificateCustomValidationCallback=[System.Net.Http.HttpClientHandler]::DangerousAcceptAnyServerCertificateValidator; $h.CookieContainer=[System.Net.CookieContainer]::new(); $script:cli=[System.Net.Http.HttpClient]::new($h); $script:cli.Timeout=[TimeSpan]::FromSeconds(120) }
for($i=0;$i -lt 480;$i++){
  if(Test-Connection 192.168.3.159 -Count 2 -Quiet){
    try {
      New-Cli
      $null = $cli.GetAsync("https://192.168.3.159/api/os/info").Result
      $aid=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes("EdgeHTMLReborn.Harness_x0rqga78m2mgc!App"))
      $pkg=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes("EdgeHTMLReborn.Harness_0.2.5.6_arm__x0rqga78m2mgc"))
      $null = $cli.DeleteAsync("https://192.168.3.159/api/taskmanager/app?package=" + [uri]::EscapeDataString($pkg)).Result
      Start-Sleep 3
      $null = $cli.PostAsync("https://192.168.3.159/api/taskmanager/app?appid=" + [uri]::EscapeDataString($aid) + "&package=" + [uri]::EscapeDataString($pkg), $null).Result
      "relaunched at $(Get-Date -Format HH:mm:ss)" | Out-File -Append E:\Apotheosis\crash\repro-github.log
      break
    } catch { }
  }
  Start-Sleep 10
}
Start-Sleep 90
for($i=0;$i -lt 240;$i++){
  try {
    New-Cli
    $d = $cli.GetAsync("https://192.168.3.159/api/debug/dump/usermode/dumps").Result.Content.ReadAsStringAsync().Result
    if($d -match "0.2.5.6"){ $d | Out-File E:\Apotheosis\crash\repro-github.log -Append; "DUMP-FOUND" | Out-File E:\Apotheosis\crash\repro-github.log -Append; break }
  } catch {}
  Start-Sleep 10
}
