// PortStorage.h — 0.2.0 的 Web Platform 存储/套接字 provider 接线。
//
// 一句话:localStorage / sessionStorage / IndexedDB / WebSocket 这四样,WebCore 里的实现
// (Storage / StorageArea / IDB 客户端与服务端 / WebSocketChannel 协议层)本来就编在
// WebCore.lib 里,缺的一直是 Page 上的四个 provider —— 0.1.9 之前它们全是
// pageConfigurationWithEmptyClients 给的 Empty/Dummy 版:
//   · storageNamespaceProvider = nullptr        → window.localStorage 恒空
//   · databaseProvider         = nullptr        → indexedDB.open() 拿不到连接
//   · socketProvider           = EmptySocketProvider → new WebSocket() 得到 null channel
// 本文件把它们换成真的:前两个复用 WebKitLegacy 的单进程实现(StorageNamespaceImpl /
// InProcessIDBServer —— 单进程架构正是它们的目标场景,不需要造 IPC),第三个用
// WebKitLegacy 的 WebSocketChannel + port/PortSocketStreamHandle.cpp 的 curl 字节流。
#pragma once

#include <wtf/Forward.h>
#include <wtf/Ref.h>
#include <wtf/text/WTFString.h>

namespace WebCore {
class DatabaseProvider;
class SocketProvider;
class StorageNamespaceProvider;
}

namespace WebCorePort {

// harness 在引擎线程首个任务前注入 profile 根目录(LocalState\profile)。空 = 全部退化为
// 进程内非持久(不崩,只是关掉应用就没了)。
void setPortProfilePath(const WTF::String& path);

// profile 子目录。目录不存在时会被建出来;拿不到可写路径时返回空串。
WTF::String portLocalStorageDirectory();   // <profile>\storage
WTF::String portIndexedDBDirectory();      // <profile>\indexeddb

// 三个 provider。都可以在没有可写路径时安全降级(见各自实现里的说明)。
WTF::Ref<WebCore::StorageNamespaceProvider> makeStorageNamespaceProvider();
WTF::Ref<WebCore::DatabaseProvider> makeDatabaseProvider();
WTF::Ref<WebCore::SocketProvider> makeSocketProvider();

// localStorage 落盘。StorageAreaImpl 平时是攒够一批再异步刷,应用被 UWP 挂起时不刷就会丢。
// harness 在 Suspending 时调(和 flushCookiesToDisk 同一处)。
void flushLocalStorage();

// 诊断:把存储子系统的实际状态(路径、是否真的持久化、SQLite 可用性)写成一行给 diag 通道。
WTF::String portStorageStatusLine();

} // namespace WebCorePort
