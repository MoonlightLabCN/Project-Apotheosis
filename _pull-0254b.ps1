[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"
Get-ChildItem Env: | Where-Object { $_.Name -match "(?i)proxy" } | ForEach-Object { Remove-Item ("Env:" + $_.Name) }
for($i=0;$i -lt 720;$i++){
  if(Test-Connection 192.168.3.159 -Count 2 -Quiet){
    $out = pwsh -NoProfile -File E:\Apotheosis\tools\pull-file.ps1 -Ip 192.168.3.159 -Ver 0.2.5.4 -File autodump.txt 2>&1 | Out-String
    $segs = ([regex]::Matches($out,"########## URL:")).Count
    if($segs -ge 4){ [IO.File]::WriteAllText("E:\Apotheosis\crash\autodump-0254.txt", $out)
      pwsh -NoProfile -File E:\Apotheosis\tools\pull-file.ps1 -Ip 192.168.3.159 -Ver 0.2.5.4 -File console.txt *> E:\Apotheosis\crash\console-0254.txt
      "PULL-DONE" | Out-File E:\Apotheosis\crash\pull-0254.flag; break }
  }
  Start-Sleep 15
}
