// PortSQLiteAppContainer.cpp — 见 .h。用 SQLite 公开的 xSetSystemCall 把 App Container 里
// 被编成空指针的几个 win32 系统调用换成可用的等价实现。

#include "config.h"
#include "PortSQLiteAppContainer.h"

#include <sqlite3.h>
#include <windows.h>

#include <cstring>
#include <string>

namespace WebCorePort {

namespace {

// --- CreateFileW ----------------------------------------------------------
// SQLite 的调用形态(os_win.c winOpen):
//   osCreateFileW(zConverted, dwDesiredAccess, dwShareMode, NULL,
//                 dwCreationDisposition, dwFlagsAndAttributes, NULL)
// CreateFile2 把最后那个 DWORD 拆成三段,拆法见 MSDN CREATEFILE2_EXTENDED_PARAMETERS:
//   低 16 位 = 文件属性(FILE_ATTRIBUTE_*),0x000F0000 = SQOS,高 12 位 = 文件标志(FILE_FLAG_*)。
HANDLE WINAPI apoCreateFileW(LPCWSTR fileName, DWORD desiredAccess, DWORD shareMode,
    LPSECURITY_ATTRIBUTES securityAttributes, DWORD creationDisposition,
    DWORD flagsAndAttributes, HANDLE templateFile)
{
    // SQLite 从不传这两个;真传了也没法转交给 CreateFile2,宁可失败也不悄悄忽略。
    if (templateFile) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return INVALID_HANDLE_VALUE;
    }

    CREATEFILE2_EXTENDED_PARAMETERS params;
    ZeroMemory(&params, sizeof params);
    params.dwSize = sizeof params;
    params.dwFileAttributes = flagsAndAttributes & 0x0000FFFF;
    params.dwSecurityQosFlags = flagsAndAttributes & 0x000F0000;
    params.dwFileFlags = flagsAndAttributes & 0xFFF00000;
    params.lpSecurityAttributes = securityAttributes;

    return CreateFile2(fileName, desiredAccess, shareMode, creationDisposition, &params);
}

// --- CreateFileMappingW ---------------------------------------------------
// SQLite 只用它做只读/读写的整文件映射(winMapfile),名字参数永远是 NULL。
HANDLE WINAPI apoCreateFileMappingW(HANDLE file, LPSECURITY_ATTRIBUTES securityAttributes,
    DWORD protect, DWORD maximumSizeHigh, DWORD maximumSizeLow, LPCWSTR name)
{
    // 命名映射对象在 App Container 里需要能力声明,SQLite 也用不到 —— 直接拒绝而不是装作成功。
    if (name) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return nullptr;
    }
    ULONG64 maximumSize = (static_cast<ULONG64>(maximumSizeHigh) << 32) | maximumSizeLow;
    return CreateFileMappingFromApp(file, securityAttributes, protect, maximumSize, nullptr);
}

// --- MapViewOfFile --------------------------------------------------------
LPVOID WINAPI apoMapViewOfFile(HANDLE fileMapping, DWORD desiredAccess,
    DWORD fileOffsetHigh, DWORD fileOffsetLow, SIZE_T numberOfBytesToMap)
{
    ULONG64 offset = (static_cast<ULONG64>(fileOffsetHigh) << 32) | fileOffsetLow;
    return MapViewOfFileFromApp(fileMapping, desiredAccess, offset, numberOfBytesToMap);
}

// --- GetVersionExW --------------------------------------------------------
// 默认构建里 SQLITE_WIN32_GETVERSIONEX=0,winIsNT() 直接返回 1,这一项永远不会被调用。
// 但它在表里且可能为空,补一个如实回答"NT 系(Windows 10)"的实现,免得万一被调到就是空指针。
BOOL WINAPI apoGetVersionExW(LPOSVERSIONINFOW info)
{
    if (!info || info->dwOSVersionInfoSize < sizeof(OSVERSIONINFOW)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    info->dwMajorVersion = 10;
    info->dwMinorVersion = 0;
    info->dwBuildNumber = 0;
    info->dwPlatformId = VER_PLATFORM_WIN32_NT;
    info->szCSDVersion[0] = L'\0';
    return TRUE;
}

struct Replacement {
    const char* name;
    sqlite3_syscall_ptr function;
};

const Replacement kReplacements[] = {
    { "CreateFileW", reinterpret_cast<sqlite3_syscall_ptr>(apoCreateFileW) },
    { "CreateFileMappingW", reinterpret_cast<sqlite3_syscall_ptr>(apoCreateFileMappingW) },
    { "MapViewOfFile", reinterpret_cast<sqlite3_syscall_ptr>(apoMapViewOfFile) },
    { "GetVersionExW", reinterpret_cast<sqlite3_syscall_ptr>(apoGetVersionExW) },
};

// 打开真实文件这条路上真正必须非空的项。少一个就不敢把库指向磁盘。
// (winOpen 的主干:转路径 → 打开 → 读写 → 定位 → 取属性 → 加解锁 → 刷盘 → 关闭 → 删除。
//  内存映射不在此列:SQLite 的 mmap 是可关的优化,winMapfile 失败会静默退回普通读写。)
const char* const kRequired[] = {
    "CreateFileW",
    "CloseHandle",
    "ReadFile",
    "WriteFile",
    "SetFilePointer",
    "SetEndOfFile",
    "FlushFileBuffers",
    "GetFileAttributesW",
    "GetFileAttributesExW",
    "GetFullPathNameW",
    "DeleteFileW",
    "LockFile",
    "UnlockFile",
    "LockFileEx",
    "UnlockFileEx",
    "GetLastError",
    "Sleep",
};

bool g_installed = false;
bool g_fileIOAvailable = false;
std::string* g_missing = nullptr;

} // namespace

void installSQLiteAppContainerSyscalls()
{
    if (g_installed)
        return;
    g_installed = true;

    if (!g_missing)
        g_missing = new std::string();

    // 名字传 nullptr = 默认 VFS。这一步内部会 sqlite3_initialize(),因此必须在任何 open 之前。
    sqlite3_vfs* vfs = sqlite3_vfs_find(nullptr);
    if (!vfs || vfs->iVersion < 3 || !vfs->xSetSystemCall || !vfs->xGetSystemCall) {
        // iVersion < 3 的 VFS 没有 xSetSystemCall —— 修不了,老老实实报告不可用。
        *g_missing = "vfs-has-no-xSetSystemCall";
        g_fileIOAvailable = false;
        return;
    }

    for (const auto& replacement : kReplacements) {
        // 只补空的。原本就有实现的项不动 —— 我们的替身没有理由比 SQLite 自己的更好。
        if (vfs->xGetSystemCall(vfs, replacement.name))
            continue;
        vfs->xSetSystemCall(vfs, replacement.name, replacement.function);
    }

    for (const char* name : kRequired) {
        if (vfs->xGetSystemCall(vfs, name))
            continue;
        if (!g_missing->empty())
            *g_missing += '\n';
        *g_missing += name;
    }

    g_fileIOAvailable = g_missing->empty();
}

bool sqliteFileIOAvailable()
{
    return g_fileIOAvailable;
}

const char* sqliteMissingSyscalls()
{
    return g_missing ? g_missing->c_str() : "";
}

} // namespace WebCorePort
