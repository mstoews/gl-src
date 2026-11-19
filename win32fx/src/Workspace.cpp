//
// Workspace.cpp - see Workspace.h.
//
#include "Workspace.h"

#include "MdiFrame.h"   // the legacy Window menu's command IDs
#include "MessageLoop.h"
#include "Page.h"

#include <Uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace win32fx {

namespace {

constexpr UINT kNavId = 0xFE01;
constexpr UINT kTabsId = 0xFE02;
constexpr WORD kFirstPageCommand = 0xFE10;   // the Window menu's page list
constexpr WORD kMaxListedPages = 9;
constexpr int kNavWidth96 = 230;
constexpr WORD kTabClose = 1, kTabCloseOthers = 2, kTabPin = 3;

Workspace* gKeyWorkspace = nullptr;   // the workspace whose keys the message loop filters

constexpr int kNavIcon96 = 16;
constexpr COLORREF kNavAccent = RGB(0, 95, 184);   // the Windows 11 accent blue (the home page's links)

// The system icon font at `px`: Segoe Fluent Icons (Windows 11), else Segoe MDL2 Assets.
HFONT iconFont(int px)
{
    for (const wchar_t* face : {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"}) {
        HFONT f = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
        if (!f) continue;
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ old = SelectObject(dc, f);
        wchar_t got[LF_FACESIZE] = {};
        GetTextFaceW(dc, LF_FACESIZE, got);
        SelectObject(dc, old);
        DeleteDC(dc);
        if (_wcsicmp(got, face) == 0) return f;
        DeleteObject(f);
    }
    return nullptr;
}

// `glyph` drawn in `color` on a transparent px x px icon (alpha from the glyph's coverage).
HICON glyphIcon(HFONT font, wchar_t glyph, int px, COLORREF color)
{
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = px;
    bi.bmiHeader.biHeight = -px;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP color32 = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color32 || !bits) {
        DeleteDC(dc);
        return nullptr;
    }
    HGDIOBJ oldBmp = SelectObject(dc, color32);
    HGDIOBJ oldFont = SelectObject(dc, font);
    RECT rc{0, 0, px, px};
    FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    DrawTextW(dc, &glyph, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteDC(dc);
    // Black on white: the coverage is 255 less the brightness. Straight alpha, as an icon's.
    auto* p = static_cast<DWORD*>(bits);
    const DWORD rgb = static_cast<DWORD>(GetRValue(color)) << 16 | static_cast<DWORD>(GetGValue(color)) << 8 |
                      GetBValue(color);
    for (int i = 0; i < px * px; ++i) {
        const DWORD cover = 255 - (p[i] & 0xFF);
        p[i] = cover ? (cover << 24 | rgb) : 0;
    }
    std::vector<BYTE> zeros(static_cast<size_t>(((px + 15) / 16) * 2 * px), 0);
    HBITMAP mask = CreateBitmap(px, px, 1, 1, zeros.data());
    ICONINFO ii{TRUE, 0, 0, mask, color32};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color32);
    return icon;
}

bool ctrlDown() { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }
bool shiftDown() { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }

} // namespace

Workspace::~Workspace()
{
    if (gKeyWorkspace == this) {
        gKeyWorkspace = nullptr;
        if (CurrentKeyFilter() == &Workspace::FilterKey) SetKeyFilter(nullptr);
    }
    if (uiFont_) DeleteObject(uiFont_);
    if (navImages_) ImageList_Destroy(navImages_);
}

HWND Workspace::CreateWorkspace(HINSTANCE inst, const wchar_t* className, const wchar_t* title, HMENU menu,
                                int windowMenuPos, HICON icon)
{
    inst_ = inst;
    windowMenu_ = (menu && windowMenuPos >= 0) ? GetSubMenu(menu, windowMenuPos) : nullptr;
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_TAB_CLASSES | ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);
    if (!EnsureWindowClass(inst, className, icon, reinterpret_cast<HBRUSH>(COLOR_APPWORKSPACE + 1))) return nullptr;
    return Create(0, className, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                  CW_USEDEFAULT, CW_USEDEFAULT, nullptr, menu, inst);
}

int Workspace::scaled(int px96) const
{
    return MulDiv(px96, static_cast<int>(hwnd() ? GetDpiForWindow(hwnd()) : 96), 96);
}

void Workspace::SetUiFont()
{
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, GetDpiForWindow(hwnd()));
    HFONT old = uiFont_;
    uiFont_ = CreateFontIndirectW(&ncm.lfMessageFont);
    for (HWND h : {nav_, tabs_})
        if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    if (old) DeleteObject(old);
}

