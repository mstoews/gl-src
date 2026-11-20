//
// Lookup.cpp - see Lookup.h.
//
#include "Lookup.h"

#include "StringUtil.h"

#include <CommCtrl.h>
#include <Uxtheme.h>

#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace win32fx {

namespace {

constexpr size_t kMaxRows = 12;

std::wstring editText(HWND edit)
{
    const int n = GetWindowTextLengthW(edit);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(edit, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

} // namespace

Lookup::~Lookup()
{
    if (edit_ && IsWindow(edit_)) RemoveWindowSubclass(edit_, EditProc, 1);
    if (popup_ && IsWindow(popup_)) DestroyWindow(popup_);
}

void Lookup::Attach(HWND edit, const model::LookupModel* model, Pick onPick)
{
    edit_ = edit;
    model_ = model;
    onPick_ = std::move(onPick);
    SetWindowSubclass(edit_, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
}

void Lookup::TextChanged()
{
    if (filling_ || !edit_ || GetFocus() != edit_) return;
    Show(false);
}

void Lookup::FocusLost()
{
    Close();
}

void Lookup::Close()
{
    if (popup_ && IsWindowVisible(popup_)) ShowWindow(popup_, SW_HIDE);
}

void Lookup::Show(bool all)
{
    if (!model_ || !edit_) return;
    const std::string q = all ? std::string() : WideToUtf8(editText(edit_));
    shown_ = model_->Match(q, kMaxRows);
    if (shown_.empty() || (!all && q.empty())) {
        Close();
        return;
    }
    const UINT dpi = GetDpiForWindow(edit_);
    if (!popup_) {
        popup_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, WC_LISTVIEWW, L"",
                                 WS_POPUP | WS_BORDER | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                 0, 0, 0, 0, GetAncestor(edit_, GA_ROOT), nullptr,
                                 reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(edit_, GWLP_HINSTANCE)), nullptr);
        if (!popup_) return;
        SetWindowTheme(popup_, L"Explorer", nullptr);
        ListView_SetExtendedListViewStyle(popup_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        SendMessageW(popup_, WM_SETFONT, SendMessageW(edit_, WM_GETFONT, 0, 0), FALSE);
        LVCOLUMNW col{};
        col.mask = LVCF_WIDTH;
        col.cx = MulDiv(90, static_cast<int>(dpi), 96);
        ListView_InsertColumn(popup_, 0, &col);
        col.cx = MulDiv(260, static_cast<int>(dpi), 96);
        ListView_InsertColumn(popup_, 1, &col);
        SetWindowSubclass(popup_, ListProc, 2, reinterpret_cast<DWORD_PTR>(this));
    }
    ListView_DeleteAllItems(popup_);
    for (size_t row = 0; row < shown_.size(); ++row) {
        const model::LookupItem& it = model_->items()[shown_[row]];
        std::wstring key = Utf8ToWide(it.key()), name = Utf8ToWide(it.name);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(row);
        item.pszText = key.data();
        ListView_InsertItem(popup_, &item);
        ListView_SetItemText(popup_, static_cast<int>(row), 1, name.data());
    }
    ListView_SetItemState(popup_, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    // Under the edit, as many rows as it shows.
    RECT er{};
    GetWindowRect(edit_, &er);
    const DWORD rowSpan = ListView_ApproximateViewRect(popup_, -1, -1, static_cast<int>(shown_.size()));
    const int height = HIWORD(rowSpan) + 2 * GetSystemMetricsForDpi(SM_CYBORDER, dpi) + 2;
    const int width = std::max<int>(er.right - er.left, MulDiv(360, static_cast<int>(dpi), 96));
    SetWindowPos(popup_, HWND_TOP, er.left, er.bottom, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void Lookup::Move(int by)
{
    if (!open()) {
        Show(true);
        return;
    }
    const int n = static_cast<int>(shown_.size());
    int row = ListView_GetNextItem(popup_, -1, LVNI_SELECTED);
    row = std::clamp(row + by, 0, n - 1);
    ListView_SetItemState(popup_, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(popup_, row, FALSE);
}

void Lookup::PickRow(int row)
{
    if (row < 0 || static_cast<size_t>(row) >= shown_.size() || !model_) return;
    const model::LookupItem item = model_->items()[shown_[static_cast<size_t>(row)]];
    Close();
    filling_ = true;
    SetWindowTextW(edit_, Utf8ToWide(item.code).c_str());
    SendMessageW(edit_, EM_SETSEL, 0, -1);
    filling_ = false;
    if (onPick_) onPick_(item);
}

LRESULT CALLBACK Lookup::EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
{
    auto* self = reinterpret_cast<Lookup*>(ref);
    switch (msg) {
    case WM_GETDLGCODE:
        // While the list is open, Enter, Esc and the arrows are the lookup's,
        // not the dialog manager's (IDOK, IDCANCEL, the next control).
        if (lp && self->open()) {
            const MSG* m = reinterpret_cast<const MSG*>(lp);
            if (m->message == WM_KEYDOWN && (m->wParam == VK_RETURN || m->wParam == VK_ESCAPE || m->wParam == VK_UP ||
                                              m->wParam == VK_DOWN || m->wParam == VK_PRIOR || m->wParam == VK_NEXT))
                return DLGC_WANTMESSAGE | DefSubclassProc(h, msg, wp, lp);
        }
        break;
    case WM_KEYDOWN:
        switch (wp) {
        case VK_DOWN: self->Move(1); return 0;
        case VK_UP: self->Move(-1); return 0;
        case VK_NEXT: self->Move(static_cast<int>(kMaxRows)); return 0;
        case VK_PRIOR: self->Move(-static_cast<int>(kMaxRows)); return 0;
        case VK_F4:
            if (self->open()) self->Close();
            else self->Show(true);
            return 0;
        case VK_RETURN:
            if (self->open()) {
                self->PickRow(ListView_GetNextItem(self->popup_, -1, LVNI_SELECTED));
                return 0;
            }
            break;
        case VK_ESCAPE:
            if (self->open()) {
                self->Close();
                return 0;
            }
            break;
        default: break;
        }
        break;
    case WM_CHAR:
        if ((wp == VK_RETURN || wp == VK_ESCAPE) && self->open()) return 0;   // no beep
        break;
    case WM_WINDOWPOSCHANGED:
    case WM_DESTROY:
        self->Close();
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, EditProc, id);
        self->edit_ = nullptr;
        break;
    default: break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

LRESULT CALLBACK Lookup::ListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
{
    auto* self = reinterpret_cast<Lookup*>(ref);
    switch (msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_LBUTTONDOWN: {
        // Pick without taking the focus from the edit.
        LVHITTESTINFO hit{};
        hit.pt = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
        const int row = ListView_HitTest(h, &hit);
        if (row >= 0) self->PickRow(row);
        return 0;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, ListProc, id);
        self->popup_ = nullptr;
        break;
    default: break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

} // namespace win32fx
