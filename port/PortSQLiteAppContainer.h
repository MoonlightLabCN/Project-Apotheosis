// PortSQLiteAppContainer.h — 让 SQLite 在这个 ARM32 UWP App Container 构建里能打开真实文件。
//
// 背景(2026-07-03 真机 dump,项目记忆 [[cookie-persistence]]):
//   0xC0000005 @ pc=0x00000000,lr = sqlite3_open 内部 blx r6(r6=0)
//     ← WebCore::SQLiteDatabase::open ← CookieJarDB::openDatabase ← …
//   只要打开非 ":memory:" 的库就崩。cookie 持久化因此在 0.1.8.8 被回退成内存 jar + 旁路 JSON
//   快照;localStorage / IndexedDB 想真正持久化,都卡在同一个坑上。
//
// 0.2.0 定位到根因(对 C:\vcpkg\installed\arm-uwp\lib\sqlite3.lib 做符号/字符串分析):
//   · 该 lib 的 aSyscall 表里存在名字字符串 "CreateFileW"、"CreateFileMappingW"、"MapViewOfFile",
//     但这三个 Win32 API 在整个 lib 的未定义符号表里**一个都没有**(有 CloseHandle / ReadFile /
//     WriteFile / LockFileEx / SetFilePointer / GetFileAttributesExW 等等几十个,唯独缺它们)。
//   · SQLite 的 os_win.c 对每个系统调用都写成 { "名字", (SYSCALL)函数或0, 0 };编进来的是名字,
//     函数指针为 0 时该项就是空。名字在、导入不在 ⇒ 这几项的指针被编成了 0。
//   · vcpkg 用 WINAPI_FAMILY_APP 编 sqlite3 时,winbase.h 把 CreateFileW/CreateFileMapping/
//     MapViewOfFile 挡在 WINAPI_PARTITION_DESKTOP 后面,于是落到 (SYSCALL)0 那一支;但它又没有
//     定义 SQLITE_OS_WINRT(表里没有 "CreateFile2" 这个名字),所以 winOpen() 仍然走
//     osCreateFileW(...) —— 正好就是 dump 里那个 r6=0 的间接调用。
//
// 解法不是打补丁改 SQLite,而是用它自己公开的 VFS 接口把这几项换掉:
//   sqlite3_vfs::xSetSystemCall(vfs, "CreateFileW", ptr)     (iVersion >= 3,os_win.c 实现)
// 我们用 App Container 允许的等价 API 实现它们(CreateFile2 / CreateFileMappingFromApp /
// MapViewOfFileFromApp —— 这三个恰好已经在该 lib 的导入表里,说明它们在本 SDK 下是可用的)。
//
// 只在原项为空时安装,原本非空的一律不动 —— 最坏情况是这个模块什么也没做。
#pragma once

namespace WebCorePort {

// 在任何 SQLite 数据库被打开之前调用一次(引擎线程,ensureWebCoreInitialized 内)。幂等。
void installSQLiteAppContainerSyscalls();

// installSQLiteAppContainerSyscalls() 之后为真,表示 SQLite 的 win32 VFS 里所有"打开/映射
// 真实文件"必需的系统调用项都已非空,可以安全地把库指向磁盘文件。为假时调用方必须退回
// ":memory:" —— 这是 cookie jar 在 0.1.8.8 之后一直在做的事,现在 storage / IDB 也照此判断。
bool sqliteFileIOAvailable();

// 诊断:返回安装后仍为空的系统调用名(以 '\n' 分隔),供真机 diag 通道打印。空串 = 全部就位。
const char* sqliteMissingSyscalls();

} // namespace WebCorePort