bool Workspace::OnCreate(LPCREATESTRUCTW)
{
    nav_ = CreateWindowExW(0, WC_TREEVIEWW, L"Navigation",
                           // No expand buttons: each row has its icon (AddNavItem), a group's in the accent colour.
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT |
                               TVS_TRACKSELECT | TVS_SINGLEEXPAND,
                           0, 0, 0, 0, hwnd(), reinterpret_cast<HMENU>(static_cast<INT_PTR>(kNavId)), inst_, nullptr);
    if (nav_) {
        SetWindowTheme(nav_, L"Explorer", nullptr);
        TreeView_SetExtendedStyle(nav_, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
        TreeView_SetIndent(nav_, scaled(14));
    }
    tabs_ = CreateWindowExW(0, WC_TABCONTROLW, L"Pages", WS_CHILD | WS_CLIPSIBLINGS | TCS_FOCUSNEVER | TCS_TABS,
                            0, 0, 0, 0, hwnd(), reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabsId)), inst_, nullptr);
    SetUiFont();
    if (!gKeyWorkspace) {
        gKeyWorkspace = this;
        SetKeyFilter(&Workspace::FilterKey);
    }
    return true;
}

// ---- layout -------------------------------------------------------------------------

void Workspace::Dock(HWND bar, DockSide side, int height)
{
    Undock(bar);
    docked_.push_back({bar, side, height});
    Layout();
}

void Workspace::Undock(HWND bar)
{
    std::erase_if(docked_, [bar](const Docked& d) { return d.hwnd == bar; });
}

RECT Workspace::pageRect() const noexcept
{
    RECT rc{};
    if (!hwnd()) return rc;
    GetClientRect(hwnd(), &rc);
    for (const Docked& d : docked_) {
        if (!IsWindow(d.hwnd) || !IsWindowVisible(d.hwnd)) continue;
        RECT wr{};
        GetWindowRect(d.hwnd, &wr);
        const int h = wr.bottom - wr.top;
        if (d.side == DockSide::Top) rc.top += h;
        else rc.bottom -= h;
    }
    if (navVisible_ && nav_) rc.left += scaled(kNavWidth96);
    if (tabs_ && !pages_.empty()) {
        RECT strip{rc.left, rc.top, rc.right, rc.top + scaled(200)};
        RECT inner = strip;
        TabCtrl_AdjustRect(tabs_, FALSE, &inner);   // the display area under the tabs
        rc.top += inner.top - strip.top - scaled(2);
    }
    if (rc.bottom < rc.top) rc.bottom = rc.top;
    if (rc.right < rc.left) rc.right = rc.left;
    return rc;
}

void Workspace::Layout()
{
    if (!hwnd()) return;
    RECT rc{};
    GetClientRect(hwnd(), &rc);
    const int width = rc.right - rc.left;
    int top = rc.top, bottom = rc.bottom;
    for (const Docked& d : docked_) {
        if (!IsWindow(d.hwnd) || !IsWindowVisible(d.hwnd)) continue;
        int h = d.height;
        if (h == 0) {   // sizes and places itself (status bar): bottom edge
            SendMessageW(d.hwnd, WM_SIZE, 0, 0);
            RECT wr{};
            GetWindowRect(d.hwnd, &wr);
            h = wr.bottom - wr.top;
            if (d.side == DockSide::Bottom) {
                bottom -= h;
                continue;
            }
        }
        if (d.side == DockSide::Top) {
            MoveWindow(d.hwnd, 0, top, width, h, TRUE);
            top += h;
        } else {
            bottom -= h;
            MoveWindow(d.hwnd, 0, bottom, width, h, TRUE);
        }
    }
    const int navW = navVisible_ && nav_ ? scaled(kNavWidth96) : 0;
    if (nav_) {
        ShowWindow(nav_, navVisible_ ? SW_SHOW : SW_HIDE);
        if (navVisible_) MoveWindow(nav_, 0, top, navW, std::max(0, bottom - top), TRUE);
    }
    const RECT page = pageRect();
    if (tabs_) {
        ShowWindow(tabs_, pages_.empty() ? SW_HIDE : SW_SHOW);
        if (!pages_.empty()) MoveWindow(tabs_, navW, top, width - navW, page.top - top + scaled(2), TRUE);
    }
    for (HWND p : pages_)
        SetWindowPos(p, HWND_TOP, page.left, page.top, page.right - page.left, page.bottom - page.top,
                     SWP_NOACTIVATE | (p == active_ ? 0 : SWP_NOREDRAW));
    InvalidateRect(hwnd(), nullptr, TRUE);
}

