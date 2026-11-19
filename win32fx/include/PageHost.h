//
// PageHost.h - a window that hosts pages, and the page base (spec #8 4, 5;
// plan S8-M0 task 2).
//
// A page is a child window the host shows, hides and closes: a report
// preview, the voucher list, and from S8-M1 every redesigned screen. Code
// that opens one (RunReport, OpenVoucherBrowser) takes a PageHost& rather
// than a particular frame, so the same page opens in the workspace's tabs
// (Workspace.h) or in an MDI frame (MdiFrame.h: the modal preview window
// and the example program still use one).
//
// HostedPage replaces MdiChild (kept as an alias): CreatePage() asks the
// host to make the window, ClosePage() and SetTitle() go through the host,
// and host()->hostWindow() is the owner for the dialogs a page opens. Under
// an MDI host the page's default procedure is DefMDIChildProc, as before.
//
#pragma once

#include "Window.h"

namespace win32fx {

class HostedPage;

class PageHost {
public:
    virtual ~PageHost() = default;

    // The top-level window: the owner for dialogs and message boxes.
    virtual HWND hostWindow() const noexcept = 0;
    virtual HINSTANCE hostInstance() const noexcept = 0;

    // Registers `className` (once per process) and creates `page` in the
    // host with `title`, active. Null on failure.
    virtual HWND CreatePage(HostedPage& page, const wchar_t* className, const wchar_t* title, HICON icon) = 0;

    virtual HWND activePage() const noexcept = 0;
    virtual void ActivatePage(HWND page) = 0;
    virtual void ClosePage(HWND page) = 0;   // destroys it
    virtual void SetPageTitle(HWND page, const wchar_t* title) = 0;

    // Called when a page becomes active, and with null when the last one goes.
    virtual void OnPageActivate(HWND /*page*/) {}

    // True when pages are MDI children (DefMDIChildProc, WM_MDIACTIVATE).
    virtual bool PagesAreMdi() const noexcept { return false; }
};

class HostedPage : public Window {
public:
    HWND CreatePage(PageHost& host, const wchar_t* className, const wchar_t* title, HICON icon = nullptr);

    PageHost* host() const noexcept { return host_; }
    void ClosePage();
    void SetTitle(const wchar_t* title);

    // The command that opens this page again (0: none, it can't be pinned),
    // and whether its tab is pinned: reopened when the program starts.
    WORD commandId = 0;
    bool pinned = false;

protected:
    LRESULT DefaultProc(UINT msg, WPARAM wp, LPARAM lp) override;
    // Under an MDI host, reports WM_MDIACTIVATE to it; an override must call this first.
    bool OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override;

private:
    friend class MdiFrame;
    friend class Workspace;
    PageHost* host_ = nullptr;
};

using MdiChild = HostedPage;   // the Spec #1 name

// Registers a page or frame class once per process: Window::WndProc, `icon`, `background`.
bool EnsureWindowClass(HINSTANCE inst, const wchar_t* className, HICON icon, HBRUSH background);

} // namespace win32fx
