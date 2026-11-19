//
// ManilaGL.cpp - see ManilaGL.h.
//
#include "ManilaGL.h"

#include "ApInvoiceEntry.h"
#include "ApPayments.h"
#include "ApPosting.h"
#include "ApTransfer.h"
#include "ApVendors.h"
#include "ArCustomers.h"
#include "ArInvoiceEntry.h"
#include "ArPosting.h"
#include "ArReceipts.h"
#include "Commands.h"
#include "UiState.h"
#include "pages/CustomersPage.h"
#include "pages/RecordPage.h"
#include "pages/VoucherPages.h"
#include "pages/TaskPages.h"
#include "DataChanged.h"
#include "model/JobState.h"
#include "Company.h"
#include "ChartOfAccounts.h"
#include "FixedAssetTypes.h"
#include "FixedAssets.h"
#include "DepreciationRun.h"
#include "FixedAssetDisposal.h"
#include "MasterData.h"
#include "VoucherBrowser.h"
#include "VoucherEntry.h"
#include "VoucherPosting.h"
#include "TransactionInquiry.h"
#include "Backup.h"
#include "YearEnd.h"
#include "CompanyProfile.h"
#include "Globals.h"
#include "Login.h"
#include "PortIds.h"
#include "reports/ReportList.h"
#include "reports/RunReport.h"
#include "reports/StatementLayoutDialog.h"
#include "stubs/Stubs.h"

#include "report/win32/Preview.h"
#include "report/win32/PrinterSetup.h"

#include <CommCtrl.h>
#pragma comment(lib, "comctl32.lib")   // SetWindowSubclass

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <cstring>
#include <exception>
#include <iterator>
#include <string>

#include "LegacyIds.h"   // last: see LegacyIds.h

namespace {

constexpr int kWindowMenuPos = 7;            // TManilaGL: ChildMenuPos = 7
constexpr UINT kRestoreState = WM_APP + 10;   // GL_UISTATE, once the window is shown (spec #8 10)

// Which stub each menu command outside Spec #1 goes to. The command IDs and
// handler names are TGlWindow's and TManilaGL's response tables
// (gl/header/gl_main.h, gl_man.h). A handler whose body is a PrintReport()
// call (gl/src/ap_repts.cpp) is Reporting, whichever module it belongs to;
// the other handlers in gl_man.h's "Accounts Payable" / "Accounts
// Receivable" sections are AP / AR. Everything else is General Ledger.
const WORD kApCommands[] = {
    1084,  // APPostAdj        ID_APPOSTADJ
    4046,  // APDelVendors     ID_DELVENDOR
    4047,  // APModVendors     ID_MODVENDOR
    8079,  // AccountsPayable  CM_ACCOUNT_PAYABLE (an empty handler in gl.cpp)
};
const WORD kArCommands[] = {
    8080,  // AccountsReceive  CM_ACCOUNT_RECEIVABLE (an empty handler in gl.cpp)
};
// TManilaGL::GLPrePosting; no legacy header names it.
constexpr WORD kPrePostingReport = 8136;
// TGlWindow::GLFinFA / GLFinDep (gl_xls.cpp): ID_FINFA / ID_FINDEP have no value in the headers.
constexpr WORD kFixedAssetListing = 1019;
constexpr WORD kFixedAssetTypes = 1020;

// The reports Spec #3 doesn't port (spec #3 1.2): they go with their modules.
// Every General Ledger report is ported (S3-M1 to S3-M7); 1025 left the menu
// (D13), and 6077 (GLAccounts), on no menu or toolbar, isn't ported (D11).
// The fixed-asset reports 1019 and 1020 are ported with Spec #4 (S4-M5), the
// AP reports with Spec #5 (S5-M7, RunApReport; 1082 and 1990, the adjustment
// reports, aren't: there are no adjustments, spec #5 7).
// The AR reports with Spec #6 (S6-M6, RunArReport); 8137 (ar_cust.rpt) existed
// nowhere and nothing called it: not ported (spec #6 7).
const WORD kArReports[] = {
    8137,
};

template <size_t N>
bool contains(const WORD (&list)[N], WORD id)
{
    return std::find(std::begin(list), std::end(list), id) != std::end(list);
}

// ---- the top toolbar --------------------------------------------------------

WORD toolbarCommand(WORD id)
{
    // TToolBar (gl/header/gl_tool.h, gl/src/gl_toolb.cpp): button -> the command it sent.
    switch (id) {
    case ID_GLADD:      return ID_ACCTADD;
    case ID_GLBATCH:    return ID_MODBATCH;
    case ID_ARBATCHENT: return ID_ARBATCH;
    case ID_CASH:       return ID_APCASH;
    case ID_GLPOST:     return ID_GLPOSTBATCH;
    case ID_GLINQ:      return ID_GLCHARTINQ;
    case ID_CANCEL:     return fx::CM_EXIT;   // GLQuit: Mssg(24340)
    case ID_NEW:        return 111;
    default:            return id;            // ID_PRINT, ID_OPEN, ID_SAVE, ID_APBATCH
    }
}

// TToolBar::SetupWindow's buttons, in its order; `gap` where it left a wider
// space (pos += 1.3), now a separator. The spreadsheet buttons it placed hidden
// (cut, copy, paste, border, font, alignment, number, grids) are gone: no
// spreadsheet window was ever ported to show them.
struct ToolButton {
    WORD id;   // the original button ID: its bitmap, and so its icon P<id>
    UINT tip;
    bool gap;
};
const ToolButton kToolButtons[] = {
    {ID_NEW, IDS_TIP_NEW, false},         {ID_OPEN, IDS_TIP_OPEN, false},
    {ID_SAVE, IDS_TIP_SAVE, false},       {ID_PRINT, IDS_TIP_PRINT, true},
    {ID_GLBATCH, IDS_TIP_GLBATCH, false}, {ID_APBATCH, IDS_TIP_APBATCH, false},
    {ID_CASH, IDS_TIP_CASH, false},       {ID_ARBATCHENT, IDS_TIP_ARBATCH, true},
    {ID_GLINQ, IDS_TIP_GLINQ, false},     {ID_GLPOST, IDS_TIP_GLPOST, false},
    {ID_GLADD, IDS_TIP_GLADD, true},      {ID_CANCEL, IDS_TIP_EXIT, false},
};

} // namespace