bool Workspace::OnSize(UINT type, int, int)
{
    if (type != SIZE_MINIMIZED) Layout();
    return true;
}

void Workspace::SetNavigationVisible(bool visible)
{
    if (navVisible_ == visible) return;
    navVisible_ = visible;
    Layout();
    if (!visible && GetFocus() == nav_ && active_) SetFocus(active_);
}

// ---- the navigation pane ------------------------------------------------------------

void Workspace::ClearNavigation()
{
    if (nav_) TreeView_DeleteAllItems(nav_);
}

void Workspace::BuildNavImages()
{
    if (!nav_) return;
    const int px = scaled(kNavIcon96);
    HIMAGELIST images = ImageList_Create(px, px, ILC_COLOR32 | ILC_MASK, static_cast<int>(navGlyphs_.size()) + 8, 8);
    if (!images) return;
    HFONT font = iconFont(px);
    for (const NavIcon& g : navGlyphs_) {
        HICON icon = font ? glyphIcon(font, g.glyph, px, g.group ? kNavAccent : GetSysColor(COLOR_WINDOWTEXT)) : nullptr;
        if (icon) {
            ImageList_AddIcon(images, icon);
            DestroyIcon(icon);
        } else {
            HICON blank = CreateIcon(nullptr, 1, 1, 1, 32, nullptr, nullptr);
            ImageList_AddIcon(images, blank);   // keeps the indexes in step
            if (blank) DestroyIcon(blank);
        }
    }
    if (font) DeleteObject(font);
    TreeView_SetImageList(nav_, images, TVSIL_NORMAL);
    if (navImages_) ImageList_Destroy(navImages_);
    navImages_ = images;
}

int Workspace::navImage(wchar_t glyph, bool group)
{
    for (size_t i = 0; i < navGlyphs_.size(); ++i)
        if (navGlyphs_[i].glyph == glyph && navGlyphs_[i].group == group) return static_cast<int>(i);
    navGlyphs_.push_back({glyph, group});
    const int px = scaled(kNavIcon96);
    if (!navImages_) {
        BuildNavImages();
    } else if (HFONT font = iconFont(px)) {
        HICON icon = glyphIcon(font, glyph, px, group ? kNavAccent : GetSysColor(COLOR_WINDOWTEXT));
        DeleteObject(font);
        if (icon) {
            ImageList_AddIcon(navImages_, icon);
            DestroyIcon(icon);
        }
    }
    const int index = static_cast<int>(navGlyphs_.size()) - 1;
    return navImages_ && index < ImageList_GetImageCount(navImages_) ? index : I_IMAGENONE;
}

HTREEITEM Workspace::AddNavItem(HTREEITEM parent, const std::wstring& text, WORD id, bool expanded, wchar_t glyph)
{
    if (!nav_) return nullptr;
    TVINSERTSTRUCTW ins{};
    ins.hParent = parent;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    ins.item.iImage = ins.item.iSelectedImage = glyph ? navImage(glyph, id == 0) : I_IMAGENONE;
    ins.item.pszText = const_cast<wchar_t*>(text.c_str());
    ins.item.lParam = id;
    ins.item.stateMask = TVIS_EXPANDED | TVIS_BOLD;
    ins.item.state = (expanded ? TVIS_EXPANDED : 0) | (parent == TVI_ROOT ? TVIS_BOLD : 0);
    return reinterpret_cast<HTREEITEM>(SendMessageW(nav_, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&ins)));
}

void Workspace::RefreshNavigation()
{
    if (nav_) InvalidateRect(nav_, nullptr, TRUE);
}

void Workspace::RunNavItem(HTREEITEM item)
{
    if (!item) return;
    TVITEMW tv{};
    tv.mask = TVIF_PARAM | TVIF_CHILDREN;
    tv.hItem = item;
    if (!TreeView_GetItem(nav_, &tv)) return;
    const WORD id = static_cast<WORD>(tv.lParam);
    if (!id) {   // a group opens and closes
        TreeView_Expand(nav_, item, TVE_TOGGLE);
        return;
    }
    if (!IsCommandEnabled(id)) {
        MessageBeep(MB_OK);
        return;
    }
    PostMessageW(hwnd(), WM_COMMAND, id, 0);
}

