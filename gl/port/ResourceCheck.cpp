//
// ResourceCheck.cpp - see ResourceCheck.h.
//
//   gl.exe /checkres           report in a message box
//   gl.exe /checkres /quiet    exit code = failures
//                              (from cmd: start /wait gl.exe /checkres /quiet
//                               then echo %errorlevel%)
//
// The dialog check also measures each caption against its control (UI
// refresh spec 4.6, D6): see checkTextFit. SaveScreenshots is gl.exe
// /screenshots (App.cpp).
//
#include "ResourceCheck.h"
#include "Commands.h"
#include "CommandsMenu.h"
#include "pages/CustomersPage.h"
#include "pages/RecordPage.h"
#include "pages/VoucherPages.h"
#include "pages/TaskPages.h"
#include "pages/HomePages.h"

#include "PortIds.h"

#include "win32fx.h"

#include <dwmapi.h>
#include <wincodec.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <string>
#include <vector>

#pragma comment(lib, "dwmapi.lib")        // DwmFlush, DwmGetWindowAttribute
#pragma comment(lib, "windowscodecs.lib") // CLSID_WICImagingFactory, the PNG encoder
#pragma comment(lib, "ole32.lib")

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

#include "LegacyIds.h"   // last: see LegacyIds.h (the picture IDs below)