// ---- the frame ----------------------------------------------------------------

HWND ManilaGL::Create(HINSTANCE inst)
{
    // TGlWindow::GetClassName: "GLWindow"; GetWindowClass: icon "GL".
    return CreateWorkspace(inst, L"GLWindow", szApp, HGLMenu, kWindowMenuPos, LoadIconW(inst, L"GL"));
}

bool ManilaGL::OnCreate(LPCREATESTRUCTW cs)
{
    Workspace::OnCreate(cs);

    // TGlWindow::SetupWindow: the toolbar (1032) and the module bar (2057),
    // now the standard controls (UI refresh spec 4.3, 4.4). The status bar is
    // docked first: it sits at the very bottom.
    status_.Create(hwnd(), IDC_FRAME_STATUS, hInst);
    Dock(status_, fx::DockSide::Bottom, 0);
    toolbar_.Create(hwnd(), IDC_FRAME_TOOLBAR, hInst);
    for (const ToolButton& b : kToolButtons) {
        toolbar_.AddButton(b.id, toolbarCommand(b.id), fx::LoadResString(hInst, b.tip));
        if (b.gap) toolbar_.AddSeparator();
    }
    Dock(toolbar_, fx::DockSide::Top, toolbar_.Height());

    BuildNavigation();

    // TManilaGL::SetupWindow. ID_GL isn't in gl_new.rc's MANILA; kept for parity.
    CheckMenuItem(HGLMenu, ID_GL, MF_BYCOMMAND | MF_CHECKED);

    // TGlWindow::SetupWindow: the login, if GL.INI asks for one.
    if (LoginRequired()) {
        bMenuSetup = FALSE;
        PostMessageW(hwnd(), WM_COMMAND, ID_SYSACCESS, 0);
    } else {
        strcpy_s(szUser, "NOLOGIN");   // fits; the original wrote strcmp(szUser, "NOLOGIN"), a no-op
        bMenuSetup = TRUE;
        PostMessageW(hwnd(), kRestoreState, 0, 0);   // after wWinMain shows the window
    }
    RefreshStatus();
    SetDataChangedFrame(hwnd());   // spec #8 9.3: the pages and jobs tell the frame what changed
    RefreshWaiting();
    return true;
}

void ManilaGL::OnDpiChanged(UINT)
{
    // UI refresh spec 5.2: the toolbar is made again at the new icon size (a
    // new window, so it is docked again); the status bar gets the new font. The
    // frame then takes its new size, and OnSize sets the status parts.
    Undock(toolbar_);
    if (toolbar_.Rebuild()) Dock(toolbar_, fx::DockSide::Top, toolbar_.Height());
    status_.UpdateFont();
}

bool ManilaGL::OnSize(UINT type, int cx, int cy)
{
    Workspace::OnSize(type, cx, cy);
    if (!status_.hwnd()) return true;
    // TGlWindow::WMSize's layout: company on the left (200 px at 96 DPI),
    // period on the right (100 px), menu help between them.
    const int dpi = static_cast<int>(GetDpiForWindow(hwnd()));
    const int company = MulDiv(200, dpi, 96), period = MulDiv(100, dpi, 96);
    status_.SetParts({company, (std::max)(company, cx - period), -1});
    // The original also reread the company and period here, on every resize;
    // RefreshStatus() runs after the screens that change them instead.
    return true;
}

