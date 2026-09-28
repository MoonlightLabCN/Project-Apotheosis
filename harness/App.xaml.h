#pragma once
#include "App.g.h"

namespace Harness {
    ref class App sealed {
    public:
        App();
        virtual void OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs^ e) override;
    private:
        // UWP 真正的挂起信号(比 MainPage 的 Window::VisibilityChanged 更可靠):切后台到进程被冻结/
        // 可能被系统直接终止回收内存之间,系统只给几秒;VisibilityChanged 触发的 fire-and-forget
        // 异步落盘不保证能在冻结前跑完。用 deferral 明确"等我做完"。
        void OnSuspending(Platform::Object^ sender, Windows::ApplicationModel::SuspendingEventArgs^ e);

        // Apotheosis (2026-09-27): a suspend deferral that was never completed (the page was
        // frozen mid-flush before either the engine hop or the 2 s guard timer could answer)
        // is completed on resume instead - a process cannot be killed for a deferral it has
        // already resumed from, and if the shell does come back, holding the deferral serves
        // nobody. CompleteSuspendDeferral() is idempotent, so calling it with nothing pending
        // is free.
        void OnResuming(Platform::Object^ sender, Platform::Object^ e);

        // Apotheosis: XAML catches anything that escapes a dispatched handler here and then
        // tears the app down without a dump; log HRESULT + message to crash.txt so it stops
        // being indistinguishable from an OS memory kill. e->Handled stays false.
        void OnUnhandledException(Platform::Object^ sender, Windows::UI::Xaml::UnhandledExceptionEventArgs^ e);
    };
}
