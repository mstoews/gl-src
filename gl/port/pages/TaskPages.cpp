//
// TaskPages.cpp - see TaskPages.h.
//
// Translation notes (decisions are spec #8 13):
//   - 5.3, 9.2: the batch dialogs' lists and their one button become a task
//     page; the work runs on a worker thread (fx::Job) with its own
//     connection, so the window stays responsive and other pages keep reading
//     (WAL, D8). One writing job runs at a time (model::JobGate).
//   - Post, Cancel and Recover act on the checked rows one voucher at a time
//     (each its own transaction, as the dialogs' per-voucher calls are), so
//     Stop ends at a voucher boundary. The other tasks are one transaction
//     each: Stop works before they start.
//   - Refusals go on the rows; the result names what was made.
//   - Clear Receipts is M3's receipts page.
//   - Spec #9 S9-M3 (D5, D6): Confirm Printed Cheques (8074) is the Cheques
//     page and Bank Reconciliation (6085) a task page. Their one-row commands
//     (Renumber, Void) run at once on the page's connection, after the job
//     gate, and the list is loaded again. Print cheques sends 4091 to the
//     frame (the preview, Q3). Their classic dialogs are "(classic)" entries.
//     (S8-M4's note said Confirm Printed Cheques prints; it doesn't.)
//   - Spec #9 S9-M4 (D7): Restore Databases (1074) is a task page. Its list
//     is the backups beside the company file and any Browse adds; Restore acts
//     on the selected one, on the page (it closes the company connection, so
//     not as a job): asked first, then every other page is closed (each asking
//     about its unsaved changes, as closing the program does; Home stays, it
//     holds no tables), then RestoreDatabase. Every data group is broadcast
//     after, so the frame shows the company and period again and reads the
//     rights again, as ID_RESTORE did.
//
#include "TaskPages.h"

#include "../DataChanged.h"
#include "../Globals.h"
#include "../Ini.h"
#include "../PortIds.h"
#include "../Vouchers.h"   // OpenPeriods
#include "TaskModels.h"
#include "model/JobState.h"   // JobGate

#include <CommCtrl.h>
#include <commdlg.h>   // Restore's Browse... (S9-M4)
#include <Uxtheme.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace {

namespace m = fx::model;

std::wstring wide(const std::string& s) { return fx::Utf8ToWide(s); }

std::string today()
{
    SYSTEMTIME t{};
    GetLocalTime(&t);
    char buf[16];
    snprintf(buf, sizeof buf, "%04u%02u%02u", t.wYear, t.wMonth, t.wDay);
    return buf;
}

long iniBatchno()
{
    return std::atol(fx::WideToUtf8(GetIniString(L"GL.INI", L"PERIOD", L"BATCHNO")).c_str());
}

const std::vector<std::pair<int, unsigned>> kAnchors = {
    {IDC_TK_ACTION, m::kTopLeft},  {IDC_TK_STOP, m::kTopLeft},       {IDC_TK_ALL, m::kTopLeft},
    {IDC_TK_NONE, m::kTopLeft},    {IDC_TK_REFRESH, m::kTopLeft},    {IDC_TK_LOPTION, m::kTopLeft},
    {IDC_TK_DATE, m::kTopLeft},    {IDC_TK_PERIOD, m::kTopLeft},     {IDC_TK_SUMMARY, m::kTopWide},
    {IDC_TK_LPAYDATE, m::kTopLeft}, {IDC_TK_PAYDATE, m::kTopLeft},   {IDC_TK_LBANK, m::kTopLeft},
    {IDC_TK_BANK, m::kTopLeft},    {IDC_TK_LNUMBER, m::kTopLeft},    {IDC_TK_NUMBER, m::kTopLeft},
    {IDC_TK_LIST, m::kAll},        {IDC_TK_PROGRESS, m::kBottomWide}, {IDC_TK_RESULT, m::kBottomWide},
    {IDC_TK_LROWIN, m::kTopLeft},  {IDC_TK_ROWIN, m::kTopLeft},      {IDC_TK_PAGECMD, m::kTopLeft},
    {IDC_TK_ROW, m::kTopLeft},
};

struct TaskCommand {
    WORD command, classic;
    const wchar_t* classicText;
    std::function<std::unique_ptr<TaskModel>()> make;
};

