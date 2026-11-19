//
// Page.cpp - see Page.h.
//
#include "Page.h"

#include "Accessibility.h"

#include "Ask.h"
#include "MessageLoop.h"
#include "StringUtil.h"

#include <CommCtrl.h>

#include <algorithm>

#pragma comment(lib, "comctl32.lib")

namespace win32fx {

namespace {

constexpr wchar_t kPageClass[] = L"fxPage";
constexpr COLORREF kErrorText = RGB(196, 43, 28);   // Windows 11's critical colour

bool ctrlDown() { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }
bool shiftDown() { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }

bool isEdit(HWND h)
{
    wchar_t cls[32] = {};
    GetClassNameW(h, cls, 32);
    return _wcsicmp(cls, L"Edit") == 0;
}

} // namespace

HWND Page::Open(PageHost& host, const std::wstring& title)
{
    title_ = title;
    return CreatePage(host, kPageClass, title.c_str());
}

Page* Page::FromHandle(HWND hwnd) noexcept
{
    return hwnd ? dynamic_cast<Page*>(Window::FromHandle(hwnd)) : nullptr;
}

bool Page::OnCreate(LPCREATESTRUCTW cs)
{
    HINSTANCE inst = cs->hInstance;
    dpi_ = GetDpiForWindow(hwnd());
    info_.Create(hwnd(), inst);
    form_ = CreateDialogParamW(inst, MAKEINTRESOURCEW(template_), hwnd(), FormProc, reinterpret_cast<LPARAM>(this));
    if (!form_) return false;
    SetWindowLongPtrW(form_, GWL_EXSTYLE, GetWindowLongPtrW(form_, GWL_EXSTYLE) | WS_EX_CONTROLPARENT);
    SetWindowLongPtrW(hwnd(), GWL_EXSTYLE, GetWindowLongPtrW(hwnd(), GWL_EXSTYLE) | WS_EX_CONTROLPARENT);
    AddModeless(form_);   // Tab, Shift+Tab, Enter and access keys in the form (IsDialogMessage)
    {
        Filling f(*this);
        if (!OnInitPage()) return false;
    }
    NameUnnamedControls(form_);   // spec #8 12: every control a screen reader can name
    layout_.Capture(form_, anchors_);
    ShowWindow(form_, SW_SHOW);
    return false;   // DefWindowProc too
}

void Page::SetAnchors(std::vector<std::pair<int, unsigned>> anchors)
{
    anchors_ = std::move(anchors);
}

bool Page::OnDestroy()
{
    if (form_) {
        ClearAccessibleNames(form_);
        RemoveModeless(form_);
    }
    return false;
}

void Page::SetPageTitle(const std::wstring& title)
{
    title_ = title;
    SetTitle((title_ + (dirty_ ? L" •" : L"")).c_str());
}

void Page::SetDirty(bool dirty)
{
    if (dirty_ == dirty) return;
    dirty_ = dirty;
    SetPageTitle(title_);
}

bool Page::CanClose()
{
    if (!dirty_) return true;
    enum { kSave = 101, kDiscard = 102 };
    HWND owner = host() ? host()->hostWindow() : hwnd();
    wchar_t app[64] = {};
    GetWindowTextW(owner, app, 64);
    const int chosen = Ask(owner, app, L"Save the changes to " + title_ + L"?", L"",
                           {{kSave, L"Save"}, {kDiscard, L"Don't save"}, {IDCANCEL, L"Cancel"}}, kSave, IDCANCEL,
                           AskIcon::Warning);
    if (chosen == kSave) return Save();
    if (chosen == kDiscard) return true;
    return false;
}

bool Page::OnShortcut(const MSG& msg)
{
    if (msg.message != WM_KEYDOWN || !ctrlDown() || shiftDown()) return false;
    switch (msg.wParam) {
    case 'S':
    case VK_RETURN: Save(); return true;
    case 'N': OnNew(); return true;
    default: return false;
    }
}

// ---- fields ----------------------------------------------------------------------------

std::string Page::Text(int id) const
{
    return form_ ? WideToUtf8(GetDlgItemString(form_, id)) : std::string();
}

void Page::SetText(int id, const std::string& utf8)
{
    if (form_) SetDlgItemTextW(form_, id, Utf8ToWide(utf8).c_str());
}

bool Page::Checked(int id) const
{
    return form_ && SendDlgItemMessageW(form_, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void Page::SetChecked(int id, bool on)
{
    if (form_) SendDlgItemMessageW(form_, id, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool Page::Validate(int id)
{
    const auto it = validators_.find(id);
    if (it == validators_.end()) return true;
    if (const auto why = it->second.Run(Text(id))) {
        MarkField(id, *why);
        return false;
    }
    ClearMark(id);
    return true;
}

HWND Page::labelOf(int id) const
{
    HWND c = Item(id);
    if (!c) return nullptr;
    HWND prev = GetWindow(c, GW_HWNDPREV);   // the dialog manager's rule: the label comes just before
    wchar_t cls[32] = {};
    if (prev) GetClassNameW(prev, cls, 32);
    return prev && _wcsicmp(cls, L"Static") == 0 ? prev : nullptr;
}

void Page::MarkField(int id, const std::string& message, bool focus)
{
    marks_[id] = message;
    HWND c = Item(id);
    if (!c) return;
    if (HWND l = labelOf(id)) InvalidateRect(l, nullptr, TRUE);
    if (focus) {
        SendMessageW(form_, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(c), TRUE);
    }
    if (isEdit(c) && (focus || GetFocus() == c)) {
        const std::wstring text = Utf8ToWide(message);
        EDITBALLOONTIP tip{sizeof tip, L"", text.c_str(), TTI_ERROR};
        SendMessageW(c, EM_SHOWBALLOONTIP, 0, reinterpret_cast<LPARAM>(&tip));
    }
}

void Page::ClearMark(int id)
{
    if (!marks_.erase(id)) return;
    if (HWND c = Item(id)) {
        if (isEdit(c)) SendMessageW(c, EM_HIDEBALLOONTIP, 0, 0);
        if (HWND l = labelOf(id)) InvalidateRect(l, nullptr, TRUE);
    }
}

void Page::ClearMarks()
{
    std::vector<int> ids;
    for (const auto& [id, m] : marks_) ids.push_back(id);
    for (int id : ids) ClearMark(id);
}

void Page::OnFieldChanged(int id)
{
    SetDirty(true);
    ClearMark(id);
    if (info_.visible() && (info_.kind() == InfoKind::Success || info_.kind() == InfoKind::Information)) HideInfo();
}

// ---- the information bar -------------------------------------------------------------

void Page::ShowInfo(InfoKind kind, const std::wstring& text)
{
    info_.Show(kind, text);
    Relayout();
}

void Page::HideInfo()
{
    if (!info_.visible()) return;
    info_.Hide();
    Relayout();
}

// ---- layout and scrolling ------------------------------------------------------------

bool Page::OnSize(UINT type, int, int)
{
    if (type != SIZE_MINIMIZED) Relayout();
    return false;
}

void Page::Relayout()
{
    if (!form_ || !hwnd()) return;
    const model::Size orig = layout_.original();
    // A form a few pixels larger than the page fits it instead of scrolling: a scroll bar
    // whose thumb fills the track says "there's more" when there isn't (design review).
    // The anchors take up the difference, up to kSlack96 px on each axis.
    constexpr int kSlack96 = 8;
    const int slack = MulDiv(kSlack96, static_cast<int>(dpi_), 96);
    for (int pass = 0; pass < 2; ++pass) {   // a scroll bar appearing changes the client size
        RECT rc{};
        GetClientRect(hwnd(), &rc);
        const int infoH = info_.Height();
        if (info_.hwnd()) MoveWindow(info_.hwnd(), 0, 0, rc.right, infoH, TRUE);
        const int availW = rc.right, availH = std::max(0, static_cast<int>(rc.bottom) - infoH);
        model::Size least = orig;
        if (orig.cx > availW && orig.cx - availW <= slack) least.cx = availW;
        if (orig.cy > availH && orig.cy - availH <= slack) least.cy = availH;
        const model::Size form = model::Clamp({availW, availH}, least);
        sx_ = std::clamp(sx_, 0, std::max(0, form.cx - availW));
        sy_ = std::clamp(sy_, 0, std::max(0, form.cy - availH));
        SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS};
        si.nMax = form.cx - 1;
        si.nPage = static_cast<UINT>(availW);
        si.nPos = sx_;
        SetScrollInfo(hwnd(), SB_HORZ, &si, TRUE);
        si.nMax = form.cy - 1;
        si.nPage = static_cast<UINT>(availH);
        si.nPos = sy_;
        SetScrollInfo(hwnd(), SB_VERT, &si, TRUE);
        if (pass == 1) {
            SetWindowPos(form_, nullptr, -sx_, infoH - sy_, form.cx, form.cy, SWP_NOZORDER | SWP_NOACTIVATE);
            layout_.Apply(form.cx, form.cy);
        }
    }
}

void Page::Scroll(int bar, WORD code)
{
    SCROLLINFO si{sizeof si, SIF_ALL};
    GetScrollInfo(hwnd(), bar, &si);
    const int line = MulDiv(24, static_cast<int>(dpi_), 96);
    int pos = si.nPos;
    switch (code) {
    case SB_LINEUP: pos -= line; break;
    case SB_LINEDOWN: pos += line; break;
    case SB_PAGEUP: pos -= static_cast<int>(si.nPage); break;
    case SB_PAGEDOWN: pos += static_cast<int>(si.nPage); break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    case SB_TOP: pos = 0; break;
    case SB_BOTTOM: pos = si.nMax; break;
    default: return;
    }
    (bar == SB_VERT ? sy_ : sx_) = pos;
    Relayout();
}

// ---- messages --------------------------------------------------------------------------

bool Page::OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result)
{
    HostedPage::OnMessage(msg, wp, lp, result);
    switch (msg) {
    case WM_SETFOCUS: {
        HWND to = lastFocus_ && IsWindow(lastFocus_) && IsChild(form_, lastFocus_)
                      ? lastFocus_
                      : (form_ ? GetNextDlgTabItem(form_, nullptr, FALSE) : nullptr);
        if (to) SetFocus(to);
        return false;
    }
    case WM_SHOWWINDOW:
        if (!wp) {   // hidden by the workspace: remember where the focus was
            HWND f = GetFocus();
            if (f && form_ && IsChild(form_, f)) lastFocus_ = f;
        }
        return false;
    case WM_VSCROLL: Scroll(SB_VERT, LOWORD(wp)); result = 0; return true;
    case WM_HSCROLL: Scroll(SB_HORZ, LOWORD(wp)); result = 0; return true;
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        sy_ -= delta * MulDiv(72, static_cast<int>(dpi_), 96) / WHEEL_DELTA;
        Relayout();
        result = 0;
        return true;
    }
    case WM_DPICHANGED_AFTERPARENT: {
        const UINT dpi = GetDpiForWindow(hwnd());
        layout_.Rescale(dpi_, dpi);
        dpi_ = dpi;
        Relayout();
        return false;
    }
    default: return false;
    }
}

INT_PTR CALLBACK Page::FormProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INITDIALOG) {
        SetWindowLongPtrW(h, DWLP_USER, lp);
        auto* self = reinterpret_cast<Page*>(lp);
        self->form_ = h;
        return FALSE;   // the page sets the focus when it is shown
    }
    auto* self = reinterpret_cast<Page*>(GetWindowLongPtrW(h, DWLP_USER));
    return self ? self->FormMessage(msg, wp, lp) : FALSE;
}

