# link-driver-gpu.ps1 — 同 link-driver-jit.ps1,但针对 build-clang-gpu(TextureMapper+ANGLE+JIT)编+链。
# 产出 WebCoreDriver-gpu.lib(供 GPU harness 链接)。链接需带预编译 ANGLE 导入库(GPU WebCore.lib 引用 ANGLE::EGL/GLES)。
$ErrorActionPreference = 'Stop'
. 'E:\Apotheosis\port\arm32-uwp-env.ps1' *> $null
$P = 'E:\Apotheosis\port'

$srcs = @(
  'WebCoreDriver','PortPlatformStrategies','LoadingFrameLoaderClient','PortNetworkStorageSession','PortChromeClient','PortWebSocket','PortUIBridge','PortSQLiteAppContainer','PortStorage','PortSocketStreamHandle','PortAudioOutput','PortMediaAudio',
  'webcore-driver-stubs','stubs-crypto','stubs-pasteboard','stubs-network','stubs-ax','stubs-other','stubs-loader'
)
foreach ($s in $srcs) {
    if (-not (Test-Path "$P\$s.cpp")) { Write-Host "缺源 $s.cpp" -ForegroundColor Yellow; continue }
    & pwsh -NoProfile -File "$P\compile-driver-gpu.ps1" "$P\$s.cpp" "$P\$s.gpu.obj" | Out-Null
    if (-not (Test-Path "$P\$s.gpu.obj") -or (Get-Item "$P\$s.gpu.obj").LastWriteTime -lt (Get-Item "$P\$s.cpp").LastWriteTime) {
        Write-Host "!! $s.cpp(GPU)编译失败,看 driver-compile-gpu.log" -ForegroundColor Red
        Get-Content "$P\driver-compile-gpu.log" -Tail 25; exit 1
    }
}
$objs = $srcs | ForEach-Object { "$P\$_.gpu.obj" }

$lld = 'C:\Program Files\LLVM\bin\lld-link.exe'
$libs = @(
  'WebCore.lib','JavaScriptCore.lib','PAL.lib','WTF.lib','JavaScriptCore.lib','WTF.lib'
  'libEGL.lib','libGLESv2.lib'
  'libcurl.lib','libssl.lib','libcrypto.lib'
  'cairo.lib','pixman-1.lib','freetype.lib','fontconfig.lib','libexpat.lib','harfbuzz.lib'
  'jpeg.lib','libpng16.lib','libwebp.lib','libwebpdemux.lib','libsharpyuv.lib'
  'libxml2.lib','sqlite3.lib','z.lib','bz2.lib','brotlidec.lib','brotlicommon.lib'
  'icuuc.lib','icuin.lib','icudt.lib','WindowsApp.lib'
  # Apotheosis (media-mf): the MF_MT_*/MFMediaType_*/MFVideoFormat_*/MFAudioFormat_* symbols
  # are EXTERN_GUID *data* and live in mfuuid.lib; the MF entry points themselves come from
  # WindowsApp.lib. Listing mfplat/mfreadwrite too costs nothing and makes a missing MF
  # dependency fail here rather than in the harness link.
  'mfuuid.lib','mfplat.lib','mfreadwrite.lib'
  # stubs-network.cpp 的 NetworkStateNotifier::updateStateWithoutNotifying 不再恒 true,
  # 改用 InternetGetConnectedState 查真实在线态(wininet 在 App partition,清单已带 internetClient)。
  'wininet.lib'
)
$log = "$P\link-driver-gpu.log"
& $lld /DLL /MACHINE:ARM /OUT:"$P\WebCoreDriver-gpu.dll" `
    $objs `
    /LIBPATH:"E:\Apotheosis\build-clang-gpu\lib" `
    /LIBPATH:"E:\Apotheosis\angle\arm" `
    /LIBPATH:"C:\vcpkg\installed\arm-uwp\lib" `
    /LIBPATH:"C:\icu-arm-uwp\lib" `
    $libs `
    /INCLUDE:WebCoreRenderHtml /EXPORT:WebCoreRenderHtml /EXPORT:WebCoreLoadUrl `
    /OPT:REF /OPT:NOICF /INCREMENTAL:NO /errorlimit:0 `
    *> $log
$code = $LASTEXITCODE
$unres = @(Select-String -Path $log -Pattern 'error: undefined symbol:')
$dup   = @(Select-String -Path $log -Pattern 'duplicate symbol|already defined')
Write-Host "[link-driver-gpu] EXIT=$code  undefined=$($unres.Count)  duplicate=$($dup.Count)  log=$log"
if ($code -eq 0) {
  & 'C:\Program Files\LLVM\bin\llvm-lib.exe' /OUT:"$P\WebCoreDriver-gpu.lib" $objs | Out-Null
  Write-Host "🎉 GPU 驱动链接成功! WebCoreDriver-gpu.lib 归档($($objs.Count) obj)" -ForegroundColor Green
}
else {
  if ($dup.Count) { Write-Host "--- 重复符号 ---"; $dup | ForEach-Object { ($_.Line -replace '.*(duplicate symbol|already defined):? ?','').Trim() } | Sort-Object -Unique | Select-Object -First 30 }
  if ($unres.Count) { Write-Host "--- 未定义符号 ---"; $unres | ForEach-Object { ($_.Line -replace '.*error: undefined symbol: ','').Trim() } | Sort-Object -Unique | Select-Object -First 40 }
}
