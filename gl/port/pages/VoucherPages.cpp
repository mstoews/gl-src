//
// VoucherPages.cpp - see VoucherPages.h.
//
// Translation notes (decisions are spec #8 13):
//   - D4, 5.2: a voucher is typed into a grid and saved whole; Add/Mod per
//     line, the line number, Top / Next / Previous / Bottom and the voucher
//     list window go. Ctrl+PgUp and Ctrl+PgDn (and Previous / Next) move
//     between the GL page's vouchers.
//   - D9: no "save the current transaction?": unsaved changes are asked
//     about on leaving the voucher or closing the page.
//   - Post is on the GL page only while the voucher balances and its period
//     is open; posting saves first. AP and AR post the voucher shown through
//     their own Post (PostApVouchers / PostArVouchers).
//   - The AP and AR pages show the voucher's lines as they will be saved
//     (ComputeVoucher / ComputeSale) under the typed lines, with the totals.
//   - GL vouchers have no memo (gl_btch has no memo field); the AR page has
//     its memo beside the lines, saved with the voucher (Ar.h SaveArMemo).
//   - Print (the voucher slip) stays on the classic screens and the reports.
//
#include "VoucherPages.h"

#include "../Accounts.h"   // TrimText
#include "../DataChanged.h"
#include "../Globals.h"
#include "../Ini.h"
#include "../PortIds.h"
#include "../reports/RunReport.h"        // G7: the slip's preview
#include "../reports/VoucherReports.h"   // VoucherSlip
#include "GlVoucherModel.h"
#include "SubledgerModels.h"
#include "TaskModels.h"   // data_group

#include <CommCtrl.h>
#include <Uxtheme.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

TaxMode taxMode()
{
    return ParseTaxMode(fx::WideToUtf8(GetIniString(L"GL.INI", L"WITHHOLDING", L"TAX")));
}

std::string money(std::int64_t cents) { return m::FormatAmount(cents); }

// The page was just shown: the workspace shows a tab's page with SetWindowPos
// (SWP_SHOWWINDOW), which sends no WM_SHOWWINDOW.
bool shownNow(UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SHOWWINDOW) return wp != 0;
    return msg == WM_WINDOWPOSCHANGED && (reinterpret_cast<const WINDOWPOS*>(lp)->flags & SWP_SHOWWINDOW);
}

void fillPeriods(HWND combo, const std::vector<OpenPeriod>& periods, int selected)
{
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int sel = -1;
    for (const OpenPeriod& p : periods) {
        const std::wstring text = std::to_wstring(p.number) + L"  " + wide(p.description);
        const auto i = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())));
        SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(i), p.number);
        if (p.number == selected) sel = i;
    }
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);
}

int periodOf(HWND combo)
{
    const LRESULT sel = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    return sel == CB_ERR ? 0 : static_cast<int>(SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(sel), 0));
}

void addColumns(HWND list, std::initializer_list<std::pair<const wchar_t*, int>> cols, int rightFrom)
{
    const int dpi = static_cast<int>(GetDpiForWindow(list));
    int i = 0;
    for (const auto& [text, width96] : cols) {
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        c.fmt = i >= rightFrom ? LVCFMT_RIGHT : LVCFMT_LEFT;
        c.cx = MulDiv(width96, dpi, 96);
        c.pszText = const_cast<wchar_t*>(text);
        ListView_InsertColumn(list, i++, &c);
    }
    SetWindowTheme(list, L"Explorer", nullptr);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
}

void setRow(HWND list, int row, const std::vector<std::string>& cells)
{
    std::wstring first = cells.empty() ? std::wstring() : wide(cells[0]);
    LVITEMW it{};
    it.mask = LVIF_TEXT;
    it.iItem = row;
    it.pszText = first.data();
    ListView_InsertItem(list, &it);
    for (size_t c = 1; c < cells.size(); ++c) {
        std::wstring t = wide(cells[c]);
        ListView_SetItemText(list, row, static_cast<int>(c), t.data());
    }
}

const std::vector<std::pair<int, unsigned>> kTotalsAnchors = {
    {IDC_VP_LDEBIT, m::kBottomLeft}, {IDC_VP_DEBIT, m::kBottomLeft},  {IDC_VP_LCREDIT, m::kBottomLeft},
    {IDC_VP_CREDIT, m::kBottomLeft}, {IDC_VP_LDIFF, m::kBottomLeft},  {IDC_VP_DIFF, m::kBottomLeft},
    {IDC_VP_STATUS, m::kBottomWide}, {IDC_PG_NEW, m::kTopLeft},       {IDC_PG_SAVE, m::kTopLeft},
    {IDC_PG_DELETE, m::kTopLeft},    {IDC_VP_POST, m::kTopLeft},      {IDC_VP_STATE, m::kTopWide},
    {IDC_VP_GHEADER, m::kTopWide},
};

std::vector<std::pair<int, unsigned>> with(std::vector<std::pair<int, unsigned>> more)
{
    std::vector<std::pair<int, unsigned>> v = kTotalsAnchors;
    v.insert(v.end(), more.begin(), more.end());
    return v;
}

const std::vector<std::pair<int, unsigned>> kGlAnchors = with({
    {IDC_VP_PREV, m::kTopLeft}, {IDC_VP_NEXT, m::kTopLeft}, {IDC_VP_PRINT, m::kTopLeft}, {IDC_GLV_DATE, m::kTopLeft},
    {IDC_GLV_PERIOD, m::kTopLeft},
    {IDC_GLV_CHEQUE, m::kTopLeft}, {IDC_GLV_DESC, m::kTopLeft}, {IDC_VP_GRID, m::kAll},
});

const std::vector<std::pair<int, unsigned>> kApAnchors = with({
    {IDC_APV_VENDOR, m::kTopLeft},     {IDC_APV_VNAME, m::kTopLeft},    {IDC_APV_INVOICE, m::kTopLeft},
    {IDC_APV_ORDER, m::kTopLeft},      {IDC_APV_INVDATE, m::kTopLeft},  {IDC_APV_LDUE, m::kTopLeft},
    {IDC_APV_DUEDATE, m::kTopLeft},    {IDC_APV_PERIOD, m::kTopLeft},   {IDC_APV_REF, m::kTopLeft},
    {IDC_APV_LTERMS, m::kTopLeft},     {IDC_APV_TERMS, m::kTopLeft},    {IDC_APV_VATRATE, m::kTopLeft},
    {IDC_APV_WHRATE, m::kTopLeft},     {IDC_APV_TYPE, m::kTopLeft},     {IDC_APV_LCREDIT, m::kTopLeft},
    {IDC_APV_CREDIT, m::kTopLeft},     {IDC_APV_CREDITDEPT, m::kTopLeft}, {IDC_APV_BANK, m::kTopLeft},
    {IDC_APV_CREDITNAME, m::kTopLeft}, {IDC_APV_VAT, m::kTopLeft},      {IDC_APV_VATDEPT, m::kTopLeft},
    {IDC_APV_VATNAME, m::kTopLeft},    {IDC_APV_LCHEQUE, m::kTopLeft},  {IDC_APV_CHEQUE, m::kTopLeft},
    {IDC_VP_GRID, m::kAll},            {IDC_VP_LSPLIT, m::kBottomLeft}, {IDC_VP_SPLIT, m::kBottomWide},
});

const std::vector<std::pair<int, unsigned>> kArAnchors = with({
    {IDC_ARV_CUSTOMER, m::kTopLeft}, {IDC_ARV_CNAME, m::kTopLeft},    {IDC_ARV_INVOICE, m::kTopLeft},
    {IDC_ARV_ORDER, m::kTopLeft},    {IDC_ARV_INVDATE, m::kTopLeft},  {IDC_ARV_DUEDATE, m::kTopLeft},
    {IDC_ARV_PERIOD, m::kTopLeft},   {IDC_ARV_VIN, m::kTopLeft},      {IDC_ARV_REF, m::kTopLeft},
    {IDC_ARV_TERMS, m::kTopLeft},    {IDC_ARV_VATRATE, m::kTopLeft},  {IDC_ARV_AR, m::kTopLeft},
    {IDC_ARV_ARDEPT, m::kTopLeft},   {IDC_ARV_ARNAME, m::kTopLeft},   {IDC_ARV_VAT, m::kTopLeft},
    {IDC_ARV_VATDEPT, m::kTopLeft},  {IDC_ARV_VATNAME, m::kTopLeft},  {IDC_VP_GRID, m::kAll},
    {IDC_VP_LSPLIT, m::kBottomLeft}, {IDC_VP_SPLIT, m::kBottomWide},  {IDC_ARV_LMEMO, m::kBottomRight},
    {IDC_ARV_MEMO, m::kBottomRight},
});