void ManilaGL::RefreshStatus()
{
    SetStatus(ID_STATUSHELP, GetCompany());       // TGlWindow::GLCreateStatusBar
    SetStatus(ID_PERIODFIELD, GetCurrentPeriod());
}

void ManilaGL::SetStatus(int field, const std::wstring& text)
{
    // The original's three fields of 2057 -> the status bar's parts.
    const int part = field == ID_STATUSHELP ? 0 : field == ID_MENUHELP ? 1 : 2;
    status_.SetText(part, text.c_str());
}

bool ManilaGL::OnCommand(WORD id, WORD code, HWND ctl)
{
    // Toolbar clicks are commands, as TToolBar::Mssg sent them (lParam 0).
    if (ctl && ctl == toolbar_.hwnd()) ctl = nullptr;
    if (Workspace::OnCommand(id, code, ctl)) return true;   // Window menu, CM_EXIT
    // A command the rights deny doesn't run, wherever it came from (a key, the toolbar).
    if (!ctl && id != ID_SYSACCESS && !MayRun(id)) {
        MessageBeep(MB_OK);
        if (IsWritingCommand(id) && fx::model::JobGate::Busy())   // spec #8 9.2: why it's off
            SetStatus(ID_MENUHELP, fx::Utf8ToWide(fx::model::JobGate::Holder()) + L" is writing to the books: wait for it to finish.");
        return true;
    }
    // The master screens that are pages (spec #8 S8-M2); their dialogs are the classic commands below.
    static_assert(ID_ACCTADD == 1004 && ID_DEPTCODES == 10061 && ID_MEDREPS == 6042 && ID_DOCTOR == 6040 &&
                      ID_PRODCODE == 6005 && ID_FAADD == 6000 && ID_FATYPE == 6036 && ID_ACCESS == 4078,
                  "the pages' commands (pages/RecordPages.cpp)");
    if (IsRecordPageCommand(id)) {
        OpenRecordPage(*this, id);
        return true;
    }
    // The voucher screens that are pages (spec #8 S8-M3); their dialogs are the classic commands below.
    if (IsVoucherPageCommand(id)) {
        OpenVoucherPage(*this, id);
        return true;
    }
    // The batch screens that are task pages (spec #8 S8-M4); their dialogs are the classic commands below.
    if (IsTaskPageCommand(id)) {
        OpenTaskPage(*this, id);
        return true;
    }
    switch (id) {
    case ID_SYSACCESS:                     // TManilaGL::GLSysAccess (posted at startup)
        if (RunLogin(hwnd()) != IDOK) {
            PostMessageW(hwnd(), WM_COMMAND, fx::CM_EXIT, 0);
            return true;
        }
        ApplyRights();                     // the user's menu code
        RestoreState();                    // the user's window and pages
        return true;
    case CM_USERS_CLASSIC:                 // Maintenance > User Access (ID_ACCESS): the dialogs (a page since S8-M2)
        RunUserAdministration(hwnd());
        return true;
    case ID_COMPANYPROFILE:                // TGlWindow::GLCompanySet
        RunCompanyProfile(hwnd());
        RefreshStatus();                   // the original updated a status bar it never created
        return true;
    case CM_ACCOUNTS_CLASSIC:              // TGlWindow::GLAcctAdd (ID_ACCTADD): Accounts > Chart of Accounts, GLSUB
        RunChartOfAccounts(hwnd());
        return true;
    case ID_ACCTDEL:                       // TGlWindow::GLAcctDel: GLSUB > Delete Accounts
        RunDeleteAccounts(hwnd());
        return true;
    case CM_DEPARTMENTS_CLASSIC:           // TGlWindow::GLDeptCodes (ID_DEPTCODES): Accounts > Department Codes, GLSUB
        RunDepartmentCodes(hwnd());
        return true;
    case CM_MEDREPS_CLASSIC:               // TManilaGL::GLMedicalReps (ID_MEDREPS)
        RunMedicalReps(hwnd());
        return true;
    case CM_DOCTORS_CLASSIC:               // TManilaGL::GLDoctor (ID_DOCTOR)
        RunDoctorCodes(hwnd());
        return true;
    case CM_PRODUCTS_CLASSIC:              // TManilaGL::GLProductCode (ID_PRODCODE)
        RunProductCodes(hwnd());
        return true;
    case CM_ASSETTYPES_CLASSIC:            // TManilaGL::GLFixedTypes (ID_FATYPE): Fixed Assets > Asset Types (spec #4)
        RunAssetTypes(hwnd());
        return true;
    case CM_ASSETS_CLASSIC:                // TManilaGL::GLFixedAdd (ID_FAADD): Fixed Assets > Add/Modify
        RunFixedAssets(hwnd());
        return true;
    case CM_TK_DEPRECIATION_CLASSIC:       // (ID_FASLIP) TManilaGL::GLFixedSlip: Fixed Assets > Create Voucher
        RunCreateDepreciationVoucher(hwnd());
        return true;
    case ID_DISPOSAL:                      // TManilaGL::GLFixedDisposal: Fixed Assets > Disposal
        RunAssetDisposal(hwnd());
        return true;
    case kFixedAssetListing:               // Fixed Assets > Fixed assets listing (GL_FA.RPT, spec #4 7)
        RunFixedAssetListing(*this);
        return true;
    case kFixedAssetTypes:                 // ... > Fixed asset types (GL_FATYP.RPT)
        RunFixedAssetTypes(*this);
        return true;
    case CM_VENDORS_CLASSIC:               // TManilaGL::APAddVendors (ID_ADDVENDOR, 4045): Accounts > Purchase Vendors (spec #5 S5-M1)
        RunApVendors(hwnd());
        return true;
    case 4059:                             // TManilaGL::ARAddVendors (ID_AR_CUSTOMER): Accounts > Sales Customers: a page (spec #8 S8-M1)
        OpenCustomersPage(*this);
        return true;
    case CM_AR_CUSTOMERS_CLASSIC:          // the dialog (spec #6 S6-M1), until spec #8's acceptance (Q4)
        RunArCustomers(hwnd());
        return true;
    case CM_ARINVOICE_CLASSIC:             // (5045) TManilaGL::ARBatch (ID_ARBATCH): Transactions > Sales Invoice Vouchers (spec #6 S6-M2)
        RunArInvoiceEntry(hwnd());
        return true;
    case CM_APINVOICE_CLASSIC:             // (4044) TManilaGL::APBatch (ID_APBATCH): Transactions > Purchase Invoice Vouchers, toolbar (S5-M2)
        RunApInvoiceEntry(hwnd());
        return true;
    case 8074:                             // TManilaGL::APChequeAP: Confirm Printed Cheques (S5-M6, D9)
        RunApConfirmCheques(hwnd());
        return true;
    case 6085:                             // TManilaGL::APPayment: Bank Reconciliation (S5-M6)
        RunApBankReconciliation(hwnd());
        return true;
    case CM_TK_PURGEAP_CLASSIC:            // (1066) TManilaGL::APPurgeAccts: Maintenance > Purge Paid Payables (S5-M6)
        RunApPurge(hwnd());
        return true;
    case CM_TK_CHEQUES_CLASSIC:            // (1057) TManilaGL::APCreateGLSlip: Receipts/Payments > Create Cheques (S5-M5)
        RunApCreateCheques(hwnd());
        return true;
    case CM_APCASH_CLASSIC:                // (1079) TManilaGL::APCash: Transactions > Cash Invoice Vouchers, toolbar (S5-M4)
        RunApCashEntry(hwnd());
        return true;
    case CM_TK_POSTCASH_CLASSIC:           // (1088) TManilaGL::APPostCash: Post Voucher > Cash Vouchers (S5-M4)
        RunApPost(hwnd(), ApKind::Cash);
        return true;
    case CM_TK_CANCELCASH_CLASSIC:         // (1085) TManilaGL::APCancelCash: Cancel Voucher > Cash Invoice Vouchers (S5-M4)
        RunApCancel(hwnd(), ApKind::Cash);
        return true;
    case CM_TK_POSTAP_CLASSIC:             // (1048) TManilaGL::APPostBatch: Post Voucher > Purchase Vouchers (S5-M3)
        RunApPost(hwnd(), ApKind::Invoice);
        return true;
    case CM_TK_CANCELAP_CLASSIC:           // (3064) TManilaGL::APCancelBatch: Cancel Voucher > Purchase Vouchers (S5-M3)
        RunApCancel(hwnd(), ApKind::Invoice);
        return true;
    case CM_TK_RECOVERAP_CLASSIC:          // (CM_AP_RECOVER) Transactions > Recover Purchase Voucher (spec #5 D7, S5-M3)
        RunApRecover(hwnd(), ApKind::Invoice);
        return true;
    case CM_TK_POSTAR_CLASSIC:             // (4049) TManilaGL::ARPostBatch (ID_ARPOST): Post Voucher > Sales Voucher (spec #6 S6-M3)
        RunArPost(hwnd());
        return true;
    case CM_TK_CANCELAR_CLASSIC:           // (4053) TManilaGL::ARCancelBatch (ID_ARCANCEL): Cancel Voucher > Sales Invoice Voucher (S6-M3)
        RunArCancel(hwnd());
        return true;
    case CM_TK_RECOVERAR_CLASSIC:          // (CM_AR_RECOVER) Transactions > Recover Sales Voucher (spec #6 D7, S6-M3)
        RunArRecover(hwnd());
        return true;
    case CM_RECEIPTS_CLASSIC:              // (4054) TManilaGL::ARCreateSlip (ID_ARPAYMENT): Clear Receipts (spec #6 S6-M4)
        RunArReceipts(hwnd());
        return true;
    case CM_TK_PURGEAR_CLASSIC:            // (4062) TManilaGL::ARPurgeAccts (ID_ARPURGE): Maintenance > Purge Received Receivables (S6-M5)
        RunArPurge(hwnd());
        return true;
    case CM_TK_TRANSFER_CLASSIC:           // (1028) TGlWindow::GLUpdateAP: Maintenance > Update AR and AP (S5-M3)
        RunApTransfer(hwnd());
        return true;
    case CM_GLVOUCHER_CLASSIC:             // TGlWindow::GLModifyBatch / GLBatchEnt (ID_MODBATCH, ID_BATCHENT): the dialog
        RunVoucherEntry(hwnd());
        return true;
    case CM_PRIOR_CLASSIC:                 // TGlWindow::GLPrior (CM_PRIOR): Transactions > Prior Period Entry, the dialog
        RunPriorPeriodEntry(hwnd());
        return true;
    case ID_GLTRANSINQ:                    // TGlWindow::GLTransInquiry: General Ledger Voucher Inquiry
        RunTransactionInquiry(hwnd());
        return true;
    case ID_ACCTBUDGET:                    // TGlWindow::GLAcctBudget: Budget Data
        RunBudgetData(hwnd());
        return true;
    case CM_TK_YEAREND_CLASSIC:            // (ID_YEAREND) TGlWindow::GLYearEnd: Year End Consolidation
        RunYearEnd(hwnd());
        return true;
    case CM_TK_BACKUP_CLASSIC:             // (ID_BACKUP) TGlWindow::GLBackup: Backup Databases
        RunBackup(hwnd());
        return true;
    case ID_RESTORE:                       // TGlWindow::GLRestore: Restore Databases
        if (RunRestore(hwnd())) {
            RefreshStatus();                   // the restored company's name and period
            rights_.reset();                   // and its accessm
            ApplyRights();
        }
        return true;
    case ID_REINDEX:                       // TGlWindow::GLReindex: Reindex Databases
        RunReindex(hwnd());
        return true;
    case CM_TK_POSTGL_CLASSIC:             // (ID_GLPOSTBATCH) TGlWindow::GLPostBatch: Post Voucher > General Ledger, GLSUB, toolbar
        RunPostVouchers(hwnd());
        return true;
    case CM_TK_RECOVERGL_CLASSIC:          // (ID_GLREPBATCH) TGlWindow::GLRepBatch: Recover General Ledger Voucher
        RunRecoverVoucher(hwnd());
        return true;
    case CM_TK_CANCELGL_CLASSIC:           // (ID_BATCHDELETE) TGlWindow::GLDeleteBatch: Cancel Voucher > General Ledger
        RunCancelVoucher(hwnd());
        return true;
    case ID_ABOUT: {                       // TGlWindow::CMAbout
        fx::Dialog about;
        about.DoModal(hInst, 1008, hwnd());
        return true;
    }
    case ID_GL:                            // the toolbar's GL button (not in gl_new.rc's 1032)
        return true;
    case CM_DIALOGPRINT:                   // TGlWindow::CMDialogPrint: File > Printer Setup (spec #3 D10)
        rpt::win32::ShowPrinterSetup(hwnd());
        return true;
    case ID_PRINT:                         // File > Print, the toolbar Print button (spec #3 D9)
        if (rpt::win32::PrintActivePreview(*this)) return true;
        RunReportList();                   // no preview active: the report list (1022)
        return true;
    case CM_REPORT_LIST:                   // Reporting > Custom Report, GLSUB (D9; 3068 in the original)
        RunReportList();
        return true;
    case CM_STATEMENT_LAYOUT:              // Accounts > Statement Layout (spec #3 6.4)
        RunStatementLayout(hwnd());
        return true;
    case CM_SAMPLE_REPORT:                 // the report engine's test report (gl.exe /samplereport)
        RunSampleReport(*this);
        return true;
    case ID_TB:                            // Reporting > General Ledger > Trial Balance (spec #3 F1, D14)
        RunTrialBalance(*this);
        return true;
    case ID_DISTLIST:                      // Reporting > General Ledger > Distribution List (F2, D14)
        RunDistributionTrialBalance(*this);
        return true;
    case ID_FININCOME:                     // Reporting > Financial Statements > Income statement (F3, D13)
        RunIncomeStatement(*this);
        return true;
    case ID_FINBS:                         // ... > Balance sheet (F4)
        RunBalanceSheet(*this);
        return true;
    case ID_FINRE:                         // ... > Cash flow statement (F5; was retained earnings, D13)
        RunCashFlowStatement(*this);
        return true;
    case ID_TBBYDEPT:                      // Reporting > General Ledger > Trial Balance by Dept. (GLDEPT.RPT)
        RunTrialBalanceByDept(*this);
        return true;
    case ID_GLCHARTINQ:                    // Accounts > Chart of Accounts Report, the toolbar's inquiry button
        RunChartOfAccountsReport(*this);
        return true;
    case ID_GLBUDGETINQ:                   // Accounts > Budget Report (GL_BUDGT.RPT, D6)
        RunBudgetReport(*this);
        return true;
    case ID_SLIP:                          // Reporting > General Ledger > Voucher Slips (GLSLIP.RPT)
        RunVoucherSlips(*this);
        return true;
    case CM_SLIPDETAIL:                    // ... > Voucher Detail (GLBTCHD.RPT, D8)
        RunVoucherDetail(*this);
        return true;
    case ID_SLIPSUM:                       // ... > Voucher Summary (GL_BTCHS.RPT)
        RunVoucherSummary(*this);
        return true;
    case ID_PREPOST:                       // ... > Preposting Distribution List (GLPRE.RPT, D4)
        RunPrepostingDistribution(*this);
        return true;
    case kPrePostingReport:                // Transactions > Voucher Reports > GL Pre-Posting Report (D5)
        RunPrePostingReport(*this);
        return true;
    case ID_REPLIST:                       // Reporting > General Ledger > Medical Representatives (GL_MEDRP.RPT)
        RunMedicalRepsListing(*this);
        return true;
    case ID_PRODUCTLIST:                   // ... > Product code summary (GL_PRODL.RPT)
        RunProductCodesListing(*this);
        return true;
    case ID_DOCTOR_RPT:                    // ... > Doctor Codes (GL_DRCDS.RPT, D12)
        RunDoctorCodesListing(*this);
        return true;
    default:
        break;
    }
    if (!ctl && RunApReport(*this, id)) return true;   // the AP reports, 4091 Print Cheques among them (spec #5 7)
    if (!ctl && RunArReport(*this, id)) return true;   // the AR reports (spec #6 7)
    if (ctl || id >= 0xFF00) return false;   // child-window IDs: DefFrameProc
    NotPorted(id);
    return true;
}

