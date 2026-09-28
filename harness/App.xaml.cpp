#include "pch.h"
#include "App.xaml.h"
#include "MainPage.xaml.h"
#if !defined(APOTHEOSIS_XAML_CODEGEN)
#include "App.g.hpp"
#endif
#include "WebCoreDriver.h"   // Apotheosis: WebCoreCrashNote — harness-side crash.txt reasons
#include <exception>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace Harness;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;

// Apotheosis (2026-09-03): the harness dies on its own CRT, not the engine's. The driver
// installs terminate/new/invalid-parameter/purecall handlers for the clang-cl DLL; those
// never fire for a C++/CX exception escaping the UI thread, nor for the std::terminate
// that ANGLE's RunOnUIThread raises when it times out marshalling a swapchain
// create/resize back to the panel dispatcher (the port's single-engine-thread rule). Both of
// those are exactly what a pinch-zoom-at-770 MB abort looks like from outside, so name
// them in crash.txt before falling through to the abort the CRT was going to do anyway.
static void __cdecl HarnessTerminateHandler()
{
    // Ordered by likelihood on this port: an ANGLE RunOnUIThread timeout, or an uncaught
    // Platform::Exception. Exceptions are ON in the harness, so try to describe one.
    char reason[512];
    const char* detail = "no exception object";
    std::string owned;
    try {
        if (std::current_exception()) {
            detail = "uncaught exception (unknown type)";
            try {
                std::rethrow_exception(std::current_exception());
            } catch (Platform::Exception^ ex) {
                char buf[320];
                std::string msg;
                if (ex->Message) {
                    for (unsigned i = 0; i < ex->Message->Length(); ++i) {
                        wchar_t c = ex->Message->Data()[i];
                        msg.push_back(c < 128 ? static_cast<char>(c) : '?');
                    }
                }
                std::snprintf(buf, sizeof buf, "Platform::Exception hr=0x%08lx %s",
                    static_cast<unsigned long>(ex->HResult), msg.c_str());
                owned = buf;
                detail = owned.c_str();
            } catch (const std::exception& e) {
                owned = std::string("std::exception ") + (e.what() ? e.what() : "");
                detail = owned.c_str();
            } catch (...) {
            }
        }
    } catch (...) {
    }
    std::snprintf(reason, sizeof reason,
        "harness terminate (RunOnUIThread timeout or uncaught throw on the UI thread): %s", detail);
    WebCoreCrashNote(reason);
    std::abort();   // do not swallow: the CRT's own terminate would have aborted here
}

App::App()
{
    // Install before InitializeComponent: XAML type registration itself can throw.
    std::set_terminate(&HarnessTerminateHandler);
    UnhandledException += ref new UnhandledExceptionEventHandler(this, &App::OnUnhandledException);
    InitializeComponent();
    Suspending += ref new Windows::UI::Xaml::SuspendingEventHandler(this, &App::OnSuspending);
    // Apotheosis (2026-09-27): the other half of a deferral that was never answered before
    // the freeze - see OnResuming.
    Resuming += ref new Windows::Foundation::EventHandler<Platform::Object^>(this, &App::OnResuming);
}

// Apotheosis: XAML swallows exceptions that escape a dispatched handler into this event
// and then tears the app down with no dump. Record the HRESULT and message; leave
// e->Handled false so the shutdown behaviour is unchanged (we observe, never swallow).
void App::OnUnhandledException(Platform::Object^, Windows::UI::Xaml::UnhandledExceptionEventArgs^ e)
{
    try {
        std::string msg;
        if (e->Message) {
            for (unsigned i = 0; i < e->Message->Length(); ++i) {
                wchar_t c = e->Message->Data()[i];
                msg.push_back(c < 128 ? static_cast<char>(c) : '?');
            }
        }
        char reason[512];
        std::snprintf(reason, sizeof reason, "harness UnhandledException hr=0x%08lx %s",
            static_cast<unsigned long>(e->Exception.Value), msg.c_str());
        WebCoreCrashNote(reason);
    } catch (...) {}
}

// cookie JSON 落盘的真正触发点(见 App.xaml.h 注释)。拿 deferral,转给引擎线程串行写完再 Complete——
// deferral 是 agile 对象,Complete() 不需要转回 UI 线程调。MainPage::FlushCookiesForSuspend 里实现
// (WebEngine 队列是 MainPage.xaml.cpp 内部实现细节,没有跨 TU 头,故走页面方法转发)。
//
// Apotheosis (2026-09-27): the whole body is exception-guarded. Between GetDeferral() and
// FlushCookiesForSuspend() (which stores the deferral and starts its 2 s watchdog) a
// Platform::Exception - a XAML property write on a torn-down window, a failed ref new - would
// skip every one of those steps, leaving the deferral never Completed. PLM then kills the
// process with no dump, and App::UnhandledException cannot see a throw from OnSuspending
// (only from dispatched handlers): exactly the "kill without a dump" shape this app spends
// its diagnostics budget on. The catch page answer the shell immediately, which is safe: the
// cookie flush is best-effort and the next launch reloads what the previous one managed to
// write.
void App::OnSuspending(Platform::Object^, Windows::ApplicationModel::SuspendingEventArgs^ e)
{
    Windows::ApplicationModel::SuspendingDeferral^ deferral = nullptr;
    try {
        deferral = e->SuspendingOperation->GetDeferral();
        auto page = dynamic_cast<MainPage^>(Window::Current->Content);
        if (!page) {
            if (auto frame = dynamic_cast<Frame^>(Window::Current->Content))
                page = dynamic_cast<MainPage^>(frame->Content);
        }
        if (page)
            page->FlushCookiesForSuspend(deferral);
        else
            deferral->Complete();
    } catch (...) {
        // Best effort: if the throw happened before FlushCookiesForSuspend stored the deferral,
        // nothing else will ever Complete it - answer the shell here. If it stored it, the page's
        // watchdog owns it and this Complete() throws (already completed / mid-flight); that is
        // exactly why the nested try is here.
        if (deferral) {
            try { deferral->Complete(); } catch (...) {}
        }
    }
}

void App::OnResuming(Platform::Object^, Platform::Object^)
{
    try {
        auto page = dynamic_cast<MainPage^>(Window::Current->Content);
        if (!page) {
            if (auto frame = dynamic_cast<Frame^>(Window::Current->Content))
                page = dynamic_cast<MainPage^>(frame->Content);
        }
        if (page)
            page->CompleteSuspendDeferral();
    } catch (...) {}
}

void App::OnLaunched(LaunchActivatedEventArgs^ e)
{
#if defined(APOTHEOSIS_OFFICIAL_XAML)
    auto rootFrame = dynamic_cast<Frame^>(Window::Current->Content);
    if (rootFrame == nullptr) {
        rootFrame = ref new Frame();
        Window::Current->Content = rootFrame;
    }
    if (rootFrame->Content == nullptr)
        rootFrame->Navigate(Windows::UI::Xaml::Interop::TypeName(MainPage::typeid), e->Arguments);
#else
    (void)e;
    if (Window::Current->Content == nullptr)
        Window::Current->Content = ref new MainPage();
#endif
    Window::Current->Activate();
}