const std::vector<std::pair<int, unsigned>> kReceiptsAnchors = {
    {IDC_RCP_FILTER, m::kTopLeft},  {IDC_RCP_ALL, m::kTopLeft},    {IDC_RCP_LIST, m::kLeftTall},
    {IDC_RCP_GROUP, m::kTopLeft},   {IDC_RCP_WHO, m::kTopLeft},    {IDC_RCP_WHAT, m::kTopLeft},
    {IDC_RCP_DATE, m::kTopLeft},    {IDC_RCP_BANK, m::kTopLeft},   {IDC_RCP_AMOUNT, m::kTopLeft},
    {IDC_RCP_DISCOUNT, m::kTopLeft}, {IDC_RCP_REF, m::kTopLeft},   {IDC_RCP_RECORD, m::kTopLeft},
    {IDC_RCP_REVERSE, m::kTopLeft},
};

const std::vector<std::pair<int, unsigned>> kListAnchors = {
    {IDC_VL_OPEN, m::kTopLeft}, {IDC_VL_NEW, m::kTopLeft}, {IDC_VL_POSTED, m::kTopLeft},
    {IDC_VL_FILTER, m::kTopLeft}, {IDC_VL_LIST, m::kAll},
};

// ---- the entry pages' common part ------------------------------------------------

class EntryPage : public fx::Page {
public:
    using fx::Page::Page;

    // /stress: five lines typed through the model, and the page left clean.
    virtual void TypeFiveLines() = 0;
    void Discard() { SetDirty(false); }

protected:
    // The model's grid, the page's fields: shown again.
    virtual void Fill() = 0;
    virtual void UpdateTotals() = 0;
    virtual int ControlOf(const std::string& field) const = 0;

    void AttachGrid(m::EntryGridModel& g)
    {
        grid_.Attach(Item(IDC_VP_GRID), &g);
        g.OnChanged = [this] {
            if (loading_) return;
            SetDirty(true);
            UpdateTotals();
        };
        grid_.OnAction = [this](m::GridAction a) {
            if (a == m::GridAction::Refused) ShowInfo(fx::InfoKind::Error, wide(grid_.model()->message()));
            else if (a == m::GridAction::Committed) HideInfo();
        };
    }

    // Ends an edit in the grid before saving: false if the cell's text is refused.
    bool CommitGrid()
    {
        m::EntryGridModel* g = grid_.model();
        if (!g || !g->editing()) return true;
        if (grid_.editing()) grid_.Press(m::GridKey::Tab);
        else g->Commit();
        if (g->editing()) {
            ShowInfo(fx::InfoKind::Error, wide(g->message()));
            return false;
        }
        return true;
    }

    // A refusal: the field marked, or the grid's cell made current.
    // The tables a save, post or delete on this page changes (spec #8 9.3).
    virtual unsigned Groups() const = 0;

    void ShowResult(const EntryResult& r)
    {
        if (r.ok) {
            ShowInfo(fx::InfoKind::Success, wide(r.message));
            NotifyDataChanged(Groups(), hwnd());
            return;
        }
        if (!r.field.empty()) {
            if (const int id = ControlOf(r.field)) MarkField(id, r.message, true);
        } else if (r.row >= 0 && grid_.model()) {
            grid_.model()->SetCurrent(r.row, std::max(0, grid_.model()->ColumnOf(r.column)));
            grid_.Refresh();
            SetFocus(grid_.list());
        }
        ShowInfo(fx::InfoKind::Error, wide(r.message));
    }

    bool SwitchAllowed(const std::wstring& what)
    {
        if (!IsDirty()) return true;
        enum { kSave = 101, kDiscard = 102 };
        const int chosen = fx::Ask(host() ? host()->hostWindow() : hwnd(), szApp, L"Save the changes to " + what + L"?", L"",
                                   {{kSave, L"Save"}, {kDiscard, L"Don't save"}, {IDCANCEL, L"Cancel"}}, kSave, IDCANCEL);
        if (chosen == kSave) return Save();
        return chosen == kDiscard;
    }

    bool AskYes(const std::wstring& question, const std::wstring& detail, const std::wstring& yes)
    {
        enum { kYes = 101 };
        return fx::Ask(host() ? host()->hostWindow() : hwnd(), szApp, question, detail, {{kYes, yes}, {IDCANCEL, L"Cancel"}},
                       kYes, IDCANCEL) == kYes;
    }

    void SetDate(int id, const std::string& yyyymmdd) { fx::SetDateField(Item(id), yyyymmdd); }
    std::string Date(int id) const { return fx::GetDateField(Item(id)); }
    int Int(int id) const { return std::atoi(m::Trim(Text(id)).c_str()); }
    void SetInt(int id, int v) { SetText(id, v ? std::to_string(v) : std::string()); }

    bool OnFormNotify(NMHDR* nm, LRESULT& result) override
    {
        if (grid_.OnNotify(nm, result)) return true;
        if (nm->code == DTN_DATETIMECHANGE && !filling() && !loading_) {
            SetDirty(true);
            OnHeaderChanged();
        }
        return false;
    }

    bool OnFormCommand(WORD id, WORD code, HWND) override
    {
        if (code == CBN_SELCHANGE && !filling() && !loading_) {
            SetDirty(true);
            ClearMark(id);
            OnHeaderChanged();
            return true;
        }
        return false;
    }

    void OnFieldChanged(int id) override
    {
        fx::Page::OnFieldChanged(id);
        if (!loading_) OnHeaderChanged();
    }

    virtual void OnHeaderChanged() {}

    class Loading {
    public:
        explicit Loading(EntryPage& p) : p_(p), f_(p), was_(p.loading_) { p_.loading_ = true; }
        ~Loading() { p_.loading_ = was_; }

    private:
        EntryPage& p_;
        Filling f_;
        bool was_;
    };

    fx::EntryGrid grid_;
    bool loading_ = false;
};

// ---- General Ledger voucher -----------------------------------------------------------

std::vector<HWND> gGlPages;

class GlVoucherPage : public EntryPage {
public:
    GlVoucherPage(bool adjustment, long voucher)
        : EntryPage(IDD_PG_GLVOUCHER), model_(cb, adjustment, iniBatchno()), start_(voucher)
    {
        commandId = adjustment ? 8081 : 10076;   // Prior Period Entry / General Ledger Voucher
    }

    bool adjustment() const { return model_.adjustment(); }
    long number() const { return model_.number; }
    // The tab's title, after the host has named the page (Open names it first).
    void Retitle()
    {
        const std::wstring kind = model_.adjustment() ? L"Adjustment" : L"GL Voucher";
        SetPageTitle(model_.isNew() ? kind : kind + L" - " + std::to_wstring(model_.number));
    }

    unsigned Groups() const override { return data_group::kGlVouchers | data_group::kGlBalances | data_group::kReports; }

    bool Save() override
    {
        if (SaveWaitsForJob(*this)) return false;   // spec #8 9.1, R5
        if (!CommitGrid()) return false;
        ReadHeader();
        const EntryResult r = model_.Save();
        if (r.ok) Fill();
        ShowResult(r);
        return r.ok;
    }

    void TypeFiveLines() override
    {
        // Through the grid's keys and its editor: each cell typed and Tab, F2 and Esc
        // on one, a line inserted and removed, and the removal undone.
        const char* accounts[] = {"101000", "102101", "111101", "101000", "102101"};
        for (int i = 0; i < 5; ++i) {
            for (const char* text : {accounts[i], "", "STRESS", "", i < 4 ? "1.00" : "", i < 4 ? "" : "4.00", "", "", ""}) {
                if (*text) grid_.Type(text);
                grid_.Press(m::GridKey::Tab);
            }
        }
        model_.grid().SetCurrent(0, 2);
        grid_.Press(m::GridKey::F2);
        grid_.Press(m::GridKey::Esc);
        grid_.Press(m::GridKey::Insert);
        grid_.Press(m::GridKey::CtrlDelete);
        grid_.Press(m::GridKey::CtrlDelete);
        grid_.Press(m::GridKey::CtrlZ);
    }