const std::vector<TaskCommand>& commands()
{
    static const std::vector<TaskCommand> all = {
        {10121, CM_TK_POSTGL_CLASSIC, L"Post General Ledger (classic)", [] { return MakePostTask(VoucherKind::Gl); }},
        {1048, CM_TK_POSTAP_CLASSIC, L"Post Purchase Vouchers (classic)", [] { return MakePostTask(VoucherKind::ApInvoice); }},
        {1088, CM_TK_POSTCASH_CLASSIC, L"Post Cash Vouchers (classic)", [] { return MakePostTask(VoucherKind::ApCash); }},
        {4049, CM_TK_POSTAR_CLASSIC, L"Post Sales Voucher (classic)", [] { return MakePostTask(VoucherKind::Ar); }},
        {3054, CM_TK_CANCELGL_CLASSIC, L"Cancel General Ledger (classic)", [] { return MakeCancelTask(VoucherKind::Gl); }},
        {3064, CM_TK_CANCELAP_CLASSIC, L"Cancel Purchase Vouchers (classic)", [] { return MakeCancelTask(VoucherKind::ApInvoice); }},
        {1085, CM_TK_CANCELCASH_CLASSIC, L"Cancel Cash Invoice Vouchers (classic)", [] { return MakeCancelTask(VoucherKind::ApCash); }},
        {4053, CM_TK_CANCELAR_CLASSIC, L"Cancel Sales Invoice Voucher (classic)", [] { return MakeCancelTask(VoucherKind::Ar); }},
        {10122, CM_TK_RECOVERGL_CLASSIC, L"Recover General Ledger Voucher (classic)", [] { return MakeRecoverTask(VoucherKind::Gl); }},
        {CM_AP_RECOVER, CM_TK_RECOVERAP_CLASSIC, L"Recover Purchase Voucher (classic)", [] { return MakeRecoverTask(VoucherKind::ApInvoice); }},
        {CM_AR_RECOVER, CM_TK_RECOVERAR_CLASSIC, L"Recover Sales Voucher (classic)", [] { return MakeRecoverTask(VoucherKind::Ar); }},
        {1028, CM_TK_TRANSFER_CLASSIC, L"Update AR and AP (classic)", [] { return MakeTransferTask(iniBatchno()); }},
        {6003, CM_TK_DEPRECIATION_CLASSIC, L"Create Voucher (classic)", [] { return MakeDepreciationTask(iniBatchno()); }},
        {1068, CM_TK_YEAREND_CLASSIC, L"Year End Consolidation (classic)", [] { return MakeYearEndTask(); }},
        {1066, CM_TK_PURGEAP_CLASSIC, L"Purge Paid Payables (classic)", [] { return MakePurgePayablesTask(); }},
        {4062, CM_TK_PURGEAR_CLASSIC, L"Purge Received Receivables (classic)", [] { return MakePurgeReceivablesTask(); }},
        {1057, CM_TK_CHEQUES_CLASSIC, L"Create Cheques (classic)", [] { return MakeCreateChequesTask(today()); }},
        {1072, CM_TK_BACKUP_CLASSIC, L"Backup Databases (classic)",
         [] { return MakeBackupTask(fx::WideToUtf8(CurDir), today()); }},
        {8074, CM_TK_CONFIRM_CLASSIC, L"Confirm Printed Cheques (classic)", [] { return MakeChequesTask(); }},   // spec #9 S9-M3
        {6085, CM_TK_BANKREC_CLASSIC, L"Bank Reconciliation (classic)", [] { return MakeBankReconciliationTask(); }},
        {1074, CM_TK_RESTORE_CLASSIC, L"Restore Databases (classic)",   // spec #9 S9-M4
         [] { return MakeRestoreTask(fx::WideToUtf8(CurDir)); }},
    };
    return all;
}

const TaskCommand* commandOf(WORD id)
{
    for (const TaskCommand& c : commands())
        if (c.command == id) return &c;
    return nullptr;
}

class TaskPage : public fx::Page {
public:
    TaskPage(WORD command, std::unique_ptr<TaskModel> model) : fx::Page(IDD_PG_TASK), model_(std::move(model))
    {
        commandId = command;
    }

    bool CanClose() override
    {
        if (!job_.active()) return true;
        enum { kStop = 101 };
        if (fx::Ask(host() ? host()->hostWindow() : hwnd(), szApp, wide(model_->Title()) + L" is still running.",
                    L"Stop it at the next voucher and close the page?", {{kStop, L"Stop and close"}, {IDCANCEL, L"Keep it running"}},
                    IDCANCEL, IDCANCEL) != kStop)
            return false;
        job_.Cancel();
        job_.Join();   // it stops at a boundary
        return true;
    }

