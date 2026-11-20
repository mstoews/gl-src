//
// EntryGrid.cpp - see EntryGrid.h.
//
#include "EntryGrid.h"

#include "StringUtil.h"

#include <CommCtrl.h>
#include <Uxtheme.h>

#include <algorithm>
#include <string>

#pragma comment(lib, "comctl32.lib")

namespace win32fx {

namespace {

constexpr UINT_PTR kListSubclass = 0x4547, kEditSubclass = 0x4548;
constexpr int kEditId = 0x4549;
constexpr COLORREF kCellBack = RGB(204, 232, 255), kRowBack = RGB(243, 246, 250);

bool ctrl() { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }
bool shift() { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }

std::wstring textOf(HWND h)
{
    const int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

} // namespace

EntryGrid::~EntryGrid()
{
    Detach();
}

void EntryGrid::Attach(HWND list, model::EntryGridModel* model)
{
    Detach();
    list_ = list;
    model_ = model;
    if (!list_ || !model_) return;
    SetWindowTheme(list_, L"Explorer", nullptr);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    const int dpi = static_cast<int>(GetDpiForWindow(list_));
    int i = 0;
    for (const model::GridColumn& c : model_->columns()) {
        std::wstring heading = Utf8ToWide(c.heading);
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.fmt = c.kind == model::CellKind::Amount ? LVCFMT_RIGHT : LVCFMT_LEFT;
        col.cx = MulDiv((std::max)(c.width * 7 + 12, static_cast<int>(heading.size()) * 7 + 16), dpi, 96);   // the heading fits
        col.pszText = heading.data();
        ListView_InsertColumn(list_, i++, &col);
    }
    SetWindowSubclass(list_, ListProc, kListSubclass, reinterpret_cast<DWORD_PTR>(this));
    Refresh();
}

void EntryGrid::Detach()
{
    HideEditor();
    if (list_ && IsWindow(list_)) RemoveWindowSubclass(list_, ListProc, kListSubclass);
    list_ = nullptr;
    model_ = nullptr;
}

void EntryGrid::Refresh()
{
    if (!list_ || !model_) return;
    ListView_SetItemCountEx(list_, model_->Rows(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
    ListView_EnsureVisible(list_, model_->row(), FALSE);
    InvalidateRect(list_, nullptr, FALSE);
}

model::GridAction EntryGrid::Press(model::GridKey key)
{
    if (edit_) SyncEditText();
    const model::GridAction a = Apply(key);
    return a;
}

void EntryGrid::Type(const std::string& utf8)
{
    if (!model_) return;
    if (!edit_) {
        if (!model_->BeginEdit(false)) return;
        ShowEditor(nullptr);
    }
    if (edit_) SetWindowTextW(edit_, Utf8ToWide(utf8).c_str());
}

void EntryGrid::SyncEditText()
{
    if (edit_ && model_) model_->SetEditText(WideToUtf8(textOf(edit_)));
}

model::GridAction EntryGrid::Apply(model::GridKey key)
{
    if (!model_) return model::GridAction::None;
    const bool wasEditing = model_->editing();
    const model::GridAction a = model_->Key(key);
    if (a == model::GridAction::Editing) {
        ShowEditor(nullptr);
    } else if (wasEditing && !model_->editing()) {
        HideEditor();
    }
    Done(a);
    return a;
}

void EntryGrid::Done(model::GridAction a)
{
    Refresh();
    if (a == model::GridAction::Refused && edit_) {
        SetFocus(edit_);
        SendMessageW(edit_, EM_SETSEL, 0, -1);
    }
    if (OnAction && a != model::GridAction::None) OnAction(a);
}

void EntryGrid::ShowEditor(const wchar_t* firstChar)
{
    HideEditor();
    if (!model_ || !model_->editing()) return;
    const int row = model_->row(), col = model_->col();
    ListView_EnsureVisible(list_, row, FALSE);
    RECT rc{};
    ListView_GetSubItemRect(list_, row, col, col == 0 ? LVIR_LABEL : LVIR_BOUNDS, &rc);
    if (col == 0) {   // the label rect starts after the (absent) icon: use the column's own edges
        RECT b{};
        ListView_GetSubItemRect(list_, row, 0, LVIR_BOUNDS, &b);
        rc.left = b.left;
        rc.right = b.left + ListView_GetColumnWidth(list_, 0);
    }
    const model::GridColumn& c = model_->columns()[static_cast<size_t>(col)];
    DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
    if (c.kind == model::CellKind::Amount) style |= ES_RIGHT;
    if (c.kind == model::CellKind::Upper || c.kind == model::CellKind::Lookup) style |= ES_UPPERCASE;
    edit_ = CreateWindowExW(0, L"EDIT", Utf8ToWide(model_->editText()).c_str(), style, rc.left, rc.top,
                            rc.right - rc.left, rc.bottom - rc.top, list_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)),
                            reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(list_, GWLP_HINSTANCE)), nullptr);
    if (!edit_) return;
    SendMessageW(edit_, WM_SETFONT, SendMessageW(list_, WM_GETFONT, 0, 0), FALSE);
    SendMessageW(edit_, EM_LIMITTEXT, static_cast<WPARAM>(std::max(c.width, 1) * 2), 0);
    SetWindowSubclass(edit_, EditProc, kEditSubclass, reinterpret_cast<DWORD_PTR>(this));
    const auto lk = lookups_.find(col);
    if (lk != lookups_.end() && lk->second) {
        lookup_ = std::make_unique<Lookup>();
        lookup_->Attach(edit_, lk->second, [this, row, col](const model::LookupItem& item) {
            SyncEditText();
            if (OnPick) OnPick(row, col, item);
        });
    }
    SetFocus(edit_);
    SendMessageW(edit_, EM_SETSEL, 0, -1);
    if (firstChar) SendMessageW(edit_, WM_CHAR, firstChar[0], 1);
}

void EntryGrid::HideEditor()
{
    if (!edit_ || hiding_) return;
    hiding_ = true;
    HWND e = edit_;
    edit_ = nullptr;
    lookup_.reset();
    const bool hadFocus = GetFocus() == e;
    if (IsWindow(e)) {
        RemoveWindowSubclass(e, EditProc, kEditSubclass);
        DestroyWindow(e);
    }
    if (hadFocus && list_) SetFocus(list_);
    hiding_ = false;
}

bool EntryGrid::ListKey(WPARAM vk)
{
    using model::GridKey;
    switch (vk) {
    case VK_TAB: Apply(shift() ? GridKey::ShiftTab : GridKey::Tab); return true;
    case VK_RETURN: Apply(GridKey::Enter); return true;
    case VK_RIGHT: Apply(GridKey::Tab); return true;
    case VK_LEFT: Apply(GridKey::ShiftTab); return true;
    case VK_UP: Apply(GridKey::Up); return true;
    case VK_DOWN: Apply(GridKey::Down); return true;
    case VK_F2: Apply(GridKey::F2); return true;
    case VK_ESCAPE: Apply(GridKey::Esc); return true;
    case VK_INSERT: Apply(GridKey::Insert); return true;
    case VK_DELETE:
        if (ctrl()) {
            Apply(GridKey::CtrlDelete);
            return true;
        }
        return false;
    case 'Z':
        if (ctrl()) {
            Apply(GridKey::CtrlZ);
            return true;
        }
        return false;
    default: return false;
    }
}

LRESULT CALLBACK EntryGrid::ListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
{
    auto* self = reinterpret_cast<EntryGrid*>(ref);
    switch (msg) {
    case WM_GETDLGCODE:
        if (!ctrl()) return DLGC_WANTALLKEYS | DefSubclassProc(h, msg, wp, lp);
        break;
    case WM_KEYDOWN:
        if (self->ListKey(wp)) return 0;
        break;
    case WM_CHAR:
        if (wp >= 0x20 && wp != 0x7F && !ctrl() && self->model_ && self->model_->BeginEdit(false)) {
            const wchar_t c[2] = {static_cast<wchar_t>(wp), 0};
            self->ShowEditor(c);
            self->Done(model::GridAction::Editing);
            return 0;
        }
        if (wp == VK_TAB || wp == VK_RETURN || wp == VK_ESCAPE) return 0;   // no beep
        break;
    case WM_COMMAND:
        if (LOWORD(wp) == kEditId && self->lookup_) {
            if (HIWORD(wp) == EN_CHANGE) self->lookup_->TextChanged();
            if (HIWORD(wp) == EN_KILLFOCUS) self->lookup_->FocusLost();
        }
        break;
    case WM_VSCROLL:
    case WM_HSCROLL:
    case WM_MOUSEWHEEL:
        if (self->edit_) {   // the cell moves: commit the edit (or drop it) first
            self->SyncEditText();
            if (self->model_->Commit() == model::GridAction::Refused) self->model_->CancelEdit();
            self->HideEditor();
            self->Done(model::GridAction::Committed);
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, ListProc, kListSubclass);
        self->list_ = nullptr;
        break;
    default: break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

LRESULT CALLBACK EntryGrid::EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
{
    auto* self = reinterpret_cast<EntryGrid*>(ref);
    using model::GridKey;
    switch (msg) {
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DefSubclassProc(h, msg, wp, lp);
    case WM_KEYDOWN: {
        GridKey key{};
        bool handled = true;
        switch (wp) {
        case VK_TAB: key = shift() ? GridKey::ShiftTab : GridKey::Tab; break;
        case VK_RETURN: key = GridKey::Enter; break;
        case VK_ESCAPE: key = GridKey::Esc; break;
        case VK_UP: key = GridKey::Up; break;
        case VK_DOWN: key = GridKey::Down; break;
        default: handled = false; break;
        }
        if (handled) {
            self->SyncEditText();
            self->Apply(key);
            return 0;
        }
        break;
    }
    case WM_CHAR:
        if (wp == VK_TAB || wp == VK_RETURN || wp == VK_ESCAPE) return 0;
        break;
    case WM_KILLFOCUS: {
        // Focus moved to another control of this window (a click elsewhere): commit, or
        // drop a refused edit. Another application taking the focus leaves the edit as it is.
        const HWND to = reinterpret_cast<HWND>(wp);
        if (!to || GetAncestor(to, GA_ROOT) != GetAncestor(h, GA_ROOT)) break;
        if (!self->hiding_ && self->edit_ == h && self->model_ && self->model_->editing()) {
            self->SyncEditText();
            model::GridAction a = self->model_->Commit();
            if (a == model::GridAction::Refused) {
                self->model_->CancelEdit();
                if (self->OnAction) self->OnAction(model::GridAction::Refused);
                a = model::GridAction::Cancelled;
            }
            self->HideEditor();
            self->Done(a);
            return 0;
        }
        break;
    }
    default: break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

bool EntryGrid::OnNotify(NMHDR* nm, LRESULT& result)
{
    if (!list_ || nm->hwndFrom != list_ || !model_) return false;
    switch (nm->code) {
    case LVN_GETDISPINFOW: {
        auto* di = reinterpret_cast<NMLVDISPINFOW*>(nm);
        if (di->item.mask & LVIF_TEXT) {
            const std::wstring t = Utf8ToWide(model_->Cell(di->item.iItem, di->item.iSubItem));
            wcsncpy_s(di->item.pszText, static_cast<size_t>(di->item.cchTextMax), t.c_str(), _TRUNCATE);
        }
        result = 0;
        return true;
    }
    case NM_CUSTOMDRAW: {
        auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(nm);
        switch (cd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT: result = CDRF_NOTIFYITEMDRAW; return true;
        case CDDS_ITEMPREPAINT: result = CDRF_NOTIFYSUBITEMDRAW; return true;
        case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
            const int row = static_cast<int>(cd->nmcd.dwItemSpec);
            cd->nmcd.uItemState &= ~static_cast<UINT>(CDIS_SELECTED | CDIS_FOCUS);
            cd->clrText = GetSysColor(COLOR_WINDOWTEXT);
            cd->clrTextBk = row == model_->row() ? (cd->iSubItem == model_->col() ? kCellBack : kRowBack)
                                                 : GetSysColor(COLOR_WINDOW);
            result = CDRF_NEWFONT;
            return true;
        }
        default: result = CDRF_DODEFAULT; return true;
        }
    }
    case NM_CLICK:
    case NM_DBLCLK: {
        const auto* ia = reinterpret_cast<NMITEMACTIVATE*>(nm);
        LVHITTESTINFO hit{};
        hit.pt = ia->ptAction;
        if (ListView_SubItemHitTest(list_, &hit) >= 0 && hit.iItem >= 0) {
            HideEditor();
            model_->SetCurrent(hit.iItem, hit.iSubItem);
            model::GridAction a = model::GridAction::Moved;
            if (nm->code == NM_DBLCLK && model_->BeginEdit(true)) {
                ShowEditor(nullptr);
                a = model::GridAction::Editing;
            }
            Done(a);
        }
        result = 0;
        return true;
    }
    case LVN_ITEMCHANGED: result = 0; return true;
    default: return false;
    }
}

} // namespace win32fx
