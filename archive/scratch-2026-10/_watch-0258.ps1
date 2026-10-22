[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"
Get-ChildItem Env: | Where-Object { $_.Name -match "(?i)proxy" } | ForEach-Object { Remove-Item ("Env:" + $_.Name) }
$fn = "EdgeHTMLReborn.Harness_0.2.5.8_arm__x0rqga78m2mgc"
for($i=0;$i -lt 720;$i++){
  if(Test-Connection 192.168.3.159 -Count 1 -Quiet){
    try {
      $h=[System.Net.Http.HttpClientHandler]::new(); $h.ServerCertificateCustomValidationCallback=[System.Net.Http.HttpClientHandler]::DangerousAcceptAnyServerCertificateValidator
      $cli=[System.Net.Http.HttpClient]::new($h); $cli.Timeout=[TimeSpan]::FromSeconds(120)
      $d = $cli.GetAsync("https://192.168.3.159/api/debug/dump/usermode/dumps").Result.Content.ReadAsStringAsync().Result
      $m = [regex]::Matches($d, '"FileName" : "(Harness.exe.\d+.dmp)"[^}]*0\.2\.5\.8')
      foreach($mm in $m){
        $name = $mm.Groups[1].Value
        $out = "E:\Apotheosis\crash\" + $name
        if(-not (Test-Path $out)){
          $r = $cli.GetAsync("https://192.168.3.159/api/debug/dump/usermode/crashdump?packageFullName=$fn&fileName=$name").Result
          [IO.File]::WriteAllBytes($out, $r.Content.ReadAsByteArrayAsync().Result)
          "got $name" | Out-File -Append E:\Apotheosis\crash\watch-0258.log
        }
      }
    } catch {}
  }
  Start-Sleep 20
}