void ManilaGL::RunReportList()
{
    // The chosen report runs as its menu item does, once the list has closed;
    // a user report (spec #7) through the report writer.
    const ReportChoice c = ShowReportList(hwnd());
    if (c.definition) RunUserReport(*this, c.definition);
    else if (c.command) OnCommand(c.command, 0, nullptr);
}

std::wstring ManilaGL::MenuText(WORD id) const
{
    wchar_t buf[128] = {};
    HMENU menu = GetMenu(hwnd());
    if (!menu || GetMenuStringW(menu, id, buf, 128, MF_BYCOMMAND) <= 0) return {};
    std::wstring text;
    for (const wchar_t* p = buf; *p; ++p)
        if (*p != L'&') text += *p;
    while (!text.empty() && (text.back() == L'.' || text.back() == L' ')) text.pop_back();
    return text;
}

void ManilaGL::NotPorted(WORD id)
{
    const std::wstring item = MenuText(id);
    if (contains(kArReports, id)) {
        ReportingStubs::Show(hwnd(), item.c_str(), L"Accounts Receivable", 6);
    } else if (contains(kApCommands, id)) {
        ApStubs::Show(hwnd(), item.c_str());
    } else if (contains(kArCommands, id)) {
        ArStubs::Show(hwnd(), item.c_str());
    } else {
        std::wstring text = item.empty() ? fx::Format(L"Command %u", id) : item;
        text += L"\n\nThis General Ledger function is not part of Spec #1.";
        MessageBoxW(hwnd(), text.c_str(), szApp, MB_OK | MB_ICONINFORMATION);
    }
}