    bool Goto(long voucher)
    {
        switch (model_.Load(voucher)) {
        case GlVoucherModel::Open::Opened: Fill(); return true;
        case GlVoucherModel::Open::Posted:
            ShowInfo(fx::InfoKind::Warning, L"Voucher " + std::to_wstring(voucher) + L" has been posted: it can't be changed.");
            break;
        case GlVoucherModel::Open::NotAdjustment:
            ShowInfo(fx::InfoKind::Warning, L"Only prior period adjustment entries may be modified here.");
            break;
        case GlVoucherModel::Open::WrongMode:
            ShowInfo(fx::InfoKind::Warning, L"Voucher " + std::to_wstring(voucher) + L" is a prior period adjustment: open it from Prior Period Entry.");
            break;
        case GlVoucherModel::Open::NoLines:
            ShowInfo(fx::InfoKind::Warning, L"Voucher " + std::to_wstring(voucher) + L" has no lines.");
            break;
        case GlVoucherModel::Open::NotFound:
            ShowInfo(fx::InfoKind::Warning, L"There is no voucher " + std::to_wstring(voucher) + L".");
            break;
        }
        return false;
    }

protected:
    bool OnInitPage() override
    {
        SetAnchors(kGlAnchors);
        AttachGrid(model_.grid());
        fx::SetAccessibleName(Item(IDC_VP_GRID), L"Voucher lines");   // spec #8 12
        grid_.SetLookup(0, &model_.accounts());
        grid_.SetLookup(6, &model_.reps());
        grid_.SetLookup(7, &model_.products());
        grid_.SetLookup(8, &model_.doctors());
        grid_.OnPick = [this](int row, int col, const m::LookupItem& item) {
            if (col != 0) return;
            model_.grid().SetCell(row, "dept", item.sub);
            model_.grid().SetCell(row, "name", item.name);
            grid_.Refresh();
        };
        SendDlgItemMessageW(form(), IDC_GLV_DESC, EM_LIMITTEXT, 40, 0);
        SendDlgItemMessageW(form(), IDC_GLV_CHEQUE, EM_LIMITTEXT, 7, 0);
        model_.New(today());
        if (start_) Goto(start_);
        Fill();
        return true;
    }

    void Fill() override
    {
        {
            Loading l(*this);
            SetDate(IDC_GLV_DATE, model_.date);
            fillPeriods(Item(IDC_GLV_PERIOD), model_.periods(), model_.period);
            SetText(IDC_GLV_DESC, model_.description);
            SetText(IDC_GLV_CHEQUE, model_.cheque);
            grid_.Refresh();
        }
        const std::wstring kind = model_.adjustment() ? L"Adjustment" : L"GL Voucher";
        SetText(IDC_VP_STATE, fx::WideToUtf8(model_.isNew()
                                                 ? L"New " + std::wstring(model_.adjustment() ? L"adjustment" : L"voucher") +
                                                       L": it will be number " + std::to_wstring(model_.nextNumber()) + L"."
                                                 : kind + L" " + std::to_wstring(model_.number) + L", not posted."));
        SetPageTitle(model_.isNew() ? kind : kind + L" - " + std::to_wstring(model_.number));
        EnableWindow(Item(IDC_PG_DELETE), !model_.isNew());
        ClearMarks();
        SetDirty(false);
        UpdateTotals();
    }

    void UpdateTotals() override
    {
        const m::EntryGridModel& g = model_.grid();
        const std::int64_t d = g.Total("debit"), c = g.Total("credit");
        SetText(IDC_VP_DEBIT, money(d));
        SetText(IDC_VP_CREDIT, money(c));
        SetText(IDC_VP_DIFF, money(d - c));
        SetText(IDC_VP_STATUS, d == 0 && c == 0 ? std::string()
                               : d == c       ? std::string("Balanced.")
                                              : "Out of balance by " + money(d > c ? d - c : c - d) + ".");
        // Post only while the voucher balances and its period is open (spec #8 5.2).
        const bool post = g.Balances("debit", "credit") && model_.PeriodOpen(periodOf(Item(IDC_GLV_PERIOD)));
        ShowWindow(Item(IDC_VP_POST), post ? SW_SHOW : SW_HIDE);
    }

    int ControlOf(const std::string& f) const override
    {
        if (f == "description") return IDC_GLV_DESC;
        if (f == "date") return IDC_GLV_DATE;
        if (f == "period") return IDC_GLV_PERIOD;
        return 0;
    }

    void OnHeaderChanged() override { UpdateTotals(); }

    void OnNew() override
    {
        if (!SwitchAllowed(Named())) return;
        HideInfo();
        model_.New(today());
        Fill();
        SetFocus(Item(IDC_GLV_DESC));
    }

    bool OnFormCommand(WORD id, WORD code, HWND ctl) override
    {
        if (code == BN_CLICKED) switch (id) {
            case IDC_PG_NEW: OnNew(); return true;
            case IDC_PG_SAVE: Save(); return true;
            case IDC_PG_DELETE: Delete(); return true;
            case IDC_VP_POST: Post(); return true;
            case IDC_VP_PREV: Move(-1); return true;
            case IDC_VP_NEXT: Move(1); return true;
            case IDC_VP_PRINT: PrintSlip(); return true;
            default: break;
            }
        return EntryPage::OnFormCommand(id, code, ctl);
    }

    // G7 (Voucher Entry's Print, spec #3 D7): the saved voucher's slip, previewed in a tab.
    void PrintSlip()
    {
        if (model_.isNew()) {
            ShowInfo(fx::InfoKind::Information, L"Save the voucher first, then print its slip.");
            return;
        }
        if (IsDirty()) {
            ShowInfo(fx::InfoKind::Information, L"The slip prints the voucher as saved: save the changes first.");
            return;
        }
        const long n = model_.number;
        RunReport(*host(), {}, [n](const ReportContext& ctx) { return VoucherSlip(cb, ctx, n); });
    }

    bool OnShortcut(const MSG& msg) override
    {
        if (msg.message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) && (msg.wParam == VK_PRIOR || msg.wParam == VK_NEXT)) {
            Move(msg.wParam == VK_PRIOR ? -1 : 1);
            return true;
        }
        return EntryPage::OnShortcut(msg);
    }

    bool OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override
    {
        if (msg == WM_DESTROY) gGlPages.erase(std::remove(gGlPages.begin(), gGlPages.end(), hwnd()), gGlPages.end());
        if (msg == kDataChanged) {   // spec #8 9.3: a job or another page changed the vouchers
            if ((wp & data_group::kGlVouchers) && reinterpret_cast<HWND>(lp) != hwnd() && !model_.isNew()) {
                const long n = model_.number;
                if (!IsDirty()) {
                    if (!Goto(n)) {   // posted or cancelled meanwhile: Goto said so in the bar
                        model_.New(today());
                        Fill();
                    }
                } else {
                    ShowInfo(fx::InfoKind::Warning, L"Voucher " + std::to_wstring(n) + L" may have changed since you opened it. "
                                                    L"Save keeps your changes.");
                }
            }
            result = 0;
            return true;
        }
        return EntryPage::OnMessage(msg, wp, lp, result);
    }

private:
    std::wstring Named() const
    {
        return model_.isNew() ? L"the new voucher" : L"voucher " + std::to_wstring(model_.number);
    }

    void ReadHeader()
    {
        model_.description = m::Trim(Text(IDC_GLV_DESC));
        model_.cheque = m::Trim(Text(IDC_GLV_CHEQUE));
        model_.date = Date(IDC_GLV_DATE);
        model_.period = periodOf(Item(IDC_GLV_PERIOD));
    }

    void Move(int step)
    {
        const long n = model_.Neighbour(step);
        if (!n) {
            ShowInfo(fx::InfoKind::Information, step < 0 ? L"There is no earlier voucher to change." : L"There is no later voucher to change.");
            return;
        }
        if (!SwitchAllowed(Named())) return;
        HideInfo();
        Goto(n);
    }

    void Delete()
    {
        if (model_.isNew()) return;
        if (!AskYes(L"Delete voucher " + std::to_wstring(model_.number) + L"?", L"Its lines are deleted with it. It can't be undone.",
                    L"Delete voucher"))
            return;
        const EntryResult r = model_.Delete();
        Fill();
        ShowResult(r);
    }

    void Post()
    {
        if (IsDirty() && !Save()) return;
        if (!model_.CanPost()) {
            ShowInfo(fx::InfoKind::Warning, L"The voucher must balance, in an open period, to be posted.");
            return;
        }
        const long n = model_.number;
        if (!AskYes(L"Post voucher " + std::to_wstring(n) + L"?", L"Its lines go to the ledger; it can then only be recovered.",
                    L"Post voucher"))
            return;
        const EntryResult r = model_.Post();
        Fill();
        ShowResult(r);
    }

    GlVoucherModel model_;
    long start_;
};