LRESULT Workspace::NavCustomDraw(NMTVCUSTOMDRAW* cd) const
{
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        const WORD id = static_cast<WORD>(cd->nmcd.lItemlParam);
        if (id && !IsCommandEnabled(id)) cd->clrText = GetSysColor(COLOR_GRAYTEXT);
        return CDRF_DODEFAULT;
    }
    default: return CDRF_DODEFAULT;
    }
}

// ---- pages ---------------------------------------------------------------------------

int Workspace::indexOf(HWND page) const
{
    const auto it = std::find(pages_.begin(), pages_.end(), page);
    return it == pages_.end() ? -1 : static_cast<int>(it - pages_.begin());
}

HWND Workspace::CreatePage(HostedPage& page, const wchar_t* className, const wchar_t* title, HICON icon)
{
    page.host_ = this;
    if (!EnsureWindowClass(inst_, className, icon, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1))) return nullptr;
    const bool first = pages_.empty();
    if (first && tabs_) ShowWindow(tabs_, SW_SHOW);   // so pageRect() allows for the strip
    pages_.push_back(nullptr);                           // likewise, while the page is created
    const RECT r = pageRect();
    pages_.pop_back();
    HWND h = page.Create(0, className, title, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, r.left, r.top,
                         r.right - r.left, r.bottom - r.top, hwnd(), nullptr, inst_);
    if (!h) {
        if (first && tabs_) ShowWindow(tabs_, SW_HIDE);
        return nullptr;
    }
    TCITEMW item{};
    item.mask = TCIF_TEXT | TCIF_PARAM;
    item.pszText = const_cast<wchar_t*>(title ? title : L"");
    item.lParam = reinterpret_cast<LPARAM>(h);
    TabCtrl_InsertItem(tabs_, static_cast<int>(pages_.size()), &item);
    pages_.push_back(h);
    Layout();
    ActivatePage(h);
    return h;
}

void Workspace::ActivatePage(HWND page)
{
    const int i = indexOf(page);
    if (i < 0) return;
    if (active_ != page) {
        HWND old = active_;
        active_ = page;
        const RECT r = pageRect();
        SetWindowPos(page, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_SHOWWINDOW);
        if (old && IsWindow(old)) ShowWindow(old, SW_HIDE);
        TabCtrl_SetCurSel(tabs_, i);
        OnPageActivate(page);
    }
    SetFocus(page);
}

void Workspace::ClosePage(HWND page)
{
    if (indexOf(page) < 0) return;
    if (Page* p = Page::FromHandle(page)) {
        if (p->IsDirty()) ActivatePage(page);   // show what it asks about
        if (!p->CanClose()) return;
    }
    DestroyWindow(page);   // WM_PARENTNOTIFY takes its tab away
}

bool Workspace::CloseAllPages()
{
    const std::vector<HWND> all = pages_;
    for (HWND p : all) {
        if (!IsWindow(p)) continue;
        ClosePage(p);
        if (IsWindow(p)) return false;   // it asked, and stayed
    }
    return true;
}

std::vector<WORD> Workspace::PinnedCommands() const
{
    std::vector<WORD> out;
    for (HWND h : pages_)
        if (auto* p = dynamic_cast<HostedPage*>(Window::FromHandle(h)); p && p->pinned && p->commandId)
            out.push_back(p->commandId);
    return out;
}

void Workspace::SetPageTitle(HWND page, const wchar_t* title)
{
    const int i = indexOf(page);
    if (i < 0) return;
    SetWindowTextW(page, title);
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(title ? title : L"");
    TabCtrl_SetItem(tabs_, i, &item);
}

void Workspace::PageGone(HWND page)
{
    const int i = indexOf(page);
    if (i < 0 || destroying_) return;
    TabCtrl_DeleteItem(tabs_, i);
    pages_.erase(pages_.begin() + i);
    if (active_ != page) {
        if (active_) TabCtrl_SetCurSel(tabs_, indexOf(active_));
        Layout();
        return;
    }
    active_ = nullptr;
    Layout();
    if (pages_.empty()) {
        OnPageActivate(nullptr);
        if (nav_ && navVisible_) SetFocus(nav_);
        else SetFocus(hwnd());
    } else {
        ActivatePage(pages_[static_cast<size_t>(std::min(i, static_cast<int>(pages_.size()) - 1))]);
    }
}

void Workspace::NextPage(int step)
{
    if (pages_.empty()) return;
    const int n = static_cast<int>(pages_.size());
    const int i = active_ ? indexOf(active_) : 0;
    ActivatePage(pages_[static_cast<size_t>(((i + step) % n + n) % n)]);
}

