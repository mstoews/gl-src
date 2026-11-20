//
// ManilaGL.h - the main window: the original TManilaGL / TGlWindow
// (gl/src/gl.cpp, gl/header/gl_main.h, gl_man.h). Plan M5 task 3; since
// spec #8 S8-M0 a Workspace (navigation pane and tabs) instead of an MDI
// frame (spec #8 4, D3).
//
//   - MANILA menu (HGLMenu), built from the command table (Commands.h); the
//     Window menu is submenu 7, and lists the open pages.
//   - The navigation pane: the menu's popups as groups and its commands as
//     items, but the Window menu (the tabs do its job).
//   - Rights (spec #8 8, D6): every command's state comes from
//     cmd::Rights::MayRun with the user's menu code, in the menu, the toolbar
//     and the navigation pane alike.
//   - A toolbar at the top (the original TToolBar, dialog 1032) and a status
//     bar at the bottom (TVToolBar, dialog 2057), both the standard controls
//     since the UI refresh (spec 4.3, 4.4). The status bar's three parts are
//     the original's fields: 3222 company name, 3223 menu help, 3224 period.
//   - GL.INI [ACCESS] PSW=T: a login over the frame at startup; Cancel
//     closes the program. The menu-access string grays menu items on the
//     next WM_INITMENUPOPUP.
//   - A right-click on the empty page area opens the GLSUB popup (TGLClient).
//   - Menu commands outside Spec #1 go to the AP / AR / Reporting stubs, or
//     to a general "not yet ported" message (ManilaGL.cpp lists which).
//
// The GLE menu swap (spec 10 Q5) is gone: only the spreadsheet window
// (gl_xls.cpp, never ported) asked for it, and nothing called UseGleMenu.
//
#pragma once

#include "Commands.h"
#include "win32fx.h"

#include <optional>
#include <string>

class ManilaGL : public fx::Workspace {
public:
    HWND Create(HINSTANCE inst);

    // Rereads the company name and current period into the status fields.
    void RefreshStatus();

    void OnDpiChanged(UINT dpi) override;   // the toolbar's icons, the status bar's font

    // May the logged-in user run this command (the rights, spec #8 8)?
    bool MayRun(WORD id);

protected:
    bool IsCommandEnabled(WORD id) const override;
    void OnPageAreaContextMenu(POINT screen) override;
    bool OnCreate(LPCREATESTRUCTW cs) override;
    bool OnSize(UINT type, int cx, int cy) override;
    bool OnCommand(WORD id, WORD code, HWND ctl) override;
    bool OnDestroy() override;
    bool OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override;

private:
    void SetStatus(int field, const std::wstring& text);   // field: ID_STATUSHELP, ID_MENUHELP, ID_PERIODFIELD
    void MenuHelp(WPARAM wp, LPARAM lp);       // TGlWindow::WMMenuSelect
    void PopUpMenu(POINT screen);              // TGlWindow::PopUpMenu
    void NotPorted(WORD id);
    void RunReportList();                      // TGLCustomReport: dialog 1022 (spec #3 D9)
    std::wstring MenuText(WORD id) const;
    void BuildNavigation();
    void ApplyRights();   // the menu's and toolbar's commands, from the rights
    void RefreshWaiting();   // the status bar's waiting counts (spec #8 9.3)
    // GL_UISTATE (spec #8 10): the window, the navigation pane, the pinned
    // tabs and the active one, per user. Restored after the login.
    void RestoreState();
    void SaveState();
    void OnPinsChanged() override { SaveState(); }
    bool restored_ = false;

    fx::CommandBar toolbar_;
    std::optional<cmd::Rights> rights_;   // loaded at first use: accessm's order is the company's
    fx::StatusBar status_;
};
