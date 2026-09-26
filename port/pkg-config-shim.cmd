@echo off
rem pkg-config-shim.cmd —— 供 CMake FindPkgConfig 调用的垫片。
rem
rem 为什么需要:CMake 的 _pkg_set_path_internal 会把 CMAKE_PREFIX_PATH 派生的
rem "<prefix>\lib\pkgconfig" 用 file(TO_NATIVE_PATH) 转成反斜杠形态注入
rem PKG_CONFIG_PATH;而 msys2 的 pkg-config 按冒号切分该变量 —— "C:\..." 里的
rem "C:" 被当成两段,盘符丢失,得到 "\vcpkg\installed\arm-uwp\lib\pkgconfig"。
rem pkg-config 随后以该无盘符路径做前缀重定位,吐出
rem "\vcpkg\...\lib\pkgconfig/../../lib",CMake 解析列表时把 \v 当转义符,
rem 直接 FATAL "Invalid character escape '\v'"。
rem
rem 垫片强制把 PKG_CONFIG_PATH 置为单条 POSIX 形态(无冒号可切),查询结果里的
rem 路径也是 POSIX 形态(无反斜杠转义),CMake 解析安全;实际库/头文件的定位由
rem 各 find 模块经 CMAKE_PREFIX_PATH 兜底完成(与 2.52.4 本地构建的实际发现路径
rem 一致)。查询类信息(版本号等)照常可用。
set "PKG_CONFIG_PATH=/c/vcpkg/installed/arm-uwp/lib/pkgconfig"
"C:\vcpkg\downloads\tools\msys2\19f68f647e350a23\usr\bin\pkg-config.exe" %*