    // /stress: a dry job (nothing checked) started, stopped and joined.
    bool DryRun()
    {
        for (TaskRow& r : rows_) r.checked = false;
        if (!Start(false)) return false;
        job_.Cancel();
        job_.Join();
        Finished();
        return true;
    }

protected:
    bool OnInitPage() override
    {
        // A task with no check boxes hides Check all / Check none: Refresh takes Check all's
        // place, so no gap reads as missing buttons (design review). Before the anchors are
        // recorded, so the layout keeps it there.
        if (!model_->Checks()) {
            RECT r{};
            GetWindowRect(Item(IDC_TK_ALL), &r);
            MapWindowPoints(nullptr, form(), reinterpret_cast<POINT*>(&r), 2);
            SetWindowPos(Item(IDC_TK_REFRESH), nullptr, r.left, r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        // A row command with no page command beside it (Bank Reconciliation's Void) takes that
        // command's place, so no gap reads as a missing button (spec #9 S9-M3).
        if (!model_->RowAction().empty() && model_->PageCommand() == 0) {
            RECT r{};
            GetWindowRect(Item(IDC_TK_PAGECMD), &r);
            MapWindowPoints(nullptr, form(), reinterpret_cast<POINT*>(&r), 2);
            SetWindowPos(Item(IDC_TK_ROW), nullptr, r.left, r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        SetAnchors(kAnchors);
        list_ = Item(IDC_TK_LIST);
        fx::SetAccessibleName(list_, wide(model_->Title()) + L": what it acts on");
        SetWindowTheme(list_, L"Explorer", nullptr);
        // LVS_EX_LABELTIP: a cut-off cell (a long path, a long note) shows whole in a tooltip.
        ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP |
                                                     (model_->Checks() ? LVS_EX_CHECKBOXES : 0));
        const int dpi = static_cast<int>(GetDpiForWindow(list_));
        int i = 0;
        for (const auto& [heading, width] : model_->Columns()) {
            std::wstring h = wide(heading);
            LVCOLUMNW c{};
            c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            c.fmt = heading == "Amount" || heading == "Owed" || heading == "Balance" || heading == "This month" ? LVCFMT_RIGHT : LVCFMT_LEFT;
            c.cx = MulDiv(width, dpi, 96);
            c.pszText = h.data();
            ListView_InsertColumn(list_, i++, &c);
        }
        {
            LVCOLUMNW c{};
            c.mask = LVCF_TEXT | LVCF_WIDTH;
            c.cx = MulDiv(300, dpi, 96);
            c.pszText = const_cast<wchar_t*>(L"Note");
            ListView_InsertColumn(list_, i, &c);
        }
        SetDlgItemTextW(form(), IDC_TK_ACTION, (L"&" + wide(model_->Action())).c_str());   // an access key (spec #8 S8-M6)
        for (int id : {IDC_TK_ALL, IDC_TK_NONE}) ShowWindow(Item(id), model_->Checks() ? SW_SHOW : SW_HIDE);
        const TaskOption opt = model_->Option();
        SetText(IDC_TK_LOPTION, model_->OptionLabel());
        ShowWindow(Item(IDC_TK_DATE), opt == TaskOption::Date ? SW_SHOW : SW_HIDE);
        ShowWindow(Item(IDC_TK_PERIOD), opt == TaskOption::Period ? SW_SHOW : SW_HIDE);
        if (opt == TaskOption::Date) fx::SetDateField(Item(IDC_TK_DATE), today());
        if (opt == TaskOption::Period) {
            HWND combo = Item(IDC_TK_PERIOD);
            const std::vector<OpenPeriod> periods = OpenPeriods(cb);
            for (const OpenPeriod& p : periods) {
                const std::wstring t = std::to_wstring(p.number) + L"  " + wide(p.description);
                const auto at = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str())));
                SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(at), p.number);
            }
            SendMessageW(combo, CB_SETCURSEL, periods.empty() ? static_cast<WPARAM>(-1) : periods.size() - 1, 0);
        }
        ShowPaymentOptions();
        ShowRowCommands();
        SendDlgItemMessageW(form(), IDC_TK_PROGRESS, PBM_SETRANGE32, 0, 1);
        EnableWindow(Item(IDC_TK_STOP), FALSE);
        Load();
        return true;
    }

    bool OnFormCommand(WORD id, WORD code, HWND) override
    {
        if (code == BN_CLICKED) switch (id) {
            case IDC_TK_ACTION:
                if (model_->ActionOnRow()) RunRowCommand();   // Restore: on the selected row, on the page
                else Start(true);
                return true;
            case IDC_TK_STOP:
                job_.Cancel();
                SetText(IDC_TK_RESULT, model_->PerItem() ? "Stopping after this voucher..." : "Stopping...");
                return true;
            case IDC_TK_ALL:
            case IDC_TK_NONE: CheckAll(id == IDC_TK_ALL); return true;
            case IDC_TK_REFRESH: Load(); return true;
            case IDC_TK_ROW: RunRowCommand(); return true;
            case IDC_TK_PAGECMD:   // Print cheques: the frame runs 4091, the preview; Restore's Browse...
                if (!model_->BrowseLabel().empty()) {
                    Browse();
                    return true;
                }
                if (host() && model_->PageCommand())
                    PostMessageW(host()->hostWindow(), WM_COMMAND, model_->PageCommand(), 0);
                return true;
            default: break;
            }
        if ((code == CBN_SELCHANGE && (id == IDC_TK_PERIOD || id == IDC_TK_BANK)) || code == EN_CHANGE) {
            if (id == IDC_TK_PERIOD) Load();
            return true;   // a task's options aren't a record: never dirty
        }
        return false;
    }

    bool OnFormNotify(NMHDR* nm, LRESULT& result) override
    {
        if (nm->idFrom == IDC_TK_DATE && nm->code == DTN_DATETIMECHANGE) {
            if (!job_.active() && model_->OptionFiltersList()) Load();
            return true;
        }
        if (nm->idFrom == IDC_TK_PAYDATE) return true;   // an option, not a record
        if (nm->hwndFrom != list_ || nm->code != LVN_ITEMCHANGED || filling_) return false;
        const auto* lv = reinterpret_cast<NMLISTVIEW*>(nm);
        if ((lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_STATEIMAGEMASK) && lv->iItem >= 0 &&
            static_cast<size_t>(lv->iItem) < rows_.size()) {
            TaskRow& r = rows_[static_cast<size_t>(lv->iItem)];
            const bool on = ((lv->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2;
            if (job_.active() || !r.selectable) {   // not now / not this one: back as it was
                if (on != r.checked) {
                    filling_ = true;
                    ListView_SetCheckState(list_, lv->iItem, r.checked);
                    filling_ = false;
                }
            } else {
                r.checked = on;
                ShowSummary();
            }
        }
        result = 0;
        return true;
    }

    bool OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override
    {
        switch (msg) {
        case fx::Job::kProgress: {
            const int done = static_cast<int>(wp), of = static_cast<int>(lp);
            SendDlgItemMessageW(form(), IDC_TK_PROGRESS, PBM_SETRANGE32, 0, of > 0 ? of : 1);
            SendDlgItemMessageW(form(), IDC_TK_PROGRESS, PBM_SETPOS, static_cast<WPARAM>(done), 0);
            if (!job_.state().cancelRequested())
                SetText(IDC_TK_RESULT, model_->PerItem() ? "Working: " + std::to_string(done) + " of " + std::to_string(of) + "..." : "Working...");
            result = 0;
            return true;
        }
        case fx::Job::kFinished:
            Finished();
            result = 0;
            return true;
        case kDataChanged:
            // Another page or job changed this task's tables: list them again (not for other tables,
            // so a finished run's notes and result stay until they matter).
            if ((wp & model_->Groups()) && !job_.active() && !notifying_ && reinterpret_cast<HWND>(lp) != hwnd()) Load();
            result = 0;
            return true;
        default: return fx::Page::OnMessage(msg, wp, lp, result);
        }
    }

private:
    // Create Cheques' row (G8), filled as the classic screen fills it; other
    // tasks hide it and give its room to the list.
    void ShowPaymentOptions()
    {
        static constexpr int kRow[] = {IDC_TK_LPAYDATE, IDC_TK_PAYDATE, IDC_TK_LBANK, IDC_TK_BANK, IDC_TK_LNUMBER, IDC_TK_NUMBER};
        if (!model_->HasPaymentOptions()) {
            for (int id : kRow) ShowWindow(Item(id), SW_HIDE);
            RECT step{0, 0, 0, 14};   // the row's height in dialog units
            MapDialogRect(form(), &step);
            for (int id : {IDC_TK_SUMMARY, IDC_TK_LIST}) {
                RECT r{};
                GetWindowRect(Item(id), &r);
                MapWindowPoints(nullptr, form(), reinterpret_cast<POINT*>(&r), 2);
                const int grow = id == IDC_TK_LIST ? step.bottom : 0;
                SetWindowPos(Item(id), nullptr, r.left, r.top - step.bottom, r.right - r.left, r.bottom - r.top + grow,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return;
        }
        PaymentOptions o;
        try {
            o = model_->DefaultPaymentOptions(cb);
        } catch (const std::exception& e) {
            ShowInfo(fx::InfoKind::Error, wide(e.what()));
        }
        fx::SetDateField(Item(IDC_TK_PAYDATE), o.payDate.empty() ? today() : o.payDate);
        HWND combo = Item(IDC_TK_BANK);
        banks_.clear();
        for (const auto& [key, text] : o.banks) {
            const std::wstring t = wide(text);
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str()));
            banks_.push_back(key);
        }
        const auto at = std::find(banks_.begin(), banks_.end(), o.bank);
        SendMessageW(combo, CB_SETCURSEL, at == banks_.end() ? static_cast<WPARAM>(-1) : static_cast<WPARAM>(at - banks_.begin()), 0);
        SetText(IDC_TK_NUMBER, o.firstNumber > 0 ? std::to_string(o.firstNumber) : std::string());
    }

    void ReadPaymentOptions()
    {
        if (!model_->HasPaymentOptions()) return;
        model_->payment.payDate = fx::GetDateField(Item(IDC_TK_PAYDATE));
        const LRESULT sel = SendDlgItemMessageW(form(), IDC_TK_BANK, CB_GETCURSEL, 0, 0);
        model_->payment.bank = sel >= 0 && static_cast<size_t>(sel) < banks_.size() ? banks_[static_cast<size_t>(sel)] : std::string();
        model_->payment.firstNumber = std::atol(Text(IDC_TK_NUMBER).c_str());
    }

    // The cheque pages' commands (spec #9 S9-M3); other tasks hide them.
    void ShowRowCommands()
    {
        const bool row = !model_->RowAction().empty() && !model_->ActionOnRow(), input = !model_->RowInputLabel().empty();
        const bool page = model_->PageCommand() != 0 || !model_->BrowseLabel().empty();
        if (row) SetDlgItemTextW(form(), IDC_TK_ROW, wide(model_->RowAction()).c_str());
        if (input) SetDlgItemTextW(form(), IDC_TK_LROWIN, wide(model_->RowInputLabel()).c_str());
        if (page)
            SetDlgItemTextW(form(), IDC_TK_PAGECMD,
                            wide(model_->BrowseLabel().empty() ? model_->PageCommandLabel() : model_->BrowseLabel()).c_str());
        ShowWindow(Item(IDC_TK_ROW), row ? SW_SHOW : SW_HIDE);
        ShowWindow(Item(IDC_TK_LROWIN), input ? SW_SHOW : SW_HIDE);
        ShowWindow(Item(IDC_TK_ROWIN), input ? SW_SHOW : SW_HIDE);
        ShowWindow(Item(IDC_TK_PAGECMD), page ? SW_SHOW : SW_HIDE);
        FillRowInput();
    }

    void FillRowInput()
    {
        if (model_->RowInputLabel().empty()) return;
        long n = 0;
        try {
            n = model_->RowInputDefault(cb);
        } catch (const std::exception&) {
        }
        SetText(IDC_TK_ROWIN, n > 0 ? std::to_string(n) : std::string());
    }

    // Renumber or Void the selected row: at once, one cheque, one transaction.
    void RunRowCommand()
    {
        if (job_.active()) return;
        if (m::JobGate::Busy()) {   // writes are serialised (spec #8 9.1)
            ShowInfo(fx::InfoKind::Warning, wide(m::JobGate::Holder()) + L" is writing to the books: try again when it has finished.");
            return;
        }
        const int sel = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        if (sel < 0 || static_cast<size_t>(sel) >= rows_.size()) {
            ShowInfo(fx::InfoKind::Information, L"Select the " + wide(model_->Noun(1)) + L" in the list first.");
            return;
        }
        ReadOption();
        model_->rowInput = Text(IDC_TK_ROWIN);
        const TaskRow row = rows_[static_cast<size_t>(sel)];
        if (!row.selectable && model_->ActionOnRow()) {   // a backup that can't be restored: say why
            ShowInfo(fx::InfoKind::Warning, wide(row.note));
            return;
        }
        const std::string question = model_->RowQuestion(row);
        if (!question.empty()) {
            std::wstring action = wide(model_->RowAction());
            action.erase(std::remove(action.begin(), action.end(), L'&'), action.end());
            enum { kGo = 101 };
            if (fx::Ask(host() ? host()->hostWindow() : hwnd(), szApp, wide(question), L"",
                        {{kGo, action + L" " + wide(model_->Noun(1))}, {IDCANCEL, L"Keep it"}}, IDCANCEL, IDCANCEL) != kGo)
                return;
        }
        if (model_->ClosesOtherPages() && !CloseOtherPages()) {
            ShowInfo(fx::InfoKind::Information, L"Nothing was restored: a page was kept open.");
            return;
        }
        TaskStep s;
        try {
            s = model_->RunRowAction(cb, row);
        } catch (const std::exception& e) {
            s = {false, e.what()};
        }
        if (s.ok) {
            Load();
            FillRowInput();
            Broadcast(model_->Groups());
        }
        ShowInfo(s.ok ? fx::InfoKind::Success : fx::InfoKind::Error, wide(s.note));
    }

    // Restore (spec #9 D7): every page but this one and Home closes first, each
    // asking about its unsaved changes; false if one stayed open.
    bool CloseOtherPages()
    {
        auto* ws = host() ? dynamic_cast<fx::Workspace*>(host()) : nullptr;
        if (!ws) return true;
        const std::vector<HWND> all = ws->pages();
        for (HWND h : all) {
            if (h == hwnd() || !IsWindow(h)) continue;
            auto* p = dynamic_cast<fx::HostedPage*>(fx::Window::FromHandle(h));
            if (p && p->commandId == CM_HOME) continue;   // it holds no tables, and is always open
            ws->ClosePage(h);
            if (IsWindow(h)) return false;
        }
        return true;
    }

    // Restore's Browse...: a file from anywhere, added to the list and selected.
    void Browse()
    {
        std::wstring file(MAX_PATH, L'\0');
        static const wchar_t kFilter[] = L"Company databases (*.db;*.bak)\0*.db;*.bak\0All files (*.*)\0*.*\0";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof ofn;
        ofn.hwndOwner = host() ? host()->hostWindow() : hwnd();
        ofn.lpstrFilter = kFilter;
        ofn.lpstrFile = file.data();
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = L"Restore Databases";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
        if (!GetOpenFileNameW(&ofn)) return;
        file.resize(wcslen(file.c_str()));
        int at = -1;
        try {
            at = model_->AddFile(cb, fx::WideToUtf8(file));
        } catch (const std::exception& e) {
            ShowInfo(fx::InfoKind::Error, wide(e.what()));
        }
        Load();
        if (at >= 0) {
            ListView_SetItemState(list_, at, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(list_, at, FALSE);
            if (static_cast<size_t>(at) < rows_.size() && !rows_[static_cast<size_t>(at)].selectable)
                ShowInfo(fx::InfoKind::Warning, wide(rows_[static_cast<size_t>(at)].note));
        }
    }

    void ReadOption()
    {
        switch (model_->Option()) {
        case TaskOption::Date: model_->option = fx::GetDateField(Item(IDC_TK_DATE)); break;
        case TaskOption::Period: {
            const LRESULT sel = SendDlgItemMessageW(form(), IDC_TK_PERIOD, CB_GETCURSEL, 0, 0);
            model_->option = sel == CB_ERR ? std::string() : std::to_string(SendDlgItemMessageW(form(), IDC_TK_PERIOD, CB_GETITEMDATA, static_cast<WPARAM>(sel), 0));
            break;
        }
        case TaskOption::None: break;
        }
    }

    void Load()
    {
        if (job_.active()) return;
        ReadOption();
        try {
            rows_ = model_->Load(cb);
        } catch (const std::exception& e) {
            rows_.clear();
            ShowInfo(fx::InfoKind::Error, wide(e.what()));
        }
        FillList();
        ShowSummary();
        SetText(IDC_TK_RESULT, "");
        SendDlgItemMessageW(form(), IDC_TK_PROGRESS, PBM_SETPOS, 0, 0);
    }

    void FillList()
    {
        filling_ = true;
        SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list_);
        for (size_t i = 0; i < rows_.size(); ++i) {
            const TaskRow& r = rows_[i];
            std::wstring first = r.cells.empty() ? std::wstring() : wide(r.cells[0]);
            LVITEMW it{};
            it.mask = LVIF_TEXT;
            it.iItem = static_cast<int>(i);
            it.pszText = first.data();
            const int at = ListView_InsertItem(list_, &it);
            for (size_t c = 1; c < r.cells.size(); ++c) {
                std::wstring t = wide(r.cells[c]);
                ListView_SetItemText(list_, at, static_cast<int>(c), t.data());
            }
            std::wstring note = wide(r.note);
            ListView_SetItemText(list_, at, static_cast<int>(model_->Columns().size()), note.data());
            if (model_->Checks()) {
                // A row that can't be done has no check box (state image 0).
                ListView_SetItemState(list_, at, INDEXTOSTATEIMAGEMASK(r.selectable ? (r.checked ? 2 : 1) : 0), LVIS_STATEIMAGEMASK);
            }
        }
        SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(list_, nullptr, TRUE);
        filling_ = false;
    }

    void ShowSummary() { SetText(IDC_TK_SUMMARY, model_->Summary(rows_)); }

    void CheckAll(bool on)
    {
        for (TaskRow& r : rows_)
            if (r.selectable) r.checked = on;
        FillList();
        ShowSummary();
    }

    bool Start(bool ask)
    {
        if (job_.active()) return false;
        int checked = 0;
        for (const TaskRow& r : rows_)
            if (r.checked && r.selectable) ++checked;
        ReadPaymentOptions();
        if (ask) {
            if (model_->Checks() && checked == 0) {
                ShowInfo(fx::InfoKind::Information, L"Check what to " + wide(model_->Action()) + L" in the list.");
                return false;
            }
            enum { kGo = 101 };
            const std::wstring note = !model_->QuestionNote().empty() ? wide(model_->QuestionNote())
                                      : model_->PerItem() ? L"It runs in the background: you can go on working, or Stop it after any voucher."
                                                          : L"It runs in the background, as one step.";
            if (fx::Ask(host() ? host()->hostWindow() : hwnd(), szApp, wide(model_->Question(rows_)), note,
                        {{kGo, wide(model_->Action())}, {IDCANCEL, L"Cancel"}}, kGo, IDCANCEL) != kGo)
                return false;
        }
        work_ = std::make_shared<std::vector<TaskRow>>(rows_);
        auto rows = work_;
        TaskModel* model = model_.get();
        const std::wstring path = CurDir;
        std::string refusal;
        const bool started = job_.Start(
            hwnd(), model_->Title(), model_->Writes(),
            [rows, model, path](fx::Job::Context& ctx) {
                Code4 own;   // the job's own connection (WAL: the pages read meanwhile)
                if (own.open(std::wstring_view(path)) != r4success) return fx::Job::Outcome{false, false, "The company file can't be opened."};
                const TaskRun r = RunTask(*model, own, *rows, [&ctx](int done, int of) {
                    ctx.Progress(done, of);
                    return !ctx.cancelled();
                });
                own.close();
                return fx::Job::Outcome{r.ok, r.stoppedEarly, r.message};
            },
            &refusal);
        if (!started) {
            ShowInfo(fx::InfoKind::Warning, wide(refusal));
            return false;
        }
        HideInfo();
        for (int id : {IDC_TK_ACTION, IDC_TK_ALL, IDC_TK_NONE, IDC_TK_REFRESH, IDC_TK_DATE, IDC_TK_PERIOD, IDC_TK_PAYDATE, IDC_TK_BANK, IDC_TK_NUMBER, IDC_TK_ROWIN, IDC_TK_ROW}) EnableWindow(Item(id), FALSE);
        EnableWindow(Item(IDC_TK_STOP), TRUE);
        SetText(IDC_TK_RESULT, "Starting...");
        SetPageTitle(wide(model_->Title()) + L" - running");
        Broadcast(0);   // the frame: writing commands off while the job holds the books
        return true;
    }

    void Finished()
    {
        if (!work_) return;
        job_.Join();
        const m::JobState s = job_.state();
        rows_ = *work_;
        work_.reset();
        FillList();
        ShowSummary();
        SendDlgItemMessageW(form(), IDC_TK_PROGRESS, PBM_SETRANGE32, 0, s.of() > 0 ? s.of() : 1);
        SendDlgItemMessageW(form(), IDC_TK_PROGRESS, PBM_SETPOS, static_cast<WPARAM>(s.done()), 0);
        SetText(IDC_TK_RESULT, s.message());
        switch (s.phase()) {
        case m::JobPhase::Done: ShowInfo(fx::InfoKind::Success, wide(s.message())); break;
        case m::JobPhase::Cancelled: ShowInfo(fx::InfoKind::Warning, L"Stopped. " + wide(s.message())); break;
        case m::JobPhase::Failed: ShowInfo(fx::InfoKind::Error, L"It failed: " + wide(s.message())); break;
        default: break;
        }
        for (int id : {IDC_TK_ACTION, IDC_TK_ALL, IDC_TK_NONE, IDC_TK_REFRESH, IDC_TK_DATE, IDC_TK_PERIOD, IDC_TK_PAYDATE, IDC_TK_BANK, IDC_TK_NUMBER, IDC_TK_ROWIN, IDC_TK_ROW}) EnableWindow(Item(id), TRUE);
        EnableWindow(Item(IDC_TK_STOP), FALSE);
        SetPageTitle(wide(model_->Title()));
        Broadcast(model_->Groups());
    }

    void Broadcast(unsigned groups)
    {
        notifying_ = true;   // this page's own notes stay on its list
        NotifyDataChanged(groups, hwnd());
        notifying_ = false;
    }

    std::unique_ptr<TaskModel> model_;
    std::vector<std::string> banks_;   // the bank list's short names (G8)
    std::vector<TaskRow> rows_;
    std::shared_ptr<std::vector<TaskRow>> work_;   // the job's copy, until it finishes
    fx::Job job_;
    HWND list_ = nullptr;
    bool filling_ = false;
    bool notifying_ = false;
};

template <class P>
class Owned : public P {
public:
    using P::P;

protected:
    void OnFinalMessage() override { delete this; }
};

std::map<WORD, HWND> gOpen;

} // namespace

