//
// Workspace.h - the program's main window (spec #8 4, D3; plan S8-M0 task 3).
//
// One top-level window in place of the MDI frame:
//
//   +-----------------------------------------------------------+
//   | menu bar                                                  |
//   | docked bars at the top (the toolbar)                      |
//   +-------------+---------------------------------------------+
//   | navigation  | [tab] [tab] [tab]                           |
//   | pane        +---------------------------------------------+
//   | (TreeView)  | the active page                             |
//   +-------------+---------------------------------------------+
//   | docked bars at the bottom (the status bar)                |
//   +-----------------------------------------------------------+
//
// Pages (HostedPage, PageHost.h) are child windows of the workspace; the
// one whose tab is selected is shown, the others are hidden and keep their
// state. A page closes through ClosePage() or by destroying itself; either
// way its tab goes (WM_PARENTNOTIFY).
//
// Keys, wherever the focus is in the workspace: Ctrl+Tab and Ctrl+Shift+Tab
// move between tabs, Ctrl+W and Ctrl+F4 close the active one, Ctrl+Shift+N
// shows or hides the navigation pane. A tab's context menu has Close and
// Close others.
//
// The navigation pane holds what the subclass puts in it (AddNavItem): an
// item with a command ID sends WM_COMMAND when clicked or on Enter, unless
// IsCommandEnabled() says no (it is drawn greyed). The legacy Window menu's
// commands keep working: Close Children closes every page; Tile, Cascade and
// Arrange Icons have nothing to arrange and are greyed; the open pages are
// listed under it, as MDI listed its children.
//
#pragma once

#include "Dock.h"
#include "PageHost.h"
#include "Window.h"

#include <CommCtrl.h>

#include <string>
#include <vector>

namespace win32fx {

class Workspace : public Window, public PageHost {
public:
    ~Workspace() override;

    // Registers `className` (once per process) and creates the window with
    // `menu`. The legacy Window menu is submenu `windowMenuPos` of `menu`
    // (-1: none); the open pages are listed at its end.
    HWND CreateWorkspace(HINSTANCE inst, const wchar_t* className, const wchar_t* title, HMENU menu,
                         int windowMenuPos, HICON icon = nullptr);

    HINSTANCE instance() const noexcept { return inst_; }

    // Bars docked full width at the top or bottom, as MdiFrame::Dock.
    void Dock(HWND bar, DockSide side, int height);
    void Undock(HWND bar);
    void Layout();

    // ---- the navigation pane ----
    HWND navigation() const noexcept { return nav_; }
    void ClearNavigation();
    // An item under `parent` (TVI_ROOT for the top). id 0: a group.
    HTREEITEM AddNavItem(HTREEITEM parent, const std::wstring& text, WORD id, bool expanded = false);
    void SetNavigationVisible(bool visible);
    bool navigationVisible() const noexcept { return navVisible_; }
    // Redraw the pane (after the rights change, say).
    void RefreshNavigation();

    // ---- pages ----
    const std::vector<HWND>& pages() const noexcept { return pages_; }
    // Closes every page that may close (a dirty Page asks); false if one stayed.
    bool CloseAllPages();
    // The pinned pages' commands, in tab order.
    std::vector<WORD> PinnedCommands() const;

    // PageHost
    HWND hostWindow() const noexcept override { return hwnd(); }
    HINSTANCE hostInstance() const noexcept override { return inst_; }
    HWND CreatePage(HostedPage& page, const wchar_t* className, const wchar_t* title, HICON icon) override;
    HWND activePage() const noexcept override { return active_; }
    void ActivatePage(HWND page) override;
    void ClosePage(HWND page) override;
    void SetPageTitle(HWND page, const wchar_t* title) override;

    // WM_DPICHANGED: called with the new DPI before the window takes Windows'
    // suggested rectangle and lays out again (as MdiFrame::OnDpiChanged).
    virtual void OnDpiChanged(UINT /*dpi*/) {}

    // The rect the pages fill, in client coordinates.
    RECT pageRect() const noexcept;

protected:
    // A navigation item's command may run (drawn greyed, and ignored, if not).
    virtual bool IsCommandEnabled(WORD /*id*/) const { return true; }
    // A right-click on the empty page area.
    virtual void OnPageAreaContextMenu(POINT /*screen*/) {}
    // A tab was pinned or unpinned.
    virtual void OnPinsChanged() {}

    bool OnCreate(LPCREATESTRUCTW cs) override;
    bool OnSize(UINT type, int cx, int cy) override;
    bool OnCommand(WORD id, WORD code, HWND ctl) override;
    bool OnDestroy() override;   // PostQuitMessage(0): the workspace is the main window
    // A subclass that overrides this calls it first.
    bool OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override;
    LRESULT DefaultProc(UINT msg, WPARAM wp, LPARAM lp) override;
    // Notes WM_DESTROY before any handler: the pages' WM_PARENTNOTIFYs that
    // follow mustn't touch the closing tabs. A subclass's BEGIN_MSG_MAP calls this.
    bool ProcessMessageMap(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override;

private:
    struct Docked {
        HWND hwnd;
        DockSide side;
        int height;
    };

    static bool FilterKey(MSG& msg);
    bool Key(const MSG& msg);
    int indexOf(HWND page) const;
    void PageGone(HWND page);
    void NextPage(int step);
    void TabMenu(POINT screen);
    void FillWindowMenu(HMENU menu);
    void RunNavItem(HTREEITEM item);
    LRESULT NavCustomDraw(NMTVCUSTOMDRAW* cd) const;
    void SetUiFont();
    int scaled(int px96) const;

    HINSTANCE inst_ = nullptr;
    HWND nav_ = nullptr;
    HWND tabs_ = nullptr;
    HMENU windowMenu_ = nullptr;
    HFONT uiFont_ = nullptr;
    bool navVisible_ = true;
    bool destroying_ = false;
    std::vector<Docked> docked_;
    std::vector<HWND> pages_;   // in tab order
    HWND active_ = nullptr;
};

} // namespace win32fx
