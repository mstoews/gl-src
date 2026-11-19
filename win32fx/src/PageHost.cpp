//
// PageHost.cpp - see PageHost.h.
//
#include "PageHost.h"

namespace win32fx {

bool EnsureWindowClass(HINSTANCE inst, const wchar_t* className, HICON icon, HBRUSH background)
{
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof existing;
    if (GetClassInfoExW(inst, className, &existing)) return true;
    WNDCLASSEXW wc = Window::ClassInfo(inst, className);
    wc.hIcon = icon;
    wc.hIconSm = icon;
    wc.hbrBackground = background;
    return RegisterClassExW(&wc) != 0;
}

HWND HostedPage::CreatePage(PageHost& host, const wchar_t* className, const wchar_t* title, HICON icon)
{
    host_ = &host;
    return host.CreatePage(*this, className, title, icon);
}

void HostedPage::ClosePage()
{
    if (!hwnd()) return;
    if (host_) host_->ClosePage(hwnd());
    else DestroyWindow(hwnd());
}

void HostedPage::SetTitle(const wchar_t* title)
{
    if (!hwnd()) return;
    if (host_) host_->SetPageTitle(hwnd(), title);
    else SetWindowTextW(hwnd(), title);
}

bool HostedPage::OnMessage(UINT msg, WPARAM, LPARAM lp, LRESULT&)
{
    if (msg == WM_MDIACTIVATE && host_ && host_->PagesAreMdi()) {
        const HWND activated = reinterpret_cast<HWND>(lp);
        if (activated == hwnd()) host_->OnPageActivate(activated);
        else if (!activated) host_->OnPageActivate(nullptr);   // the last child is going
    }
    return false;   // the default procedure too
}

LRESULT HostedPage::DefaultProc(UINT msg, WPARAM wp, LPARAM lp)
{
    if (host_ && host_->PagesAreMdi()) return DefMDIChildProcW(hwnd(), msg, wp, lp);
    return DefWindowProcW(hwnd(), msg, wp, lp);
}

} // namespace win32fx
