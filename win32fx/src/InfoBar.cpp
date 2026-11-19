//
// InfoBar.cpp - see InfoBar.h.
//
#include "InfoBar.h"

#include "Accessibility.h"

#include <CommCtrl.h>

#pragma comment(lib, "comctl32.lib")

namespace win32fx {

namespace {

constexpr wchar_t kClass[] = L"fxInfoBar";

// Fills and text, close to the Windows 11 InfoBar's (high contrast: system colours).
COLORREF fillOf(InfoKind k)
{
    switch (k) {
    case InfoKind::Success: return RGB(223, 246, 221);
    case InfoKind::Warning: return RGB(255, 244, 206);
    case InfoKind::Error: return RGB(253, 231, 233);
    default: return RGB(230, 241, 251);
    }
}

bool highContrast()
{
    HIGHCONTRASTW hc{sizeof hc};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
}

} // namespace

InfoBar::~InfoBar()
{
    if (font_) DeleteObject(font_);
}

HWND InfoBar::Create(HWND parent, HINSTANCE inst)
{
    WNDCLASSEXW existing{sizeof existing};
    if (!GetClassInfoExW(inst, kClass, &existing)) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = Proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
    }
    hwnd_ = CreateWindowExW(0, kClass, L"", WS_CHILD, 0, 0, 0, 0, parent, nullptr, inst, this);
    return hwnd_;
}

int InfoBar::Height() const
{
    if (!visible_ || !hwnd_) return 0;
    return MulDiv(32, static_cast<int>(GetDpiForWindow(hwnd_)), 96);
}

void InfoBar::Show(InfoKind kind, const std::wstring& text)
{
    kind_ = kind;
    text_ = text;
    visible_ = true;
    if (!hwnd_) return;
    SetWindowTextW(hwnd_, text.c_str());   // what a screen reader reads
    ShowWindow(hwnd_, SW_SHOWNA);
    InvalidateRect(hwnd_, nullptr, TRUE);
    NotifyWinEvent(EVENT_OBJECT_LIVEREGIONCHANGED, hwnd_, OBJID_CLIENT, CHILDID_SELF);
    Announce(hwnd_, text);   // spec #8 12: a UI Automation notification, read at once
}

void InfoBar::Hide()
{
    visible_ = false;
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

void InfoBar::Paint(HDC dc)
{
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const bool hc = highContrast();
    HBRUSH fill = CreateSolidBrush(hc ? GetSysColor(COLOR_INFOBK) : fillOf(kind_));
    FillRect(dc, &rc, fill);
    DeleteObject(fill);
    const UINT dpi = GetDpiForWindow(hwnd_);
    const int pad = MulDiv(10, static_cast<int>(dpi), 96), icon = MulDiv(16, static_cast<int>(dpi), 96);
    PCWSTR iconId = kind_ == InfoKind::Error     ? IDI_ERROR
                    : kind_ == InfoKind::Warning ? IDI_WARNING
                                                 : IDI_INFORMATION;
    HICON ic = nullptr;
    if (SUCCEEDED(LoadIconWithScaleDown(nullptr, iconId, icon, icon, &ic)) && ic) {
        DrawIconEx(dc, pad, (rc.bottom - icon) / 2, ic, icon, icon, 0, nullptr, DI_NORMAL);
        DestroyIcon(ic);
    }
    if (!font_) {
        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof ncm;
        SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, dpi);
        font_ = CreateFontIndirectW(&ncm.lfMessageFont);
    }
    HGDIOBJ old = SelectObject(dc, font_);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, hc ? GetSysColor(COLOR_INFOTEXT) : RGB(26, 26, 26));
    RECT text = rc;
    text.left = pad * 2 + icon;
    text.right -= pad;
    DrawTextW(dc, text_.c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, old);
}

LRESULT CALLBACK InfoBar::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* self = reinterpret_cast<InfoBar*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(h, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (self) self->Paint(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_DPICHANGED_AFTERPARENT:
        if (self && self->font_) {
            DeleteObject(self->font_);
            self->font_ = nullptr;
        }
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    case WM_NCDESTROY:
        if (self) self->hwnd_ = nullptr;
        break;
    default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace win32fx