INT_PTR Page::FormMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND: {
        const WORD id = LOWORD(wp), code = HIWORD(wp);
        const HWND ctl = reinterpret_cast<HWND>(lp);
        if (OnFormCommand(id, code, ctl)) return TRUE;
        if (code == EN_CHANGE && !filling_) {
            OnFieldChanged(id);
            return TRUE;
        }
        if (code == EN_KILLFOCUS) {
            Validate(id);
            return TRUE;
        }
        if (id == IDOK && !ctl) {   // Enter: the next field (spec #8 Q3)
            SendMessageW(form_, WM_NEXTDLGCTL, 0, FALSE);
            return TRUE;
        }
        if (id == IDCANCEL && !ctl) return TRUE;   // Esc closes nothing
        return FALSE;
    }
    case WM_NOTIFY: {
        LRESULT r = 0;
        if (OnFormNotify(reinterpret_cast<NMHDR*>(lp), r)) {
            SetWindowLongPtrW(form_, DWLP_MSGRESULT, r);
            return TRUE;
        }
        return FALSE;
    }
    case WM_CTLCOLORSTATIC: {
        const HWND label = reinterpret_cast<HWND>(lp);
        for (const auto& [id, m] : marks_)
            if (labelOf(id) == label) {
                HDC dc = reinterpret_cast<HDC>(wp);
                SetTextColor(dc, kErrorText);
                SetBkMode(dc, TRANSPARENT);
                return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
            }
        return FALSE;
    }
    default: return FALSE;
    }
}

} // namespace win32fx