bool ManilaGL::OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result)
{
    if (msg == WM_CLOSE) SaveState();   // while the pinned pages are still open
    if (msg == kRestoreState) {
        RestoreState();
        result = 0;
        return true;
    }
    if (msg == kDataChanged) {   // spec #8 9.3: a save or a job changed the books, or a job started
        RefreshWaiting();
        ApplyRights();             // writing commands off while a job holds the books
        result = 0;
        return true;
    }
    if (Workspace::OnMessage(msg, wp, lp, result)) return true;   // tabs, navigation, the Window menu's pages
    switch (msg) {
    case WM_INITMENUPOPUP:   // TManilaGL::WMInitMenuPopup: the rights, as ApplyMenuAccess and its follow-ons did
        ApplyRights();
        return false;
    case WM_MENUSELECT:
        MenuHelp(wp, lp);
        return false;
    default:
        return false;
    }
}

void ManilaGL::MenuHelp(WPARAM wp, LPARAM lp)
{
    // TGlWindow::WMMenuSelect. Win16 packed the flags and menu into lParam;
    // Win32 sends the item and flags in wParam and the menu in lParam.
    const UINT item = LOWORD(wp);
    const UINT flags = HIWORD(wp);
    UINT str = item;
    if (flags == 0xFFFF && lp == 0)
        str = IDS_READY;        // "Ready..."
    else if (flags & MF_POPUP)
        str = IDS_MENUITEM;     // "Select a menu item from the menu list..."
    else if (flags & MF_SYSMENU)
        str = IDS_SYSTEM;       // "System menu..."
    SetStatus(ID_MENUHELP, fx::LoadResString(hInst, str));
}