// ---- AP: purchase and cash invoice vouchers ------------------------------------------

class ApVoucherPage : public EntryPage {
public:
    explicit ApVoucherPage(ApKind kind) : EntryPage(IDD_PG_APVOUCHER), model_(cb, kind, taxMode())
    {
        commandId = kind == ApKind::Cash ? 1079 : 4044;
    }

    unsigned Groups() const override { return data_group::kAp | data_group::kReports; }

    bool Save() override
    {
        if (SaveWaitsForJob(*this)) return false;   // spec #8 9.1, R5
        if (!CommitGrid()) return false;
        ReadHeader();
        const EntryResult r = model_.Save();
        if (r.ok) Fill();
        ShowResult(r);
        return r.ok;
    }

    void TypeFiveLines() override
    {
        for (int i = 0; i < 5; ++i) {
            model_.grid().SetCell(i, "account", "501000");
            model_.grid().SetCell(i, "amount", "10.00");
        }
        grid_.Refresh();
        UpdateTotals();
    }

protected:
    bool cash() const { return model_.kind() == ApKind::Cash; }

    bool OnInitPage() override
    {
        SetAnchors(kApAnchors);
        AttachGrid(model_.grid());
        fx::SetAccessibleName(Item(IDC_VP_GRID), model_.kind() == ApKind::Cash ? L"Cash voucher lines" : L"Expense lines");   // spec #8 12
        grid_.SetLookup(0, &model_.lookups().accounts);
        grid_.SetLookup(4, &model_.lookups().reps);
        grid_.SetLookup(5, &model_.lookups().products);
        grid_.SetLookup(6, &model_.lookups().doctors);
        grid_.OnPick = [this](int row, int col, const m::LookupItem& item) {
            if (col != 0) return;
            model_.grid().SetCell(row, "dept", item.sub);
            if (model_.grid().Cell(row, "descript").empty()) model_.grid().SetCell(row, "descript", item.name);
            grid_.Refresh();
        };
        vendors_.Attach(Item(IDC_APV_VENDOR), &model_.lookups().vendors, [this](const m::LookupItem& it) { PickVendor(it.code); });
        addColumns(Item(IDC_VP_SPLIT), {{L"Line", 70}, {L"Account", 80}, {L"Description", 220}, {L"Debit", 90}, {L"Credit", 90}}, 3);
        HWND type = Item(IDC_APV_TYPE);
        for (const wchar_t* t : {L"Company", L"Individual"}) SendMessageW(type, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
        HWND bank = Item(IDC_APV_BANK);
        for (const ApBank& b : model_.banks()) {
            const std::wstring text = wide(TrimText(b.shortName) + "  " + TrimText(b.name));
            SendMessageW(bank, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        }
        for (int id : {IDC_APV_CREDIT, IDC_APV_CREDITDEPT, IDC_APV_LDUE, IDC_APV_DUEDATE, IDC_APV_LTERMS, IDC_APV_TERMS})
            ShowWindow(Item(id), cash() ? SW_HIDE : SW_SHOW);
        for (int id : {IDC_APV_BANK, IDC_APV_LCHEQUE, IDC_APV_CHEQUE}) ShowWindow(Item(id), cash() ? SW_SHOW : SW_HIDE);
        SetText(IDC_APV_LCREDIT, cash() ? "&Bank:" : "AP account:");
        SendDlgItemMessageW(form(), IDC_APV_INVOICE, EM_LIMITTEXT, cash() ? 10 : 8, 0);
        model_.New(today());
        Fill();
        return true;
    }

    void Fill() override
    {
        {
            Loading l(*this);
            SetText(IDC_APV_VENDOR, model_.vendor);
            picked_ = model_.vendor;
            SetText(IDC_APV_VNAME, model_.vendorName);
            SetText(IDC_APV_INVOICE, model_.invoiceNo);
            SetText(IDC_APV_ORDER, model_.orderNo);
            SetDate(IDC_APV_INVDATE, model_.invoiceDate);
            SetDate(IDC_APV_DUEDATE, model_.dueDate);
            fillPeriods(Item(IDC_APV_PERIOD), model_.periods(), model_.period);
            SetText(IDC_APV_REF, model_.reference);
            SetInt(IDC_APV_TERMS, model_.terms);
            SetInt(IDC_APV_VATRATE, model_.vatRate);
            SetInt(IDC_APV_WHRATE, model_.whRate);
            SendDlgItemMessageW(form(), IDC_APV_TYPE, CB_SETCURSEL, static_cast<WPARAM>(model_.individual), 0);
            SetText(IDC_APV_CREDIT, model_.credit.account);
            SetText(IDC_APV_CREDITDEPT, model_.credit.dept);
            int bank = -1;
            for (size_t i = 0; i < model_.banks().size(); ++i)
                if (model_.banks()[i].account == model_.credit) bank = static_cast<int>(i);
            SendDlgItemMessageW(form(), IDC_APV_BANK, CB_SETCURSEL, static_cast<WPARAM>(bank), 0);
            SetText(IDC_APV_VAT, model_.vat.account);
            SetText(IDC_APV_VATDEPT, model_.vat.dept);
            SetText(IDC_APV_CHEQUE, model_.cheque);
            ShowNames();
            grid_.Refresh();
        }
        const std::wstring kind = cash() ? L"Cash Voucher" : L"Purchase Voucher";
        SetText(IDC_VP_STATE, fx::WideToUtf8(model_.isNew() ? L"New voucher: it will be number " + std::to_wstring(model_.nextNumber()) + L"."
                                                             : kind + L" " + std::to_wstring(model_.number) + L", not posted."));
        SetPageTitle(model_.isNew() ? kind + L"s" : kind + L" - " + std::to_wstring(model_.number));
        EnableWindow(Item(IDC_PG_DELETE), !model_.isNew());
        ClearMarks();
        SetDirty(false);
        UpdateTotals();
    }

    void ShowNames()
    {
        SetText(IDC_APV_CREDITNAME, model_.lookups().AccountName(model_.credit));
        SetText(IDC_APV_VATNAME, model_.lookups().AccountName(model_.vat));
    }

    void UpdateTotals() override
    {
        HWND split = Item(IDC_VP_SPLIT);
        const ComputedVoucher v = model_.Split();
        SendMessageW(split, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(split);
        int row = 0;
        for (const ApLine& l : v.lines) {
            const char* role = l.role == ApLineRole::Expense ? "Expense" : l.role == ApLineRole::Vat ? "VAT"
                               : l.role == ApLineRole::WithHolding ? "W/H tax" : cash() ? "Bank" : "AP";
            setRow(split, row++, {role, TrimText(l.account.account) + (l.account.dept.empty() ? "" : "-" + TrimText(l.account.dept)),
                                  TrimText(l.descript), l.debit ? money(l.debit) : "", l.credit ? money(l.credit) : ""});
        }
        SendMessageW(split, WM_SETREDRAW, TRUE, 0);
        SetText(IDC_VP_DEBIT, money(v.debits));
        SetText(IDC_VP_CREDIT, money(v.credits));
        SetText(IDC_VP_DIFF, money(v.payable));
        SetText(IDC_VP_STATUS, v.lines.empty() ? std::string()
                                               : "VAT " + money(v.vat) + ", W/H " + money(v.wh) + (cash() ? ", paid " : ", owed ") +
                                                     money(v.credit) + ".");
        EnableWindow(Item(IDC_VP_POST), model_.CanPost());
    }

    int ControlOf(const std::string& f) const override
    {
        static const std::map<std::string, int> ids = {
            {"vendor", IDC_APV_VENDOR},   {"invoiceNo", IDC_APV_INVOICE}, {"invoiceDate", IDC_APV_INVDATE},
            {"period", IDC_APV_PERIOD},   {"reference", IDC_APV_REF},     {"type", IDC_APV_TYPE},
            {"vatRate", IDC_APV_VATRATE}, {"whRate", IDC_APV_WHRATE},     {"vat", IDC_APV_VAT}};
        if (f == "credit") return cash() ? IDC_APV_BANK : IDC_APV_CREDIT;
        const auto it = ids.find(f);
        return it == ids.end() ? 0 : it->second;
    }

    void OnHeaderChanged() override
    {
        ReadHeader();
        ShowNames();
        UpdateTotals();
    }

    void OnNew() override
    {
        if (!SwitchAllowed(Named())) return;
        HideInfo();
        model_.New(today());
        Fill();
        SetFocus(Item(IDC_APV_VENDOR));
    }

    bool OnFormCommand(WORD id, WORD code, HWND ctl) override
    {
        if (code == BN_CLICKED) switch (id) {
            case IDC_PG_NEW: OnNew(); return true;
            case IDC_PG_SAVE: Save(); return true;
            case IDC_PG_DELETE: Delete(); return true;
            case IDC_VP_POST: Post(); return true;
            default: break;
            }
        if (id == IDC_APV_VENDOR) {
            if (code == EN_CHANGE && !filling()) vendors_.TextChanged();
            if (code == EN_KILLFOCUS) {
                vendors_.FocusLost();
                const std::string typed = m::Trim(Text(IDC_APV_VENDOR));
                if (!typed.empty() && typed != picked_) PickVendor(typed);   // not model_.vendor: every key copies the header into it
            }
        }
        return EntryPage::OnFormCommand(id, code, ctl);
    }

private:
    std::wstring Named() const { return model_.isNew() ? L"the new voucher" : L"voucher " + std::to_wstring(model_.number); }

    void PickVendor(const std::string& no)
    {
        ReadHeader();
        if (!model_.SelectVendor(no)) {
            MarkField(IDC_APV_VENDOR, "There is no vendor " + no + ": add them in Purchase Vendors.");
            return;
        }
        picked_ = model_.vendor;
        ClearMark(IDC_APV_VENDOR);
        Loading l(*this);
        SetText(IDC_APV_VENDOR, model_.vendor);
        SetText(IDC_APV_VNAME, model_.vendorName);
        SetInt(IDC_APV_TERMS, model_.terms);
        SetInt(IDC_APV_VATRATE, model_.vatRate);
        SetInt(IDC_APV_WHRATE, model_.whRate);
        SetDate(IDC_APV_DUEDATE, model_.dueDate);
        SendDlgItemMessageW(form(), IDC_APV_TYPE, CB_SETCURSEL, static_cast<WPARAM>(model_.individual), 0);
        SetText(IDC_APV_CREDIT, model_.credit.account);
        SetText(IDC_APV_CREDITDEPT, model_.credit.dept);
        SetText(IDC_APV_VAT, model_.vat.account);
        SetText(IDC_APV_VATDEPT, model_.vat.dept);
        ShowNames();
        UpdateTotals();
        SetDirty(true);
    }

    void ReadHeader()
    {
        model_.vendor = m::Trim(Text(IDC_APV_VENDOR));
        model_.invoiceNo = m::Trim(Text(IDC_APV_INVOICE));
        model_.orderNo = m::Trim(Text(IDC_APV_ORDER));
        model_.invoiceDate = Date(IDC_APV_INVDATE);
        model_.dueDate = Date(IDC_APV_DUEDATE);
        model_.period = periodOf(Item(IDC_APV_PERIOD));
        model_.reference = m::Trim(Text(IDC_APV_REF));
        model_.terms = Int(IDC_APV_TERMS);
        model_.vatRate = Int(IDC_APV_VATRATE);
        model_.whRate = Int(IDC_APV_WHRATE);
        model_.individual = static_cast<int>(SendDlgItemMessageW(form(), IDC_APV_TYPE, CB_GETCURSEL, 0, 0));
        if (cash()) {
            const auto b = static_cast<int>(SendDlgItemMessageW(form(), IDC_APV_BANK, CB_GETCURSEL, 0, 0));
            model_.credit = b >= 0 && static_cast<size_t>(b) < model_.banks().size() ? model_.banks()[static_cast<size_t>(b)].account
                                                                                       : ApAccount{};
            model_.cheque = m::Trim(Text(IDC_APV_CHEQUE));
        } else {
            model_.credit = {m::Trim(Text(IDC_APV_CREDIT)), m::Trim(Text(IDC_APV_CREDITDEPT))};
        }
        model_.vat = {m::Trim(Text(IDC_APV_VAT)), m::Trim(Text(IDC_APV_VATDEPT))};
    }

    void Delete()
    {
        if (model_.isNew()) return;
        if (!AskYes(L"Delete voucher " + std::to_wstring(model_.number) + L"?", L"It can't be undone.", L"Delete voucher")) return;
        const EntryResult r = model_.Delete();
        Fill();
        ShowResult(r);
    }

    void Post()
    {
        if (IsDirty() && !Save()) return;
        if (!model_.CanPost()) return;
        if (!AskYes(L"Post voucher " + std::to_wstring(model_.number) + L"?", L"", L"Post voucher")) return;
        const EntryResult r = model_.Post();
        Fill();
        ShowResult(r);
    }

    ApVoucherModel model_;
    fx::Lookup vendors_;
    std::string picked_;   // the vendor whose details are shown
};

// ---- AR: sales invoice vouchers --------------------------------------------------------

class ArInvoicePage : public EntryPage {
public:
    ArInvoicePage() : EntryPage(IDD_PG_ARINVOICE), model_(cb) { commandId = 5045; }

    unsigned Groups() const override { return data_group::kAr | data_group::kReports; }

    bool Save() override
    {
        if (SaveWaitsForJob(*this)) return false;   // spec #8 9.1, R5
        if (!CommitGrid()) return false;
        ReadHeader();
        const EntryResult r = model_.Save();
        if (r.ok) Fill();
        ShowResult(r);
        return r.ok;
    }

    void TypeFiveLines() override
    {
        for (int i = 0; i < 5; ++i) {
            model_.grid().SetCell(i, "account", model_.sales.account);
            model_.grid().SetCell(i, "dept", model_.sales.dept);
            model_.grid().SetCell(i, "amount", "11.00");
        }
        grid_.Refresh();
        UpdateTotals();
    }

protected:
    bool OnInitPage() override
    {
        SetAnchors(kArAnchors);
        AttachGrid(model_.grid());
        fx::SetAccessibleName(Item(IDC_VP_GRID), L"Sales lines");   // spec #8 12
        grid_.SetLookup(0, &model_.lookups().products);
        grid_.SetLookup(1, &model_.lookups().accounts);
        grid_.SetLookup(7, &model_.lookups().reps);
        grid_.SetLookup(8, &model_.lookups().doctors);
        grid_.OnPick = [this](int row, int col, const m::LookupItem& item) {
            if (col != 1) return;
            model_.grid().SetCell(row, "dept", item.sub);
            grid_.Refresh();
        };
        customers_.Attach(Item(IDC_ARV_CUSTOMER), &model_.lookups().customers,
                          [this](const m::LookupItem& it) { PickCustomer(it.code); });
        addColumns(Item(IDC_VP_SPLIT), {{L"Line", 70}, {L"Account", 80}, {L"Description", 160}, {L"Debit", 80}, {L"Credit", 80}}, 3);
        SendDlgItemMessageW(form(), IDC_ARV_INVOICE, EM_LIMITTEXT, 8, 0);
        model_.New(today());
        Fill();
        return true;
    }

    void Fill() override
    {
        {
            Loading l(*this);
            SetText(IDC_ARV_CUSTOMER, model_.customer);
            picked_ = model_.customer;
            ShowCustomerName();
            SetText(IDC_ARV_INVOICE, model_.invoiceNo);
            SetText(IDC_ARV_ORDER, model_.orderNo);
            SetDate(IDC_ARV_INVDATE, model_.invoiceDate);
            SetDate(IDC_ARV_DUEDATE, model_.dueDate);
            fillPeriods(Item(IDC_ARV_PERIOD), model_.periods(), model_.period);
            SetText(IDC_ARV_VIN, model_.vin);
            SetText(IDC_ARV_REF, model_.reference);
            SetInt(IDC_ARV_TERMS, model_.terms);
            SetInt(IDC_ARV_VATRATE, model_.vatRate);
            SetText(IDC_ARV_AR, model_.ar.account);
            SetText(IDC_ARV_ARDEPT, model_.ar.dept);
            SetText(IDC_ARV_VAT, model_.vat.account);
            SetText(IDC_ARV_VATDEPT, model_.vat.dept);
            SetText(IDC_ARV_MEMO, model_.memo);
            ShowNames();
            grid_.Refresh();
        }
        SetText(IDC_VP_STATE, fx::WideToUtf8(model_.isNew() ? L"New sales voucher: it will be number " + std::to_wstring(model_.nextNumber()) + L"."
                                                             : L"Sales voucher " + std::to_wstring(model_.number) + L", not posted."));
        SetPageTitle(model_.isNew() ? L"Sales Vouchers" : L"Sales Voucher - " + std::to_wstring(model_.number));
        EnableWindow(Item(IDC_PG_DELETE), !model_.isNew());
        ClearMarks();
        SetDirty(false);
        UpdateTotals();
    }

    void ShowCustomerName()
    {
        const int i = model_.lookups().customers.Exact(model_.customer);
        SetText(IDC_ARV_CNAME, i >= 0 ? model_.lookups().customers.items()[static_cast<size_t>(i)].name : std::string());
    }

    void ShowNames()
    {
        SetText(IDC_ARV_ARNAME, model_.lookups().AccountName(model_.ar));
        SetText(IDC_ARV_VATNAME, model_.lookups().AccountName(model_.vat));
    }

    void UpdateTotals() override
    {
        HWND split = Item(IDC_VP_SPLIT);
        const ComputedSale v = model_.Split();
        SendMessageW(split, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(split);
        int row = 0;
        for (const ArLine& l : v.lines) {
            const char* role = l.role == ArLineRole::Sales ? "Sales" : l.role == ArLineRole::Vat ? "VAT" : "AR";
            setRow(split, row++, {role, TrimText(l.account.account) + (l.account.dept.empty() ? "" : "-" + TrimText(l.account.dept)),
                                  TrimText(l.descript), l.debit ? money(l.debit) : "", l.credit ? money(l.credit) : ""});
        }
        SendMessageW(split, WM_SETREDRAW, TRUE, 0);
        SetText(IDC_VP_DEBIT, money(v.debits));
        SetText(IDC_VP_CREDIT, money(v.credits));
        SetText(IDC_VP_DIFF, money(v.total));
        SetText(IDC_VP_STATUS, v.lines.empty() ? std::string() : "VAT " + money(v.vat) + ".");
        EnableWindow(Item(IDC_VP_POST), model_.CanPost());
    }

    int ControlOf(const std::string& f) const override
    {
        static const std::map<std::string, int> ids = {
            {"customer", IDC_ARV_CUSTOMER}, {"invoiceNo", IDC_ARV_INVOICE}, {"invoiceDate", IDC_ARV_INVDATE},
            {"period", IDC_ARV_PERIOD},     {"reference", IDC_ARV_REF},     {"ar", IDC_ARV_AR},
            {"vatRate", IDC_ARV_VATRATE},   {"vat", IDC_ARV_VAT}};
        const auto it = ids.find(f);
        return it == ids.end() ? 0 : it->second;
    }

    void OnHeaderChanged() override
    {
        ReadHeader();
        ShowNames();
        UpdateTotals();
    }

    void OnNew() override
    {
        if (!SwitchAllowed(Named())) return;
        HideInfo();
        model_.New(today());
        Fill();
        SetFocus(Item(IDC_ARV_CUSTOMER));
    }

    bool OnFormCommand(WORD id, WORD code, HWND ctl) override
    {
        if (code == BN_CLICKED) switch (id) {
            case IDC_PG_NEW: OnNew(); return true;
            case IDC_PG_SAVE: Save(); return true;
            case IDC_PG_DELETE: Delete(); return true;
            case IDC_VP_POST: Post(); return true;
            default: break;
            }
        if (id == IDC_ARV_CUSTOMER) {
            if (code == EN_CHANGE && !filling()) customers_.TextChanged();
            if (code == EN_KILLFOCUS) {
                customers_.FocusLost();
                const std::string typed = m::Trim(Text(IDC_ARV_CUSTOMER));
                if (!typed.empty() && typed != picked_) PickCustomer(typed);
            }
        }
        return EntryPage::OnFormCommand(id, code, ctl);
    }

private:
    std::wstring Named() const { return model_.isNew() ? L"the new sales voucher" : L"sales voucher " + std::to_wstring(model_.number); }

    void PickCustomer(const std::string& no)
    {
        ReadHeader();
        if (!model_.SelectCustomer(no)) {
            MarkField(IDC_ARV_CUSTOMER, "There is no customer " + no + ": add them in Sales Customers.");
            return;
        }
        picked_ = model_.customer;
        ClearMark(IDC_ARV_CUSTOMER);
        Loading l(*this);
        SetText(IDC_ARV_CUSTOMER, model_.customer);
        ShowCustomerName();
        SetText(IDC_ARV_VIN, model_.vin);
        SetInt(IDC_ARV_TERMS, model_.terms);
        SetInt(IDC_ARV_VATRATE, model_.vatRate);
        SetDate(IDC_ARV_DUEDATE, model_.dueDate);
        SetText(IDC_ARV_AR, model_.ar.account);
        SetText(IDC_ARV_ARDEPT, model_.ar.dept);
        SetText(IDC_ARV_VAT, model_.vat.account);
        SetText(IDC_ARV_VATDEPT, model_.vat.dept);
        ShowNames();
        UpdateTotals();
        SetDirty(true);
    }

    void ReadHeader()
    {
        model_.customer = m::Trim(Text(IDC_ARV_CUSTOMER));
        model_.invoiceNo = m::Trim(Text(IDC_ARV_INVOICE));
        model_.orderNo = m::Trim(Text(IDC_ARV_ORDER));
        model_.invoiceDate = Date(IDC_ARV_INVDATE);
        model_.dueDate = Date(IDC_ARV_DUEDATE);
        model_.period = periodOf(Item(IDC_ARV_PERIOD));
        model_.vin = m::Trim(Text(IDC_ARV_VIN));
        model_.reference = m::Trim(Text(IDC_ARV_REF));
        model_.terms = Int(IDC_ARV_TERMS);
        model_.vatRate = Int(IDC_ARV_VATRATE);
        model_.ar = {m::Trim(Text(IDC_ARV_AR)), m::Trim(Text(IDC_ARV_ARDEPT))};
        model_.vat = {m::Trim(Text(IDC_ARV_VAT)), m::Trim(Text(IDC_ARV_VATDEPT))};
        model_.memo = Text(IDC_ARV_MEMO);
    }

    void Delete()
    {
        if (model_.isNew()) return;
        if (!AskYes(L"Delete sales voucher " + std::to_wstring(model_.number) + L"?", L"It can't be undone.", L"Delete voucher")) return;
        const EntryResult r = model_.Delete();
        Fill();
        ShowResult(r);
    }

    void Post()
    {
        if (IsDirty() && !Save()) return;
        if (!model_.CanPost()) return;
        if (!AskYes(L"Post sales voucher " + std::to_wstring(model_.number) + L"?", L"", L"Post voucher")) return;
        const EntryResult r = model_.Post();
        Fill();
        ShowResult(r);
    }

    ArInvoiceModel model_;
    fx::Lookup customers_;
    std::string picked_;   // the customer whose details are shown
};

// ---- Clear Receipts ------------------------------------------------------------------------

class ReceiptsPage : public fx::Page {
public:
    ReceiptsPage() : fx::Page(IDD_PG_RECEIPTS), model_(cb) { commandId = 4054; }

    void SelectFirst() { Select(0); }
    void Filter(const std::string& text) { SetText(IDC_RCP_FILTER, text); }

protected:
    bool OnInitPage() override
    {
        SetAnchors(kReceiptsAnchors);
        list_ = Item(IDC_RCP_LIST);
        fx::SetAccessibleName(list_, L"Open invoices");
        addColumns(list_, {{L"Customer", 64}, {L"Name", 120}, {L"Invoice", 70}, {L"Due", 72}, {L"Owed", 90}}, 4);
        SendDlgItemMessageW(form(), IDC_RCP_FILTER, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Filter by customer or invoice"));
        model_.Load(false);
        HWND bank = Item(IDC_RCP_BANK);
        for (const ApBank& b : model_.banks()) {
            const std::wstring text = wide(TrimText(b.shortName) + "  " + TrimText(b.name));
            SendMessageW(bank, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        }
        SendMessageW(bank, CB_SETCURSEL, 0, 0);
        fx::SetDateField(Item(IDC_RCP_DATE), today());
        ShowList();
        ShowSelected();
        return true;
    }

    bool OnFormCommand(WORD id, WORD code, HWND) override
    {
        if (id == IDC_RCP_FILTER) {
            if (code == EN_CHANGE) ShowList();
            return code == EN_CHANGE || code == EN_KILLFOCUS;
        }
        if (code == BN_CLICKED && id == IDC_RCP_ALL) {
            model_.Load(Checked(IDC_RCP_ALL));
            ShowList();
            return true;
        }
        if (code == BN_CLICKED && id == IDC_RCP_RECORD) {
            Record();
            return true;
        }
        if (code == BN_CLICKED && id == IDC_RCP_REVERSE) {
            Reverse();
            return true;
        }
        if (code == EN_CHANGE) return true;   // the receipt's fields aren't a record to save: never dirty
        return false;
    }

    bool OnFormNotify(NMHDR* nm, LRESULT& result) override
    {
        if (nm->hwndFrom != list_) return false;
        switch (nm->code) {
        case LVN_GETDISPINFOW: {
            auto* di = reinterpret_cast<NMLVDISPINFOW*>(nm);
            const int row = di->item.iItem;
            if (!(di->item.mask & LVIF_TEXT) || row < 0 || static_cast<size_t>(row) >= shown_.size()) return true;
            const ReceiptItem& r = model_.items()[shown_[static_cast<size_t>(row)]];
            std::string text;
            switch (di->item.iSubItem) {
            case 0: text = TrimText(r.item.customer); break;
            case 1: text = r.customerName; break;
            case 2: text = TrimText(r.item.invoiceNo); break;
            case 3:
                text = r.item.dueDate.size() == 8 ? r.item.dueDate.substr(4, 2) + "/" + r.item.dueDate.substr(6, 2) + "/" +
                                                        r.item.dueDate.substr(0, 4)
                                                  : std::string();
                break;
            default: text = money(r.item.remainder); break;
            }
            wcsncpy_s(di->item.pszText, static_cast<size_t>(di->item.cchTextMax), wide(text).c_str(), _TRUNCATE);
            result = 0;
            return true;
        }
        case LVN_ITEMCHANGED: {
            const auto* lv = reinterpret_cast<NMLISTVIEW*>(nm);
            if (!selecting_ && (lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED)) Select(lv->iItem);
            result = 0;
            return true;
        }
        default: return false;
        }
    }

    bool OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override
    {
        if ((shownNow(msg, wp, lp) || (msg == kDataChanged && (wp & data_group::kAr))) && list_) {   // back to the tab, or the books changed
            model_.Load(Checked(IDC_RCP_ALL));
            ShowList();
            ShowSelected();
        }
        return fx::Page::OnMessage(msg, wp, lp, result);
    }

private:
    void ShowList()
    {
        shown_ = model_.Filter(Text(IDC_RCP_FILTER));
        selecting_ = true;
        ListView_SetItemCountEx(list_, static_cast<int>(shown_.size()), LVSICF_NOSCROLL);
        ListView_SetItemState(list_, -1, 0, LVIS_SELECTED);
        if (const ReceiptItem* s = model_.selected())
            for (size_t i = 0; i < shown_.size(); ++i)
                if (model_.items()[shown_[i]].item.batchno == s->item.batchno)
                    ListView_SetItemState(list_, static_cast<int>(i), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        InvalidateRect(list_, nullptr, FALSE);
        selecting_ = false;
    }

    void Select(int row)
    {
        if (row < 0 || static_cast<size_t>(row) >= shown_.size()) return;
        model_.Select(model_.items()[shown_[static_cast<size_t>(row)]].item.batchno);
        HideInfo();
        ShowSelected();
    }

    void ShowSelected()
    {
        const ReceiptItem* s = model_.selected();
        SetText(IDC_RCP_WHO, s ? TrimText(s->item.customer) + "  " + s->customerName : std::string("Choose an invoice in the list."));
        SetText(IDC_RCP_WHAT, s ? "Invoice " + TrimText(s->item.invoiceNo) + " (sales voucher " + std::to_string(s->item.batchno) +
                                      "): " + money(s->item.amount) + ", owed " + money(s->item.remainder) + "."
                                : std::string());
        SetText(IDC_RCP_AMOUNT, s && s->item.remainder ? m::FormatAmount(s->item.remainder) : std::string());
        SetChecked(IDC_RCP_DISCOUNT, false);
        EnableWindow(Item(IDC_RCP_RECORD), s && s->item.remainder > 0);
        EnableWindow(Item(IDC_RCP_REVERSE), model_.HasReceipt());
    }

    // G9: the latest receipt undone, after asking (D10).
    void Reverse()
    {
        const ReceiptItem* s = model_.selected();
        if (!s || !model_.HasReceipt()) return;
        enum { kReverse = 101 };
        if (fx::Ask(host() ? host()->hostWindow() : hwnd(), szApp,
                    L"Reverse the latest receipt on invoice " + wide(TrimText(s->item.invoiceNo)) + L"?",
                    L"What it received is owed again. If its journal has been transferred to the GL, it's reversed there on the date shown.",
                    {{kReverse, L"Reverse receipt"}, {IDCANCEL, L"Keep it"}}, IDCANCEL, IDCANCEL) != kReverse)
            return;
        model_.date = fx::GetDateField(Item(IDC_RCP_DATE));
        const EntryResult r = model_.Reverse();
        if (!r.ok) {
            if (r.field == "date") MarkField(IDC_RCP_DATE, r.message, true);
            ShowInfo(fx::InfoKind::Error, wide(r.message));
            return;
        }
        ClearMarks();
        ShowList();
        ShowSelected();
        ShowInfo(fx::InfoKind::Success, wide(r.message));
        NotifyDataChanged(data_group::kAr, hwnd());
    }

    void Record()
    {
        model_.date = fx::GetDateField(Item(IDC_RCP_DATE));
        model_.amount = Text(IDC_RCP_AMOUNT);
        model_.discount = Checked(IDC_RCP_DISCOUNT);
        model_.reference = Text(IDC_RCP_REF);
        model_.bank = static_cast<int>(SendDlgItemMessageW(form(), IDC_RCP_BANK, CB_GETCURSEL, 0, 0));
        const EntryResult r = model_.Record();
        if (!r.ok) {
            static const std::map<std::string, int> ids = {{"date", IDC_RCP_DATE}, {"bank", IDC_RCP_BANK},
                                                           {"amount", IDC_RCP_AMOUNT}, {"discount", IDC_RCP_DISCOUNT}};
            const auto it = ids.find(r.field);
            if (it != ids.end()) MarkField(it->second, r.message, true);
            ShowInfo(fx::InfoKind::Error, wide(r.message));
            return;
        }
        ClearMarks();
        ShowList();
        ShowSelected();
        ShowInfo(fx::InfoKind::Success, wide(r.message));
        NotifyDataChanged(data_group::kAr, hwnd());   // spec #8 9.3
    }

    ReceiptsModel model_;
    HWND list_ = nullptr;
    std::vector<size_t> shown_;
    bool selecting_ = false;
};

// ---- the voucher list ---------------------------------------------------------------------------

class VoucherListPage : public fx::Page {
public:
    VoucherListPage() : fx::Page(IDD_PG_VOUCHERLIST) { commandId = CM_VOUCHER_LIST; }

    void Reload()
    {
        VoucherHeaders h(cb);
        std::vector<std::string> modes = {voucher_mode::kOpen, voucher_mode::kAdjustment};
        if (Checked(IDC_VL_POSTED)) modes.insert(modes.end(), {voucher_mode::kPosted, voucher_mode::kPostedAdjustment});
        all_ = ListVouchers(h, modes);
        h.close();
        ShowList();
    }

protected:
    bool OnInitPage() override
    {
        SetAnchors(kListAnchors);
        list_ = Item(IDC_VL_LIST);
        fx::SetAccessibleName(list_, L"Vouchers");
        addColumns(list_, {{L"Voucher", 64}, {L"Date", 80}, {L"Period", 50}, {L"Description", 260}, {L"Debits", 100},
                           {L"Credits", 100}, {L"State", 140}},
                   4);
        // State is left-aligned
        LVCOLUMNW c{};
        c.mask = LVCF_FMT;
        c.fmt = LVCFMT_LEFT;
        ListView_SetColumn(list_, 6, &c);
        SendDlgItemMessageW(form(), IDC_VL_FILTER, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Filter by number or description"));
        Reload();
        return true;
    }

    bool OnFormCommand(WORD id, WORD code, HWND) override
    {
        if (id == IDC_VL_FILTER) {
            if (code == EN_CHANGE) ShowList();
            return code == EN_CHANGE || code == EN_KILLFOCUS;
        }
        if (code != BN_CLICKED) return false;
        switch (id) {
        case IDC_VL_POSTED: Reload(); return true;
        case IDC_VL_OPEN: OpenSelected(); return true;
        case IDC_VL_NEW:
            if (host()) OpenGlVoucherPage(*host(), false, 0);
            return true;
        default: return false;
        }
    }

    bool OnFormNotify(NMHDR* nm, LRESULT& result) override
    {
        if (nm->hwndFrom != list_) return false;
        if (nm->code == NM_DBLCLK) {
            OpenSelected();
            result = 0;
            return true;
        }
        if (nm->code == LVN_GETDISPINFOW) {
            auto* di = reinterpret_cast<NMLVDISPINFOW*>(nm);
            const int row = di->item.iItem;
            if (!(di->item.mask & LVIF_TEXT) || row < 0 || static_cast<size_t>(row) >= shown_.size()) return true;
            const VoucherHeader& v = all_[shown_[static_cast<size_t>(row)]];
            std::string text;
            switch (di->item.iSubItem) {
            case 0: text = std::to_string(v.number); break;
            case 1: text = v.date.size() == 8 ? v.date.substr(4, 2) + "/" + v.date.substr(6, 2) + "/" + v.date.substr(0, 4) : v.date; break;
            case 2: text = std::to_string(v.period); break;
            case 3: text = TrimText(v.description); break;
            case 4: text = money(std::llround(v.debit * 100)); break;
            case 5: text = money(std::llround(v.credit * 100)); break;
            default:
                text = IsPostedMode(v.mode) ? "posted"
                       : v.mode == voucher_mode::kAdjustment ? (Balanced(v.debit, v.credit) ? "adjustment" : "adjustment, out of balance")
                       : Balanced(v.debit, v.credit) ? "open" : "open, out of balance";
                break;
            }
            wcsncpy_s(di->item.pszText, static_cast<size_t>(di->item.cchTextMax), wide(text).c_str(), _TRUNCATE);
            result = 0;
            return true;
        }
        return false;
    }

    bool OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override
    {
        if ((shownNow(msg, wp, lp) || (msg == kDataChanged && (wp & data_group::kGlVouchers))) && list_) Reload();   // the tab back, or a change
        return fx::Page::OnMessage(msg, wp, lp, result);
    }

private:
    void ShowList()
    {
        const std::string f = m::Trim(Text(IDC_VL_FILTER));
        std::string lf = f;
        std::transform(lf.begin(), lf.end(), lf.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        shown_.clear();
        for (size_t i = 0; i < all_.size(); ++i) {
            std::string d = TrimText(all_[i].description);
            std::transform(d.begin(), d.end(), d.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lf.empty() || std::to_string(all_[i].number).rfind(lf, 0) == 0 || d.find(lf) != std::string::npos) shown_.push_back(i);
        }
        ListView_SetItemCountEx(list_, static_cast<int>(shown_.size()), 0);
        InvalidateRect(list_, nullptr, FALSE);
    }

    void OpenSelected()
    {
        const int row = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        if (row < 0 || static_cast<size_t>(row) >= shown_.size() || !host()) return;
        const VoucherHeader& v = all_[shown_[static_cast<size_t>(row)]];
        if (IsPostedMode(v.mode)) {
            ShowInfo(fx::InfoKind::Information, L"Voucher " + std::to_wstring(v.number) + L" has been posted: it can't be changed.");
            return;
        }
        OpenGlVoucherPage(*host(), v.mode == voucher_mode::kAdjustment, v.number);
    }

    HWND list_ = nullptr;
    std::vector<VoucherHeader> all_;
    std::vector<size_t> shown_;
};

// ---- opening -----------------------------------------------------------------------------------------

template <class P>
class Owned : public P {
public:
    using P::P;

protected:
    void OnFinalMessage() override { delete this; }
};

std::map<WORD, HWND> gSingle;   // command -> its page (the pages there is one of)

template <class P, class... A>
HWND openSingle(fx::PageHost& host, WORD command, const wchar_t* title, A... args)
{
    const auto it = gSingle.find(command);
    if (it != gSingle.end() && IsWindow(it->second)) {
        host.ActivatePage(it->second);
        return it->second;
    }
    auto* page = new Owned<P>(args...);
    HWND h = page->Open(host, title);
    if (!h) {
        delete page;
        return nullptr;
    }
    gSingle[command] = h;
    return h;
}

struct VoucherCommand {
    WORD command, classic;
    const wchar_t* classicText;
};

const std::vector<VoucherCommand>& commands()
{
    static const std::vector<VoucherCommand> all = {
        {10076, CM_GLVOUCHER_CLASSIC, L"General Ledger Voucher (classic)"},
        {8081, CM_PRIOR_CLASSIC, L"Prior Period Entry (classic)"},
        {4044, CM_APINVOICE_CLASSIC, L"Purchase Invoice Vouchers (classic)"},
        {1079, CM_APCASH_CLASSIC, L"Cash Invoice Vouchers (classic)"},
        {5045, CM_ARINVOICE_CLASSIC, L"Sales Invoice Vouchers (classic)"},
        {4054, CM_RECEIPTS_CLASSIC, L"Clear Receipts (classic)"},
    };
    return all;
}

} // namespace

HWND OpenGlVoucherPage(fx::PageHost& host, bool adjustment, long voucher)
{
    for (HWND h : gGlPages) {
        auto* p = dynamic_cast<GlVoucherPage*>(fx::Window::FromHandle(h));
        if (p && p->adjustment() == adjustment && p->number() == voucher && (voucher != 0 || !p->IsDirty())) {
            host.ActivatePage(h);
            return h;
        }
    }
    auto* page = new Owned<GlVoucherPage>(adjustment, voucher);
    HWND h = page->Open(host, adjustment ? L"Adjustment" : L"GL Voucher");
    if (!h) {
        delete page;
        return nullptr;
    }
    gGlPages.push_back(h);
    page->Retitle();
    return h;
}

bool IsVoucherPageCommand(WORD command)
{
    return command == 10076 || command == 1007 || command == 8081 || command == CM_VOUCHER_LIST || command == 4044 ||
           command == 1079 || command == 5045 || command == 4054;
}

HWND OpenVoucherPage(fx::PageHost& host, WORD command)
{
    switch (command) {
    case 10076:
    case 1007: return OpenGlVoucherPage(host, false, 0);
    case 8081: return OpenGlVoucherPage(host, true, 0);
    case CM_VOUCHER_LIST: return openSingle<VoucherListPage>(host, command, L"Vouchers");
    case 4044: return openSingle<ApVoucherPage>(host, command, L"Purchase Vouchers", ApKind::Invoice);
    case 1079: return openSingle<ApVoucherPage>(host, command, L"Cash Vouchers", ApKind::Cash);
    case 5045: return openSingle<ArInvoicePage>(host, command, L"Sales Vouchers");
    case 4054: return openSingle<ReceiptsPage>(host, command, L"Receipts");
    default: return nullptr;
    }
}

const std::vector<ClassicCommand>& VoucherClassicCommands()
{
    static const std::vector<ClassicCommand> all = [] {
        std::vector<ClassicCommand> v;
        for (const VoucherCommand& c : commands()) v.push_back({c.command, c.classic, c.classicText});
        return v;
    }();
    return all;
}

std::vector<VoucherPageForm> VoucherPageForms()
{
    return {{IDD_PG_GLVOUCHER, L"GL voucher page", &kGlAnchors},
            {IDD_PG_APVOUCHER, L"AP voucher page", &kApAnchors},
            {IDD_PG_ARINVOICE, L"Sales voucher page", &kArAnchors},
            {IDD_PG_RECEIPTS, L"Receipts page", &kReceiptsAnchors},
            {IDD_PG_VOUCHERLIST, L"Voucher list page", &kListAnchors}};
}

int StressVoucherPages(int times)
{
    class StressWorkspace : public fx::Workspace {
    protected:
        bool OnDestroy() override { return true; }   // not the main window
    };
    auto objects = [] {
        HANDLE self = GetCurrentProcess();
        return static_cast<int>(GetGuiResources(self, GR_GDIOBJECTS) + GetGuiResources(self, GR_USEROBJECTS));
    };
    StressWorkspace w;
    if (!w.CreateWorkspace(hInst, L"GLVoucherPageStress", L"page stress", nullptr, -1)) return -1;
    auto once = [&] {
        for (WORD c : {WORD(10076), WORD(8081), WORD(4044), WORD(1079), WORD(5045), WORD(4054), WORD(CM_VOUCHER_LIST)}) {
            HWND h = OpenVoucherPage(w, c);
            auto* p = dynamic_cast<fx::Page*>(fx::Window::FromHandle(h));
            if (!p) return false;
            if (auto* e = dynamic_cast<EntryPage*>(p)) {
                e->TypeFiveLines();
                e->Discard();   // closed with no save
            } else if (auto* r = dynamic_cast<ReceiptsPage*>(p)) {
                r->Filter("C");
                r->SelectFirst();
                r->Filter("");
            }
            w.ClosePage(h);
            if (IsWindow(h)) return false;
        }
        return true;
    };
    if (!once()) {   // first open: class registration, the theme
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
