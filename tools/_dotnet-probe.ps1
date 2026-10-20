Add-Type -AssemblyName System.Net.Http
Write-Host "after Add-Type, erroraction=$($ErrorActionPreference)"
try { $x = [System.Net.Http.CookieContainer]::new(); Write-Host "CookieContainer OK: $($x.GetType().FullName)" } catch { Write-Host "CC FAIL: $($_.Exception.Message)" }
try { $y = [System.Net.Http.HttpClientHandler]::new(); Write-Host "Handler OK" } catch { Write-Host "Handler FAIL: $($_.Exception.Message)" }
Write-Host "PSEdition=$($PSVersionTable.PSEdition) CLR=$($PSVersionTable.CLRVersion)"
$lcp = [System.Security.Principal.WindowsPrincipal]::new([System.Security.Principal.WindowsIdentity]::GetCurrent())
Write-Host "LanguageMode=$($ExecutionContext.SessionState.LanguageMode)"