void ManilaGL::PopUpMenu(POINT screen)
{
    // TGlWindow::PopUpMenu; HGLSubMenu is loaded once in wWinMain.
    if (HMENU popup = GetSubMenu(HGLSubMenu, 0))
        TrackPopupMenu(popup, TPM_LEFTBUTTON, screen.x, screen.y, 0, hwnd(), nullptr);
}

void ManilaGL::OnPageAreaContextMenu(POINT screen)
{
    PopUpMenu(screen);   // TGLClient::WMRButtonDown: GLSUB
}

// ---- the command table: navigation and rights (spec #8 8) ------------------------------

void ManilaGL::BuildNavigation()
{
    ClearNavigation();
    std::vector<HTREEITEM> open;   // open[d]: the group rows at depth d go into
    bool skipping = false;         // inside the Window menu
    HTREEITEM accounts = nullptr;
    std::map<WORD, HTREEITEM> parentOf;   // a command's group: its classic dialog goes beside it
    for (const cmd::MenuRow& r : cmd::MenuRows()) {
        if (r.depth == 0) skipping = r.kind == cmd::Kind::Popup && cmd::PlainText(r.text) == L"Window";
        if (skipping || r.kind == cmd::Kind::Separator) continue;
        open.resize(static_cast<size_t>(r.depth));
        HTREEITEM parent = open.empty() ? TVI_ROOT : open.back();
        if (r.kind == cmd::Kind::Popup) {
            open.push_back(AddNavItem(parent, cmd::PlainText(r.text), 0));
            if (r.depth == 0 && cmd::PlainText(r.text) == L"Accounts") accounts = open.back();
        } else {
            AddNavItem(parent, cmd::PlainText(r.text), r.id);
            if (!parentOf.count(static_cast<WORD>(r.id))) parentOf[static_cast<WORD>(r.id)] = parent;
        }
    }
    std::vector<ClassicCommand> classics = ClassicCommands();
    const std::vector<ClassicCommand>& vouchers = VoucherClassicCommands();
    classics.insert(classics.end(), vouchers.begin(), vouchers.end());
    const std::vector<ClassicCommand>& tasks = TaskClassicCommands();
    classics.insert(classics.end(), tasks.begin(), tasks.end());
    for (const ClassicCommand& c : classics) {
        const auto p = parentOf.find(c.page);
        if (p != parentOf.end()) AddNavItem(p->second, c.text, c.classic);
    }
    // The classic dialog of a screen that became a page (spec #8 Q4): not on the menu.
    if (accounts) AddNavItem(accounts, L"Sales Customers (classic)", CM_AR_CUSTOMERS_CLASSIC);
}