bool IsTaskPageCommand(WORD command)
{
    return commandOf(command) != nullptr;
}

HWND OpenTaskPage(fx::PageHost& host, WORD command)
{
    const TaskCommand* c = commandOf(command);
    if (!c) return nullptr;
    const auto it = gOpen.find(command);
    if (it != gOpen.end() && IsWindow(it->second)) {
        host.ActivatePage(it->second);
        return it->second;
    }
    std::unique_ptr<TaskModel> model = c->make();
    const std::wstring title = wide(model->Title());
    auto* page = new Owned<TaskPage>(command, std::move(model));
    HWND h = page->Open(host, title);
    if (!h) {
        delete page;
        return nullptr;
    }
    gOpen[command] = h;
    return h;
}

const std::vector<ClassicCommand>& TaskClassicCommands()
{
    static const std::vector<ClassicCommand> all = [] {
        std::vector<ClassicCommand> v;
        for (const TaskCommand& c : commands()) v.push_back({c.command, c.classic, c.classicText});
        return v;
    }();
    return all;
}

bool IsWritingCommand(WORD command)
{
    if (command == 1072) return false;   // Backup reads
    if (IsTaskPageCommand(command)) return true;
    for (const TaskCommand& c : commands())
        if (c.classic == command && c.command != 1072) return true;
    return command == 1074;   // ID_RESTORE
}