void Workspace::TabMenu(POINT screen)
{
    TCHITTESTINFO hit{};
    hit.pt = screen;
    ScreenToClient(tabs_, &hit.pt);
    const int i = TabCtrl_HitTest(tabs_, &hit);
    if (i < 0 || i >= static_cast<int>(pages_.size())) return;
    const HWND page = pages_[static_cast<size_t>(i)];
    auto* hosted = dynamic_cast<HostedPage*>(Window::FromHandle(page));
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kTabClose, L"&Close\tCtrl+W");
    AppendMenuW(menu, MF_STRING | (pages_.size() > 1 ? 0 : MF_GRAYED), kTabCloseOthers, L"Close &others");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu,
                MF_STRING | (hosted && hosted->commandId ? 0 : MF_GRAYED) | (hosted && hosted->pinned ? MF_CHECKED : 0),
                kTabPin, L"&Pin: open when the program starts");
    const UINT chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, hwnd(), nullptr);
    DestroyMenu(menu);
    if (chosen == kTabClose) {
        ClosePage(page);
    } else if (chosen == kTabCloseOthers) {
        const std::vector<HWND> all = pages_;
        for (HWND p : all)
            if (p != page && IsWindow(p)) ClosePage(p);
        ActivatePage(page);
    } else if (chosen == kTabPin && hosted) {
        hosted->pinned = !hosted->pinned;
        OnPinsChanged();
    }
}

void Workspace::FillWindowMenu(HMENU menu)
{
    // MDI listed its children here; the workspace lists its pages. Tile,
    // Cascade and Arrange Icons have nothing to arrange.
    for (WORD id : {CM_TILECHILDREN, CM_CASCADECHILDREN, CM_ARRANGEICONS})
        EnableMenuItem(menu, id, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(menu, CM_CLOSECHILDREN, MF_BYCOMMAND | (pages_.empty() ? MF_GRAYED : MF_ENABLED));
    for (int i = GetMenuItemCount(menu) - 1; i >= 0; --i) {
        const UINT id = GetMenuItemID(menu, i);
        const bool listed = id >= kFirstPageCommand && id < kFirstPageCommand + kMaxListedPages;
        const bool ourSeparator = id == 0 && i == GetMenuItemCount(menu) - 1 &&
                                  (GetMenuState(menu, static_cast<UINT>(i), MF_BYPOSITION) & MF_SEPARATOR);
        if (listed || ourSeparator) DeleteMenu(menu, static_cast<UINT>(i), MF_BYPOSITION);
    }
    if (pages_.empty()) return;
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < pages_.size() && i < kMaxListedPages; ++i) {
        wchar_t title[128] = {};
        GetWindowTextW(pages_[i], title, 128);
        const std::wstring text = L"&" + std::to_wstring(i + 1) + L" " + title;
        AppendMenuW(menu, MF_STRING | (pages_[i] == active_ ? MF_CHECKED : 0),
                    kFirstPageCommand + static_cast<WORD>(i), text.c_str());
    }
}

// ---- keys ----------------------------------------------------------------------------

bool Workspace::FilterKey(MSG& msg)
{
    return gKeyWorkspace && gKeyWorkspace->Key(msg);
}

bool Workspace::Key(const MSG& msg)
{
    if (msg.message != WM_KEYDOWN || !ctrlDown() || !hwnd()) return false;
    if (msg.hwnd != hwnd() && !IsChild(hwnd(), msg.hwnd)) return false;   // a dialog of its own
    if (Page* p = Page::FromHandle(active_); p && p->OnShortcut(msg)) return true;   // Ctrl+S, Ctrl+Enter, Ctrl+N
    switch (msg.wParam) {
    case VK_TAB:
        if (pages_.empty()) return false;
        NextPage(shiftDown() ? -1 : 1);
        return true;
    case 'W':
    case VK_F4:
        if (!active_) return false;
        ClosePage(active_);
        return true;
    case 'N':
        if (!shiftDown()) return false;
        SetNavigationVisible(!navVisible_);
        if (navVisible_ && nav_) SetFocus(nav_);
        return true;
    default: return OnFrameShortcut(msg);
    }
}

// ---- messages ------------------------------------------------------------------------