void ManilaGL::RestoreState()
{
    if (restored_) return;
    restored_ = true;
    const std::string user = UiStateUser(szUser);
    try {
        if (const auto w = GetUiState(cb, user, "window")) {
            WINDOWPLACEMENT wp{sizeof wp};
            int show = 0, l = 0, t = 0, r = 0, b = 0;
            if (sscanf_s(w->c_str(), "%d %d %d %d %d", &show, &l, &t, &r, &b) == 5 && r > l && b > t) {
                wp.showCmd = show == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
                wp.rcNormalPosition = {l, t, r, b};
                SetWindowPlacement(hwnd(), &wp);
            }
        }
        if (const auto nav = GetUiState(cb, user, "navigation")) SetNavigationVisible(*nav != "0");
        WORD active = 0;
        if (const auto a = GetUiState(cb, user, "active")) active = static_cast<WORD>(std::atoi(a->c_str()));
        if (const auto pins = GetUiState(cb, user, "pins")) {
            HWND activePage = nullptr;
            for (const char* p = pins->c_str(); *p;) {
                const WORD id = static_cast<WORD>(std::strtol(p, const_cast<char**>(&p), 10));
                if (*p == ',') ++p;
                if (!id) break;
                if (!MayRun(id)) continue;
                OnCommand(id, 0, nullptr);
                if (auto* page = dynamic_cast<fx::HostedPage*>(fx::Window::FromHandle(activePage = this->activePage()));
                    page && page->commandId == id)
                    page->pinned = true;
            }
            for (HWND h : pages())
                if (auto* page = dynamic_cast<fx::HostedPage*>(fx::Window::FromHandle(h)); page && page->commandId == active)
                    ActivatePage(h);
            (void)activePage;
        }
    } catch (const std::exception&) {
        // The state is a convenience: a database without GL_UISTATE, or a
        // read-only one, opens as it always did.
    }
}