namespace {

struct Report {
    int failures = 0;
    std::wstring log;
    void check(bool ok, const std::wstring& what)
    {
        log += ok ? L"ok      " : L"FAILED  ";
        log += what;
        log += L"\n";
        if (!ok) ++failures;
    }
};

INT_PTR CALLBACK ProbeProc(HWND, UINT, WPARAM, LPARAM) { return FALSE; }

// The buttons a screen gives a picture (MainDialog::SetBitmap: SBM_SETPICT with
// the bitmap of the same ID), so the text-fit check and the screenshots see
// the buttons as the screen shows them. Keep in step with the SetBitmap calls.
enum class Pics { None, Main };   // Main: MainDialog::OnInitDialog's set (kMainPictures)

const int kMainPictures[] = {IDCANCEL, IDOK, ID_NEW, ID_TOP, ID_BOTTOM, ID_NEXT, ID_PREVIOUS, ID_DELETE, ID_SLIPLIST};

struct DialogCase {
    int id;
    const wchar_t* what;
    bool hasGrid;   // contains a tbSpread grid (fx::Grid, Spec #2 S2-M0): checked to hold its ListView
    Pics pics = Pics::None;
    std::vector<int> morePics = {};   // the screen's own SetBitmap calls
    std::vector<std::pair<int, int>> borrowedPics = {};   // {button, picture}: a picture of another ID
};

// Spec 6.2 as corrected in M4. The three named in the M4 exit criteria come first.
const DialogCase kDialogs[] = {
    {1010, L"Chart of Accounts (TGLAcctsAdd / TGLAcctsDelete)", false, Pics::Main, {ID_GLINQ, ID_SETDATA}},
    {1158, L"Login (TGLAccess)", false, Pics::Main, {ID_ADD}},
    {1021, L"Company Profile (TGLProfile)", false, Pics::Main, {4000, 1125}},   // ID_SETPERIODS, ID_SETACCOUNTS
    {1008, L"About", false},
    {1028, L"Set Period Dates (TGLSetPeriodDates)", false, Pics::Main},
    {1032, L"Toolbar (TToolBar)", false},
    {1033, L"Account Setup (TGLSetAccounts)", false, Pics::Main},
    {1034, L"Cash Accounts (TAPCashAccts)", false, Pics::Main, {2236}},   // ID_CASHACCTS
    {1035, L"List Window Parameters (TSetQuery)", false},
    {2057, L"Module bar (TVToolBar)", false},
    {1159, L"User Access Update (TGLAdministration)", false, Pics::Main,
     {ID_MODIFY, ID_SEARCH, ID_ADD, ID_VIEW, 1094}},   // 1094: Login.cpp's ID_RESET
    {1011, L"Cash Accounts list (TAPCashAcctsList)", true},
    {1160, L"Menu Access (TAccessMenus)", true, Pics::Main},
    // Spec #2 S2-M2.
    {1023, L"Department Codes (TGLDeptCodes)", false, Pics::Main, {ID_GLINQ}},
    {1006, L"Medical Reps (TGLMedReps)", false, Pics::Main},
    {1040, L"Doctor Codes (TGLDrCodes)", false, Pics::Main},
    {1007, L"Product Codes (TGLProd)", false, Pics::Main},
    // Spec #2 S2-M3.
    {1013, L"Voucher Entry (TGLBatchEntry, TGLPrior)", false, Pics::Main, {4095, 1121, 3068}},   // Add, List, Print
    // Spec #2 S2-M4.
    {1061, L"Post General Ledger Voucher (TGLPostBatch)", true, Pics::Main},
    {1015, L"Recover General Ledger Voucher (TGLRecoverBatch)", true, Pics::Main},
    {1043, L"Cancel General Ledger Voucher (TGLCancelBatch)", true, Pics::Main},
    // Spec #2 S2-M5.
    {1029, L"Transaction Inquiry (TGLTransInq)", true, Pics::Main, {2000, ID_SETDATA}},   // 2000: kSearch
    {1017, L"Budget Data (TGLBudget)", true, Pics::Main, {ID_GLINQ, ID_SETDATA}},
    // Spec #3 S3-M7.
    {1022, L"Report list (TGLCustomReport, D9)", true},
    // Spec #4 S4-M1.
    {4002, L"Asset Types (TGLTypes)", false, Pics::Main, {1103}},   // kTypesButton (no bitmap 1103: no picture)
    // Spec #4 S4-M2.
    {4004, L"Fixed Assets Add/Modify (TGLFixedAssets)", false, Pics::Main, {1103, 1074}},
    // Spec #4 S4-M3.
    {4003, L"Create Voucher (TGLCreateDeprSlip)", false, Pics::Main},
    // Spec #4 S4-M4.
    {1001, L"Disposal (TGLFixedDelete)", false, Pics::Main, {1074}},
    // Spec #5 S5-M1.
    {1041, L"Vendors (TAPVendorAdd)", false, Pics::Main, {ID_SETDATA}, {{4082, ID_NEW}, {3043, 4054}}},
    // Spec #5 S5-M2.
    {1042, L"Invoice voucher entry (TAPInvoiceEntry)", false, Pics::Main, {1121}, {{3043, 4054}}},
    // Spec #5 S5-M3 (1043 and 1015, the Cancel and Recover lists, are Spec #2's).
    {1047, L"Post purchase vouchers (TAPPostBatch)", true, Pics::Main},
    {1030, L"Update AR and AP (TGLAPUpdate)", false, Pics::Main},
    // Spec #5 S5-M4.
    {1044, L"Cash voucher entry (TAPCashEntry)", false, Pics::Main, {1121}, {{3043, 4054}}},
    // Spec #5 S5-M5.
    {1045, L"Create Cheques (TAPCreatePayment)", true, Pics::Main, {1121}},
    {1066, L"Hold or Force (TAPHoldForce)", false},
    // Spec #5 S5-M6.
    {1046, L"Bank Reconciliation (TAPPayment)", true, Pics::Main},
    {1051, L"Clear Cancel Cheque (TAPSetChequeNumber)", false},
    {1048, L"Purge Paid Payables (TAPPurgeBatch)", true, Pics::Main},
    // Spec #6 S6-M1.
    {1052, L"Customers (TARVendorAdd)", false, Pics::Main, {3001, 1121, ID_SETDATA}},
    {1063, L"Customer Account Options (TARVendorOptions)", false, Pics::Main},
    {1053, L"Sales invoice voucher entry (TARInvoiceEntry)", false, Pics::Main, {1121}, {{3043, 4054}}},
    {1060, L"Sales voucher memo (TARMemo), with its menu MEMO", false, Pics::Main},
    // Spec #6 S6-M3 (1061, Post, and 1015, Recover, are Spec #2's).
    {1054, L"Cancel sales vouchers (TARCancelBatch)", true, Pics::Main},
    // Spec #6 S6-M4.
    {1156, L"Clear Receipts (TARCreatePayment)", true, Pics::Main, {1121}},
    {1155, L"Receipt (TARClear)", false, Pics::Main, {1094}},
    {1062, L"Purge Received Receivables (TARPurgeBatch)", true, Pics::Main},   // Spec #6 S6-M5
    // The port's own (gl/res/port.rc, spec #3 3).
    {IDD_SAMPLE_OPTIONS, L"Sample Report options (port.rc, S3-M1)", false},
    {IDD_TB_OPTIONS, L"Trial balance options (port.rc, S3-M2)", false},
    {IDD_STMT_OPTIONS, L"Financial statement options (port.rc, S3-M3)", false},
    {IDD_STMT_LAYOUT, L"Statement Layout (port.rc, S3-M4)", false},   // its grid is IDC_SL_GRID, not ID_DISTLIST
    {IDD_AP_CONFIRM, L"Confirm Printed Cheques (port.rc, S5-M6)", false},   // its grid is IDC_APC_GRID
    {IDD_RW_DESIGN, L"Report Designer (port.rc, spec #7 S7-M2)", false},
    {IDD_RW_CALC, L"Calculated Column (port.rc, spec #7 S7-M3)", false},
    {IDD_RW_PROMPT, L"Report questions (port.rc, spec #7 S7-M3)", false},
};

void applyPictures(HWND dlg, const DialogCase& d)
{
    auto set = [dlg](int id) {
        SendDlgItemMessageW(dlg, id, SBM_SETPICT, SUPERBTN_PICT_BITMAP, static_cast<LPARAM>(id));
    };
    if (d.pics == Pics::Main)
        for (const int id : kMainPictures) set(id);
    for (const int id : d.morePics) set(id);
    for (const auto& [button, picture] : d.borrowedPics)
        SendDlgItemMessageW(dlg, button, SBM_SETPICT, SUPERBTN_PICT_BITMAP, static_cast<LPARAM>(picture));
}

bool isChildTemplate(HWND dlg) { return (GetWindowLongPtrW(dlg, GWL_STYLE) & WS_CHILD) != 0; }

// ---- text fit (UI refresh spec 4.6) -------------------------------------------
//
// Each caption is measured in its control's font with DrawText(DT_CALCRECT)
// and compared with the room the control leaves for it. The allowances are
// what the v6 controls draw around the text at 96 DPI, scaled:
//   static            the client rectangle; word-wrapped unless SS_LEFTNOWORDWRAP
//                     or SS_SIMPLE, and then the lines must fit its height
//   check box, radio  16 px for the box and the gap
//   group box         16 px: the caption's indent and its gaps in the frame
//   push button       4 px each side, plus the picture and a 4 px gap
// Picture-only buttons (BS_BITMAP / BS_ICON, or the text "tbSuperBtn") and
// controls without text are skipped. Child templates (the toolbar 1032 and the
// module bar 2057) are skipped: code places their controls.

struct FitProblem {
    int id;
    std::wstring text;
    int need, room;
    bool lines;   // need/room are heights: the text wraps onto more lines than fit
};

bool same(const wchar_t* a, const wchar_t* b)   // ignoring case, as window class names compare
{
    return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

std::wstring windowText(HWND h)
{
    std::wstring s(static_cast<size_t>(GetWindowTextLengthW(h)) + 1, L'\0');
    s.resize(static_cast<size_t>(GetWindowTextW(h, s.data(), static_cast<int>(s.size()))));
    return s;
}

SIZE imageSize(HWND button)
{
    SIZE sz{};
    if (auto bmp = reinterpret_cast<HBITMAP>(SendMessageW(button, BM_GETIMAGE, IMAGE_BITMAP, 0))) {
        BITMAP bm{};
        if (GetObjectW(bmp, sizeof bm, &bm)) sz = {bm.bmWidth, bm.bmHeight};
    } else if (auto icon = reinterpret_cast<HICON>(SendMessageW(button, BM_GETIMAGE, IMAGE_ICON, 0))) {
        ICONINFO ii{};
        if (GetIconInfo(icon, &ii)) {
            BITMAP bm{};
            if (GetObjectW(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof bm, &bm))
                sz = {bm.bmWidth, ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2};
            if (ii.hbmColor) DeleteObject(ii.hbmColor);
            if (ii.hbmMask) DeleteObject(ii.hbmMask);
        }
    }
    return sz;
}

// The text's extent in `ctl`'s font: one line, or wrapped at `wrapWidth` (> 0).
RECT measure(HWND ctl, const std::wstring& text, UINT format, int wrapWidth)
{
    RECT rc{0, 0, wrapWidth > 0 ? wrapWidth : 0, 0};
    HDC dc = GetDC(ctl);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(ctl, WM_GETFONT, 0, 0));
    if (!font) font = reinterpret_cast<HFONT>(SendMessageW(GetParent(ctl), WM_GETFONT, 0, 0));
    const HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    DrawTextW(dc, text.c_str(), -1, &rc,
              DT_CALCRECT | DT_EXPANDTABS | format | (wrapWidth > 0 ? DT_WORDBREAK : DT_SINGLELINE));
    if (old) SelectObject(dc, old);
    ReleaseDC(ctl, dc);
    return rc;
}

std::vector<FitProblem> checkTextFit(HWND dlg)
{
    std::vector<FitProblem> problems;
    for (HWND ctl = GetWindow(dlg, GW_CHILD); ctl; ctl = GetWindow(ctl, GW_HWNDNEXT)) {
        const std::wstring text = windowText(ctl);
        if (text.empty()) continue;
        wchar_t cls[32] = {};
        GetClassNameW(ctl, cls, 32);
        const LONG_PTR style = GetWindowLongPtrW(ctl, GWL_STYLE);
        const int id = GetDlgCtrlID(ctl);
        const int dpi = static_cast<int>(GetDpiForWindow(ctl));
        auto px = [dpi](int v) { return MulDiv(v, dpi, 96); };
        RECT client{};
        GetClientRect(ctl, &client);
        const int width = client.right, height = client.bottom;

        if (same(cls, L"Static")) {
            const LONG_PTR type = style & SS_TYPEMASK;
            if (type != SS_LEFT && type != SS_CENTER && type != SS_RIGHT && type != SS_LEFTNOWORDWRAP &&
                type != SS_SIMPLE)
                continue;   // pictures, frames, owner-drawn
            const UINT prefix = (style & SS_NOPREFIX) ? DT_NOPREFIX : 0;
            if (type == SS_LEFTNOWORDWRAP || type == SS_SIMPLE) {
                const RECT r = measure(ctl, text, prefix, 0);
                if (r.right > width) problems.push_back({id, text, r.right, width, false});
            } else {
                const RECT r = measure(ctl, text, prefix, width);
                if (r.right > width) problems.push_back({id, text, r.right, width, false});   // a word too long
                else if (r.bottom > height) problems.push_back({id, text, r.bottom, height, true});
            }
            continue;
        }
        if (!same(cls, L"Button") && !same(cls, L"tbSuperBtn")) continue;
        const LONG_PTR type = style & BS_TYPEMASK;
        int room = width;
        switch (type) {
        case BS_CHECKBOX: case BS_AUTOCHECKBOX: case BS_3STATE: case BS_AUTO3STATE:
        case BS_RADIOBUTTON: case BS_AUTORADIOBUTTON:
            room = width - px(16);
            break;
        case BS_GROUPBOX:
            room = width - px(16);
            break;
        case BS_PUSHBUTTON: case BS_DEFPUSHBUTTON: {
            if (style & (BS_BITMAP | BS_ICON)) continue;   // picture only
            if (text == L"tbSuperBtn") continue;
            const SIZE image = imageSize(ctl);
            room = width - 2 * px(4) - (image.cx ? image.cx + px(4) : 0);
            break;
        }
        default:
            continue;
        }
        if (style & BS_MULTILINE) {
            const RECT r = measure(ctl, text, 0, (std::max)(room, 1));
            if (r.bottom > height) problems.push_back({id, text, r.bottom, height, true});
        } else {
            const RECT r = measure(ctl, text, 0, 0);
            if (r.right > room) problems.push_back({id, text, r.right, room, false});
        }
    }
    return problems;
}

void checkDialogs(HINSTANCE inst, Report& r)
{
    // A hidden owner/parent: 1032 and 2057 are WS_CHILD templates.
    HWND parent = CreateWindowExW(0, L"STATIC", L"resource check", WS_POPUP, 0, 0, 400, 300, nullptr,
                                  nullptr, inst, nullptr);
    for (const DialogCase& d : kDialogs) {
        const std::wstring name = fx::Format(L"dialog %d, %s", d.id, d.what);
        const bool present = FindResourceW(inst, MAKEINTRESOURCEW(d.id), MAKEINTRESOURCEW(5)) != nullptr;  // RT_DIALOG
        if (!present) {
            r.check(false, name + L": template missing");
            continue;
        }
        HWND dlg = CreateDialogParamW(inst, MAKEINTRESOURCEW(d.id), parent, ProbeProc, 0);
        r.check(dlg != nullptr, name);
        if (dlg && d.hasGrid) r.check(fx::Grid(GetDlgItem(dlg, 1075)).IsGrid(), name + L": tbSpread grid (ID_DISTLIST)");
        if (dlg && !isChildTemplate(dlg)) {
            applyPictures(dlg, d);
            const auto problems = checkTextFit(dlg);
            for (const FitProblem& p : problems)
                r.check(false, fx::Format(p.lines ? L"dialog %d, control %d \"%s\": text wraps to %d px high, room %d px"
                                                  : L"dialog %d, control %d \"%s\": text too wide: %d px, room %d px",
                                          d.id, p.id, p.text.c_str(), p.need, p.room));
            if (problems.empty()) r.check(true, fx::Format(L"dialog %d: every caption fits", d.id));
        }
        if (dlg) DestroyWindow(dlg);
    }
    if (parent) DestroyWindow(parent);
}

void checkMenus(HINSTANCE inst, Report& r)
{
    for (const wchar_t* name : {L"MANILA", L"GLE", L"GLSUB"}) {
        fx::MenuGuard menu(LoadMenuW(inst, name));
        r.check(static_cast<bool>(menu), fx::Format(L"menu %s", name));
        if (menu && wcscmp(name, L"MANILA") == 0) {
            wchar_t text[32] = {};
            GetMenuStringW(menu.get(), 7, text, 32, MF_BYPOSITION);
            r.check(std::wstring(text) == L"&Window", L"MANILA: Window menu at position 7");
        }
        // Spec #3 D9: Custom Report has its own command; File > Print keeps 3068.
        if (menu && wcscmp(name, L"GLE") != 0) {
            wchar_t text[32] = {};
            GetMenuStringW(menu.get(), CM_REPORT_LIST, text, 32, MF_BYCOMMAND);
            r.check(std::wstring(text) == L"&Custom Report...", fx::Format(L"%s: Custom Report is CM_REPORT_LIST", name));
        }
    }
    // Spec #8 S8-M0: the menu the program shows is built from the command
    // table; it must be MANILA, item by item.
    fx::MenuGuard manila(LoadMenuW(inst, L"MANILA"));
    fx::MenuGuard built(BuildCommandMenu());
    std::wstring why;
    const bool same = SameMenus(built.get(), manila.get(), why);
    r.check(same, same ? fx::Format(L"the command table's menu is MANILA (%u commands)",
                                    static_cast<unsigned>(cmd::CommandIds().size()))
                       : L"the command table's menu is MANILA: " + why);
}

// Spec #8 S8-M0: the workspace opens, hosts pages in tabs, switches and closes
// them, and lists them in the Window menu.
class CheckPage : public fx::HostedPage {};

class CheckWorkspace : public fx::Workspace {
protected:
    bool OnDestroy() override { return true; }   // not the main window
};

// Spec #8 S8-M6, 12: a page's controls as a screen reader and the keyboard
// meet them. Every focusable control has a name (after the page's own naming,
// fx::NameUnnamedControls); Tab reaches every one; within a group, Tab goes in
// reading order (top to bottom, then left to right); every push button has an
// access key.
void checkAccess(HWND form, int id, const std::wstring& what, Report& r)
{
    auto ids = [](const std::vector<HWND>& v) {
        std::wstring s;
        for (HWND h : v) s += L" " + std::to_wstring(GetDlgCtrlID(h));
        return s;
    };
    const std::vector<HWND> unnamed = fx::UnnamedControls(form);   // the page has named its own
    r.check(unnamed.empty(), fx::Format(L"page %d, %s: every control has an accessible name%s", id, what.c_str(),
                                        unnamed.empty() ? L"" : (L" (unnamed:" + ids(unnamed) + L")").c_str()));
    const std::vector<HWND> unreachable = fx::UnreachableControls(form);
    r.check(unreachable.empty(), fx::Format(L"page %d, %s: Tab reaches every control%s", id, what.c_str(),
                                            unreachable.empty() ? L"" : (L" (not:" + ids(unreachable) + L")").c_str()));
    const auto order = fx::TabOrderProblems(form);
    std::wstring pairs;
    for (const auto& [a, b] : order) pairs += fx::Format(L" %d->%d", GetDlgCtrlID(a), GetDlgCtrlID(b));
    r.check(order.empty(), fx::Format(L"page %d, %s: Tab follows reading order within each group%s", id, what.c_str(),
                                      order.empty() ? L"" : (L" (out of order:" + pairs + L")").c_str()));
    std::vector<HWND> noKey;
    for (HWND c = GetWindow(form, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        wchar_t cls[16] = {};
        GetClassNameW(c, cls, 16);
        const LONG t = GetWindowLongW(c, GWL_STYLE) & BS_TYPEMASK;
        if (_wcsicmp(cls, L"Button") != 0 || (t != BS_PUSHBUTTON && t != BS_DEFPUSHBUTTON)) continue;
        if (fx::GetDlgItemString(form, GetDlgCtrlID(c)).find(L'&') == std::wstring::npos) noKey.push_back(c);
    }
    r.check(noKey.empty(), fx::Format(L"page %d, %s: every button has an access key%s", id, what.c_str(),
                                      noKey.empty() ? L"" : (L" (none:" + ids(noKey) + L")").c_str()));
}

// Spec #8 S8-M1: each page's form loads as a child, its captions fit, and
// every control has an anchor.
void checkPages(HINSTANCE inst, Report& r)
{
    HWND parent = CreateWindowExW(0, L"STATIC", L"page check", WS_POPUP, 0, 0, 800, 600, nullptr, nullptr, inst, nullptr);
    struct PageCase {
        int id;
        std::wstring what;
        const std::vector<std::pair<int, unsigned>>& anchors;
    };
    std::vector<PageCase> pages = {{IDD_PG_CUSTOMERS, L"Customers page", CustomersPageAnchors()}};
    for (const PageDesc* d : RecordPageDescs())   // spec #8 S8-M2, generated by make_pages.py
        pages.push_back({static_cast<int>(d->form), std::wstring(d->title) + L" page", d->anchors});
    for (const VoucherPageForm& f : VoucherPageForms())   // spec #8 S8-M3
        pages.push_back({f.form, f.what, *f.anchors});
    pages.push_back({IDD_PG_TASK, L"Task page", TaskPageAnchors()});   // S8-M4
    pages.push_back({IDD_PG_HOME, L"Home page", HomePageAnchors()});   // S8-M5
    pages.push_back({IDD_PG_GALLERY, L"Report gallery", GalleryPageAnchors()});
    for (const PageCase& p : pages) {
        HWND form = CreateDialogParamW(inst, MAKEINTRESOURCEW(p.id), parent, ProbeProc, 0);
        r.check(form && isChildTemplate(form), fx::Format(L"page %d, %s: the form loads as a child", p.id, p.what.c_str()));
        if (!form) continue;
        const auto problems = checkTextFit(form);
        for (const FitProblem& f : problems)
            r.check(false, fx::Format(f.lines ? L"page %d, control %d \"%s\": text wraps to %d px high, room %d px"
                                              : L"page %d, control %d \"%s\": text too wide: %d px, room %d px",
                                      p.id, f.id, f.text.c_str(), f.need, f.room));
        if (problems.empty()) r.check(true, fx::Format(L"page %d: every caption fits", p.id));
        const std::vector<int> loose = fx::Layout::Unanchored(form, p.anchors);
        std::wstring ids;
        for (int id : loose) ids += L" " + std::to_wstring(id);
        r.check(loose.empty(), fx::Format(L"page %d: every control has an anchor%s", p.id,
                                          loose.empty() ? L"" : (L" (missing:" + ids + L")").c_str()));
        DestroyWindow(form);
    }
    DestroyWindow(parent);
}

void checkWorkspace(HINSTANCE inst, Report& r)
{
    fx::MenuGuard menu(BuildCommandMenu());
    CheckWorkspace w;
    if (!w.CreateWorkspace(inst, L"GLCheckWorkspace", L"check", menu.get(), 7)) {
        r.check(false, L"workspace: created");
        return;
    }
    r.check(w.navigation() != nullptr, L"workspace: created, with its navigation pane");
    CheckPage a, b;
    const HWND ha = a.CreatePage(w, L"GLCheckPage", L"First");
    const HWND hb = b.CreatePage(w, L"GLCheckPage", L"Second");
    r.check(ha && hb && w.pages().size() == 2 && w.activePage() == hb, L"workspace: two pages, the second active");
    w.ActivatePage(ha);
    // The workspace itself is hidden here, so IsWindowVisible is false for any
    // child: the pages' own WS_VISIBLE says which one is shown.
    auto shown = [](HWND h) { return (GetWindowLongW(h, GWL_STYLE) & WS_VISIBLE) != 0; };
    r.check(w.activePage() == ha && shown(ha) && !shown(hb), L"workspace: switching shows one page");
    const RECT page = w.pageRect();
    RECT pr{};
    GetWindowRect(ha, &pr);
    MapWindowPoints(nullptr, w.hwnd(), reinterpret_cast<POINT*>(&pr), 2);
    r.check(EqualRect(&page, &pr) != FALSE, L"workspace: the page fills the page area");
    HMENU windowMenu = GetSubMenu(menu.get(), 7);
    SendMessageW(w.hwnd(), WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(windowMenu), 7);
    wchar_t text[64] = {};
    GetMenuStringW(windowMenu, GetMenuItemCount(windowMenu) - 1, text, 64, MF_BYPOSITION);
    r.check(std::wstring(text) == L"&2 Second", L"workspace: the Window menu lists the pages");
    w.ClosePage(ha);
    r.check(w.pages().size() == 1 && w.activePage() == hb && !IsWindow(ha), L"workspace: closing a page activates the next");
    b.ClosePage();
    r.check(w.pages().empty() && w.activePage() == nullptr, L"workspace: the last page closed");
    DestroyWindow(w.hwnd());
}

BOOL CALLBACK collectName(HMODULE, LPCWSTR, LPWSTR name, LONG_PTR param)
{
    auto* names = reinterpret_cast<std::vector<std::wstring>*>(param);
    names->push_back(IS_INTRESOURCE(name) ? fx::Format(L"#%u", static_cast<unsigned>(reinterpret_cast<ULONG_PTR>(name)))
                                          : std::wstring(name));
    return TRUE;
}

LPCWSTR resName(const std::wstring& n)
{
    return n[0] == L'#' ? MAKEINTRESOURCEW(std::stoi(n.substr(1))) : n.c_str();
}

void checkImages(HINSTANCE inst, Report& r)
{
    std::vector<std::wstring> bitmaps;
    EnumResourceNamesW(inst, MAKEINTRESOURCEW(2), collectName, reinterpret_cast<LONG_PTR>(&bitmaps));  // RT_BITMAP
    int loaded = 0;
    for (const auto& n : bitmaps) {
        fx::BitmapGuard bmp(static_cast<HBITMAP>(LoadImageW(inst, resName(n), IMAGE_BITMAP, 0, 0, 0)));
        if (bmp) {
            ++loaded;
        } else {
            r.check(false, L"bitmap " + n);
        }
    }
    r.check(bitmaps.size() == 83 && loaded == 83, fx::Format(L"%d of %d bitmaps load (83 expected)", loaded,
                                                              static_cast<int>(bitmaps.size())));
    for (const wchar_t* n : {L"STATUSBAR", L"PLUS", L"MINUS"}) {
        fx::BitmapGuard bmp(static_cast<HBITMAP>(LoadImageW(inst, n, IMAGE_BITMAP, 0, 0, 0)));
        r.check(static_cast<bool>(bmp), fx::Format(L"bitmap %s", n));
    }
    for (const wchar_t* n : {L"GL", L"SHEET"}) {
        r.check(LoadIconW(inst, n) != nullptr, fx::Format(L"icon %s", n));
    }
    r.check(LoadIconW(inst, MAKEINTRESOURCEW(1111)) != nullptr, L"icon 1111");

    // UI refresh (spec 4.2): every P<resource> icon (icons/fluent/map.csv) loads
    // at 16 and 48 px, and replaces a bitmap that exists.
    std::vector<std::wstring> icons;
    EnumResourceNamesW(inst, MAKEINTRESOURCEW(14), collectName, reinterpret_cast<LONG_PTR>(&icons));   // RT_GROUP_ICON
    int modern = 0;
    for (const auto& n : icons) {
        if (n.size() < 2 || n[0] != L'P' || n[1] == L'#') continue;   // e.g. "PPLUS", "P4055"; not GL, SHEET, #1111
        ++modern;
        const std::wstring bitmap = n.substr(1);
        const bool numeric = bitmap.find_first_not_of(L"0123456789") == std::wstring::npos;
        const LPCWSTR res = numeric ? MAKEINTRESOURCEW(std::stoi(bitmap)) : bitmap.c_str();
        HICON icon16 = fx::LoadModernIcon(inst, res, 16), icon48 = fx::LoadModernIcon(inst, res, 48);
        const bool hasBitmap = FindResourceW(inst, res, MAKEINTRESOURCEW(2)) != nullptr;   // RT_BITMAP
        r.check(icon16 && icon48 && hasBitmap, L"icon " + n + (hasBitmap ? L"" : L": no bitmap " + bitmap));
        if (icon16) DestroyIcon(icon16);
        if (icon48) DestroyIcon(icon48);
    }
    r.check(modern == 31, fx::Format(L"%d modern icons (31 expected: icons/fluent/map.csv)", modern));
    r.check(LoadAcceleratorsW(inst, L"GL") != nullptr, L"accelerators GL");
}

void checkStrings(HINSTANCE inst, Report& r)
{
    r.check(fx::LoadResString(inst, 4024) == L"MM/DD/YY", L"string 4024 = MM/DD/YY (tbDate display format)");
    r.check(fx::LoadResString(inst, 1021) == L"Are you sure you wish to delete this account?", L"string 1021");
    r.check(!fx::LoadResString(inst, 8095).empty(), L"string 8095");
}

} // namespace

int RunResourceCheck(HINSTANCE inst, bool quiet)
{
    Report r;
    checkDialogs(inst, r);
    checkMenus(inst, r);
    checkWorkspace(inst, r);
    checkPages(inst, r);
    checkImages(inst, r);
    checkStrings(inst, r);
    if (!quiet) {
        r.log += r.failures ? fx::Format(L"\n%d check(s) FAILED", r.failures) : std::wstring(L"\nAll checks passed");
        MessageBoxW(nullptr, r.log.c_str(), L"gl.exe resource check", r.failures ? MB_ICONERROR : MB_ICONINFORMATION);
    }
    return r.failures;
}

// ---- screenshots (UI refresh spec 4.6) ---------------------------------------

namespace {

template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com()
    {
        if (p) p->Release();
    }
    T* operator->() const noexcept { return p; }
};

bool savePng(HBITMAP bmp, int w, int h, const std::wstring& path)
{
    Com<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                reinterpret_cast<void**>(&factory.p))))
        return false;
    Com<IWICBitmap> source;
    Com<IWICStream> stream;
    Com<IWICBitmapEncoder> encoder;
    Com<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    return SUCCEEDED(factory->CreateBitmapFromHBITMAP(bmp, nullptr, WICBitmapIgnoreAlpha, &source.p)) &&
           SUCCEEDED(factory->CreateStream(&stream.p)) &&
           SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
           SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder.p)) &&
           SUCCEEDED(encoder->Initialize(stream.p, WICBitmapEncoderNoCache)) &&
           SUCCEEDED(encoder->CreateNewFrame(&frame.p, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
           SUCCEEDED(frame->SetSize(static_cast<UINT>(w), static_cast<UINT>(h))) &&
           SUCCEEDED(frame->SetPixelFormat(&format)) && SUCCEEDED(frame->WriteSource(source.p, nullptr)) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

HBITMAP createDib(HDC dc, int w, int h)
{
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    return CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
}

// Paints `wnd` and its children now and waits for the compositor, so that
// PrintWindow(PW_RENDERFULLCONTENT) sees the finished window.
void settle(HWND wnd)
{
    RedrawWindow(wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    MSG msg;
    while (PeekMessageW(&msg, nullptr, WM_PAINT, WM_PAINT, PM_REMOVE)) DispatchMessageW(&msg);   // nothing else:
    DwmFlush();                                                                                   // no posted commands
}

} // namespace

bool SaveWindowPng(HWND wnd, const std::wstring& path)
{
    settle(wnd);
    RECT win{};
    GetWindowRect(wnd, &win);
    // The visible frame: GetWindowRect includes the invisible resize borders.
    RECT vis = win;
    if (FAILED(DwmGetWindowAttribute(wnd, DWMWA_EXTENDED_FRAME_BOUNDS, &vis, sizeof vis))) vis = win;
    const int w = win.right - win.left, h = win.bottom - win.top;
    const int vw = vis.right - vis.left, vh = vis.bottom - vis.top;
    if (w <= 0 || h <= 0 || vw <= 0 || vh <= 0) return false;

    HDC screen = GetDC(nullptr);
    HDC whole = CreateCompatibleDC(screen);
    HDC cropped = CreateCompatibleDC(screen);
    HBITMAP wholeBmp = createDib(screen, w, h);
    HBITMAP croppedBmp = createDib(screen, vw, vh);
    bool ok = false;
    if (whole && cropped && wholeBmp && croppedBmp) {
        const HGDIOBJ old1 = SelectObject(whole, wholeBmp);
        const HGDIOBJ old2 = SelectObject(cropped, croppedBmp);
        ok = PrintWindow(wnd, whole, PW_RENDERFULLCONTENT) &&
             BitBlt(cropped, 0, 0, vw, vh, whole, vis.left - win.left, vis.top - win.top, SRCCOPY);
        SelectObject(whole, old1);
        SelectObject(cropped, old2);
        ok = ok && savePng(croppedBmp, vw, vh, path);
    }
    if (wholeBmp) DeleteObject(wholeBmp);
    if (croppedBmp) DeleteObject(croppedBmp);
    if (whole) DeleteDC(whole);
    if (cropped) DeleteDC(cropped);
    ReleaseDC(nullptr, screen);
    return ok;
}

// The pages, by the command that opens each (spec #8 S8-M6): /checkaccess and /screenshots.
std::vector<WORD> pageCommands()
{
    std::vector<WORD> v = {CM_HOME, CM_REPORT_LIST, 4059, CM_VOUCHER_LIST};
    for (const ClassicCommand& c : ClassicCommands()) v.push_back(c.page);
    for (const ClassicCommand& c : VoucherClassicCommands()) v.push_back(c.page);
    for (const ClassicCommand& c : TaskClassicCommands()) v.push_back(c.page);
    return v;
}

HWND openPage(fx::PageHost& host, WORD c)
{
    if (c == CM_HOME) return OpenHomePage(host);
    if (c == CM_REPORT_LIST) return OpenReportGallery(host);
    if (c == 4059) return OpenCustomersPage(host);
    if (IsRecordPageCommand(c)) return OpenRecordPage(host, c);
    if (IsVoucherPageCommand(c)) return OpenVoucherPage(host, c);
    if (IsTaskPageCommand(c)) return OpenTaskPage(host, c);
    return nullptr;
}

int RunAccessCheck(HINSTANCE inst, bool quiet)
{
    class CheckHost : public fx::Workspace {
    protected:
        bool OnDestroy() override { return true; }   // not the main window
    };
    Report r;
    CheckHost w;
    if (!w.CreateWorkspace(inst, L"GLAccessCheck", L"access check", nullptr, -1)) {
        r.check(false, L"workspace: created");
    } else {
        for (WORD c : pageCommands()) {
            HWND h = openPage(w, c);
            auto* page = dynamic_cast<fx::Page*>(fx::Window::FromHandle(h));
            if (!page || !page->form()) {
                r.check(false, fx::Format(L"command %u: its page opens", c));
                continue;
            }
            const std::wstring what = page->title();
            int dlgId = 0;
            (void)dlgId;
            checkAccess(page->form(), c, what, r);
            w.ClosePage(h);
        }
        DestroyWindow(w.hwnd());
    }
    if (!quiet) {
        if (r.failures) r.log += fx::Format(L"\n%d check(s) FAILED", r.failures);
        else r.log += L"\nAll checks passed.";
        MessageBoxW(nullptr, r.log.c_str(), L"gl.exe access check", r.failures ? MB_ICONERROR : MB_ICONINFORMATION);
    }
    return r.failures;
}

int SaveScreenshots(HINSTANCE inst, HWND frame, const std::wstring& dir, bool quiet)
{
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const UINT dpi = GetDpiForWindow(frame);
    auto file = [&](const std::wstring& name) {
        return (std::filesystem::path(dir) / fx::Format(L"%s-%u.png", name.c_str(), dpi)).wstring();
    };

    Report r;
    r.check(SaveWindowPng(frame, file(L"frame")), L"frame");
    for (const DialogCase& d : kDialogs) {
        HWND dlg = CreateDialogParamW(inst, MAKEINTRESOURCEW(d.id), frame, ProbeProc, 0);
        if (!dlg) {
            r.check(false, fx::Format(L"dialog %d: not created", d.id));
            continue;
        }
        if (isChildTemplate(dlg)) {   // 1032, 2057: in frame.png
            DestroyWindow(dlg);
            continue;
        }
        applyPictures(dlg, d);
        ShowWindow(dlg, SW_SHOWNOACTIVATE);
        r.check(SaveWindowPng(dlg, file(std::to_wstring(d.id))), fx::Format(L"dialog %d, %s", d.id, d.what));
        DestroyWindow(dlg);
    }
    if (auto* w = dynamic_cast<fx::Workspace*>(fx::Window::FromHandle(frame))) {   // spec #8 S8-M6: the pages
        for (WORD c : pageCommands()) {
            HWND h = openPage(*w, c);
            if (!h) {
                r.check(false, fx::Format(L"page of command %u: not opened", c));
                continue;
            }
            RedrawWindow(frame, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            r.check(SaveWindowPng(frame, file(fx::Format(L"page-%u", c))), fx::Format(L"page of command %u", c));
            w->ClosePage(h);
        }
    }
    if (SUCCEEDED(init)) CoUninitialize();

    if (!quiet) {
        r.log += fx::Format(L"\nSaved to %s, at %u DPI (%u%%).", dir.c_str(), dpi, dpi * 100 / 96);
        if (r.failures) r.log += fx::Format(L"\n%d screenshot(s) FAILED", r.failures);
        MessageBoxW(frame, r.log.c_str(), L"gl.exe screenshots", r.failures ? MB_ICONERROR : MB_ICONINFORMATION);
    }
    return r.failures;
}