bool Workspace::OnCommand(WORD id, WORD, HWND)
{
    if (id >= kFirstPageCommand && id < kFirstPageCommand + kMaxListedPages) {
        const size_t i = id - kFirstPageCommand;
        if (i < pages_.size()) ActivatePage(pages_[i]);
        return true;
    }
    switch (id) {
    case CM_CLOSECHILDREN: CloseAllPages(); return true;
    case CM_TILECHILDREN:
    case CM_CASCADECHILDREN:
    case CM_ARRANGEICONS: return true;   // nothing to arrange (greyed in the menu)
    case CM_EXIT: PostMessageW(hwnd(), WM_CLOSE, 0, 0); return true;
    default: return false;
    }
}

bool Workspace::OnDestroy()
{
    PostQuitMessage(0);
    return true;
}

bool Workspace::OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result)
{
    switch (msg) {
    case WM_NOTIFY: {
        const auto* nm = reinterpret_cast<const NMHDR*>(lp);
        if (nm->hwndFrom == tabs_) {
            if (nm->code == TCN_SELCHANGE) {
                const int i = TabCtrl_GetCurSel(tabs_);
                if (i >= 0 && i < static_cast<int>(pages_.size())) ActivatePage(pages_[static_cast<size_t>(i)]);
                result = 0;
                return true;
            }
            if (nm->code == NM_RCLICK) {
                POINT pt{};
                GetCursorPos(&pt);
                TabMenu(pt);
                result = 1;
                return true;
            }
        } else if (nm->hwndFrom == nav_) {
            switch (nm->code) {
            case NM_CUSTOMDRAW:
                result = NavCustomDraw(reinterpret_cast<NMTVCUSTOMDRAW*>(const_cast<NMHDR*>(nm)));
                return true;
            case NM_CLICK: {
                TVHITTESTINFO hit{};
                GetCursorPos(&hit.pt);
                ScreenToClient(nav_, &hit.pt);
                HTREEITEM item = TreeView_HitTest(nav_, &hit);
                result = 0;
                if (item && (hit.flags & (TVHT_ONITEM | TVHT_ONITEMRIGHT | TVHT_ONITEMINDENT))) {
                    const bool wasSelected = TreeView_GetSelection(nav_) == item;
                    TVITEMW tv{};
                    tv.mask = TVIF_PARAM;
                    tv.hItem = item;
                    const bool group = TreeView_GetItem(nav_, &tv) && tv.lParam == 0;
                    TreeView_SelectItem(nav_, item);
                    if (group) {
                        // TVS_SINGLEEXPAND opens a group as it's selected (closing the others); a click
                        // on the group already selected opens or closes it. Toggling after the select
                        // closed it again, so the groups never seemed to open.
                        TreeView_Expand(nav_, item, wasSelected ? TVE_TOGGLE : TVE_EXPAND);
                    } else {
                        RunNavItem(item);
                    }
                    result = 1;   // handled: the tree's own click handling would toggle it once more
                }
                return true;
            }
            case NM_RETURN:
                RunNavItem(TreeView_GetSelection(nav_));
                result = 1;
                return true;
            default: break;
            }
        }
        return false;
    }
    case WM_PARENTNOTIFY:
        if (LOWORD(wp) == WM_DESTROY) PageGone(reinterpret_cast<HWND>(lp));
        return false;
    case WM_INITMENUPOPUP:
        if (windowMenu_ && reinterpret_cast<HMENU>(wp) == windowMenu_) FillWindowMenu(windowMenu_);
        return false;
    case WM_RBUTTONUP: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const RECT r = pageRect();
        if (PtInRect(&r, pt)) {
            ClientToScreen(hwnd(), &pt);
            OnPageAreaContextMenu(pt);
            result = 0;
            return true;
        }
        return false;
    }
    case WM_CLOSE:
        // Each dirty page asks first; one kept open keeps the program open.
        if (!CloseAllPages()) {
            result = 0;
            return true;
        }
        return false;
    case WM_SETFOCUS:
        if (active_) SetFocus(active_);
        else if (nav_ && navVisible_) SetFocus(nav_);
        return false;
    case WM_DPICHANGED_AFTERPARENT:
        return false;
    default: return false;
    }
}

bool Workspace::ProcessMessageMap(UINT msg, WPARAM, LPARAM, LRESULT&)
{
    if (msg == WM_DESTROY) destroying_ = true;
    return false;
}

LRESULT Workspace::DefaultProc(UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DPICHANGED) {   // per-monitor v2: moved to a monitor with another scaling
        OnDpiChanged(HIWORD(wp));
        SetUiFont();
        if (navImages_) BuildNavImages();
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd(), nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        return 0;
    }
    return DefWindowProcW(hwnd(), msg, wp, lp);
}

} // namespace win32fx