void ManilaGL::SaveState()
{
    if (!restored_ || !hwnd()) return;   // before the login: nothing of the user's yet
    const std::string user = UiStateUser(szUser);
    try {
        WINDOWPLACEMENT wp{sizeof wp};
        if (GetWindowPlacement(hwnd(), &wp)) {
            const RECT& n = wp.rcNormalPosition;
            char buf[96];
            std::snprintf(buf, sizeof buf, "%u %ld %ld %ld %ld", wp.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL,
                          n.left, n.top, n.right, n.bottom);
            SetUiState(cb, user, "window", buf);
        }
        SetUiState(cb, user, "navigation", navigationVisible() ? "1" : "0");
        std::string pins;
        for (WORD id : PinnedCommands()) pins += (pins.empty() ? "" : ",") + std::to_string(id);
        SetUiState(cb, user, "pins", pins);
        const auto* page = dynamic_cast<fx::HostedPage*>(fx::Window::FromHandle(activePage()));
        SetUiState(cb, user, "active", page && page->commandId ? std::to_string(page->commandId) : std::string());
    } catch (const std::exception&) {
        // A read-only company: the state isn't kept, the program goes on.
    }
}

bool ManilaGL::MayRun(WORD id)
{
    if (!rights_) rights_ = cmd::Rights::Load(cb);
    if (IsWritingCommand(id) && fx::model::JobGate::Busy()) return false;   // spec #8 9.2: one writing job
    return rights_->MayRun(id, szMsg);
}

void ManilaGL::RefreshWaiting()
{
    // Spec #8 9.3: what is waiting, in the status bar's middle part; the job writing, if any.
    auto count = [](const char* sql) {
        try {
            cb4sqlite::Query q(cb, sql);
            return q.next() ? std::atoi(q.text(0).c_str()) : 0;
        } catch (const std::exception&) {
            return 0;
        }
    };
    const int gl = count("SELECT count(*) FROM gl_btch WHERE _deleted = 0 AND trim(mode) IN ('1', '3')");
    const int ap = count("SELECT count(*) FROM ap_btch WHERE _deleted = 0 AND trim(mode) = '1'") +
                   count("SELECT count(*) FROM ap_cash WHERE _deleted = 0 AND trim(mode) = '1'");
    const int ar = count("SELECT count(*) FROM ar_btch WHERE _deleted = 0 AND trim(mode) = '1'");
    const int journal = count("SELECT count(*) FROM ap_jsum WHERE _deleted = 0 AND trim(coalesce(mode, '')) IN ('', '1')") +
                        count("SELECT count(*) FROM ar_jsum WHERE _deleted = 0 AND trim(coalesce(mode, '')) IN ('', '1')");
    std::wstring text = L"To post: " + std::to_wstring(gl) + L" GL, " + std::to_wstring(ap) + L" AP, " + std::to_wstring(ar) +
                        L" AR vouchers; " + std::to_wstring(journal) + L" journal rows to transfer.";
    const std::string holder = fx::model::JobGate::Holder();
    if (!holder.empty()) text = fx::Utf8ToWide(holder) + L" is writing to the books.  " + text;
    SetStatus(ID_MENUHELP, text);
}

bool ManilaGL::IsCommandEnabled(WORD id) const
{
    return const_cast<ManilaGL*>(this)->MayRun(id);
}

void ManilaGL::ApplyRights()
{
    if (HMENU menu = GetMenu(hwnd()))
        for (cmd::Id id : cmd::CommandIds()) EnableMenuItem(menu, id, MF_BYCOMMAND | (MayRun(id) ? MF_ENABLED : MF_GRAYED));
    for (const ToolButton& b : kToolButtons) toolbar_.EnableCommand(toolbarCommand(b.id), MayRun(toolbarCommand(b.id)));
    RefreshNavigation();
}

bool ManilaGL::OnDestroy()
{
    // Detach the menu so the frame doesn't destroy it: wWinMain owns
    // MANILA, GLE and GLSUB and destroys all three (~TAcctModule).
    SetMenu(hwnd(), nullptr);
    return Workspace::OnDestroy();
}