const std::vector<std::pair<int, unsigned>>& TaskPageAnchors()
{
    return kAnchors;
}

int StressTaskPages(int times)
{
    class StressWorkspace : public fx::Workspace {
    protected:
        bool OnDestroy() override { return true; }
    };
    auto objects = [] {
        HANDLE self = GetCurrentProcess();
        return static_cast<int>(GetGuiResources(self, GR_GDIOBJECTS) + GetGuiResources(self, GR_USEROBJECTS));
    };
    StressWorkspace w;
    if (!w.CreateWorkspace(hInst, L"GLTaskPageStress", L"task stress", nullptr, -1)) return -1;
    auto once = [&] {
        for (const TaskCommand& c : commands()) {
            if (c.command == 1072) continue;   // Backup would write a file
            HWND h = OpenTaskPage(w, c.command);
            auto* p = dynamic_cast<TaskPage*>(fx::Window::FromHandle(h));
            if (!p) return false;
            if (c.command == 10121 && !p->DryRun()) return false;   // posting no vouchers, started and stopped
            w.ClosePage(h);
            if (IsWindow(h)) return false;
        }
        MSG msg;   // the jobs' posted messages for closed pages
        while (PeekMessageW(&msg, nullptr, fx::Job::kProgress, fx::Job::kFinished, PM_REMOVE)) {}
        return true;
    };
    if (!once()) {
        DestroyWindow(w.hwnd());
        return -1;
    }
    const int before = objects();
    for (int i = 0; i < times; ++i)
        if (!once()) {
            DestroyWindow(w.hwnd());
            return -1;
        }
    const int growth = objects() - before;
    DestroyWindow(w.hwnd());
    return growth;
}
