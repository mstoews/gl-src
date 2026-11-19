//
// ReportOptions.cpp - see ReportOptions.h.
//
// Translation notes (spec #8 G1): the three options dialogs of spec #3
// (IDD_SAMPLE_OPTIONS, IDD_TB_OPTIONS, IDD_STMT_OPTIONS) became child panels
// with the same controls and IDs, laid out in a row; their OK became Read()
// and their messages are the same. F1 hides the To period and the voucher
// lines and moves its check box up to their place; F4 and F5 hide the
// department and the comparisons, as before.
//
#include "ReportOptions.h"

#include "../Globals.h"
#include "../PortIds.h"

#include <algorithm>
#include <exception>
#include <string>
#include <vector>

// ---- PanelDialog ------------------------------------------------------------------------

HWND PanelDialog::Create(HWND parent)
{
    HWND h = CreateModeless(hInst, TemplateId(), parent);
    if (h) fx::AddModeless(h);
    return h;
}

bool PanelDialog::OnCommand(WORD id, WORD, HWND)
{
    if (id != IDOK && id != IDCANCEL) return false;
    if (id == IDOK) SendMessageW(GetParent(hwnd()), WM_COMMAND, IDOK, 0);   // the preview refreshes
    return true;                                                              // Esc: nothing to close
}

bool PanelDialog::OnDestroy()
{
    fx::RemoveModeless(hwnd());
    return false;
}

void PanelDialog::Refuse(const wchar_t* text, int id) const
{
    MessageBoxW(GetAncestor(hwnd(), GA_ROOT), text, szApp, MB_OK | MB_ICONEXCLAMATION);
    HWND ctl = GetDlgItem(hwnd(), id);
    SetFocus(ctl);
    SendMessageW(ctl, EM_SETSEL, 0, -1);
}

namespace {

// Fills a period combo with the 12 periods ("7  Oct 26") and selects `select`.
void fillPeriods(HWND combo, const ReportContext& ctx, int select)
{
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (int p = 1; p <= 12; ++p) {
        std::wstring text = std::to_wstring(p);
        if (const FiscalPeriod* fp = ctx.period(p)) {
            if (!fp->description.empty()) text += L"   " + fx::Utf8ToWide(fp->description);
            if (p == ctx.currentPeriod && ctx.periodOpen) text += L"   (current)";
        }
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(std::clamp(select, 1, 12) - 1), 0);
}

int selectedPeriod(HWND combo)
{
    const LRESULT i = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    return i == CB_ERR ? 0 : static_cast<int>(i) + 1;
}

bool checked(HWND dlg, int id) { return IsDlgButtonChecked(dlg, id) == BST_CHECKED; }
void check(HWND dlg, int id, bool on) { CheckDlgButton(dlg, id, on ? BST_CHECKED : BST_UNCHECKED); }

// Moves control `id` to where control `to` is (the same row, its left edge).
void moveTo(HWND dlg, int id, int to)
{
    RECT a{}, b{};
    GetWindowRect(GetDlgItem(dlg, to), &b);
    GetWindowRect(GetDlgItem(dlg, id), &a);
    MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&a), 2);
    MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&b), 2);
    SetWindowPos(GetDlgItem(dlg, id), nullptr, b.left, a.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Moves control `id` to just after control `after` on its row, `gap` dialog units on.
void moveAfter(HWND dlg, int id, int after, int gap)
{
    RECT a{}, b{}, g{0, 0, gap, 0};
    MapDialogRect(dlg, &g);
    GetWindowRect(GetDlgItem(dlg, after), &b);
    GetWindowRect(GetDlgItem(dlg, id), &a);
    MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&a), 2);
    MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&b), 2);
    SetWindowPos(GetDlgItem(dlg, id), nullptr, b.right + g.right, a.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// IDD_PNL_TB for F1 (dist == nullptr) or F2.
class TrialBalancePanel : public PanelDialog {
public:
    TrialBalancePanel(const ReportContext& ctx, TrialBalanceOptions* tb, DistTrialBalanceOptions* dist)
        : ctx_(ctx), tb_(tb), dist_(dist) {}
    UINT TemplateId() const override { return IDD_PNL_TB; }

    bool Read() override
    {
        const int from = selectedPeriod(GetDlgItem(hwnd(), IDC_TB_FROM));
        const bool zeros = checked(hwnd(), IDC_TB_ZEROS);
        if (tb_) {
            tb_->period = from;
            tb_->includeZeros = zeros;
            return true;
        }
        const int to = selectedPeriod(GetDlgItem(hwnd(), IDC_TB_TO));
        if (to < from) {
            Refuse(L"The To period comes before the From period.", IDC_TB_TO);
            return false;
        }
        dist_->from = from;
        dist_->to = to;
        dist_->voucherLines = checked(hwnd(), IDC_TB_LINES);
        dist_->includeInactive = zeros;
        return true;
    }

protected:
    bool OnInitDialog() override
    {
        const int current = ctx_.currentPeriod;
        if (tb_) {
            SetDlgItemTextW(hwnd(), IDC_TB_FROM_LABEL, L"&Period:");
            SetDlgItemTextW(hwnd(), IDC_TB_ZEROS, L"&Include accounts with a zero balance");
            moveTo(hwnd(), IDC_TB_ZEROS, IDC_TB_TO_LABEL);
            for (int id : {IDC_TB_TO_LABEL, IDC_TB_TO, IDC_TB_LINES}) ShowWindow(GetDlgItem(hwnd(), id), SW_HIDE);
            fillPeriods(GetDlgItem(hwnd(), IDC_TB_FROM), ctx_, tb_->period > 0 ? tb_->period : current);
            check(hwnd(), IDC_TB_ZEROS, tb_->includeZeros);
        } else {
            fillPeriods(GetDlgItem(hwnd(), IDC_TB_FROM), ctx_, dist_->from > 0 ? dist_->from : current);
            fillPeriods(GetDlgItem(hwnd(), IDC_TB_TO), ctx_, dist_->to > 0 ? dist_->to : current);
            check(hwnd(), IDC_TB_LINES, dist_->voucherLines);
            check(hwnd(), IDC_TB_ZEROS, dist_->includeInactive);
        }
        return false;   // the focus stays where it is (the page)
    }

private:
    ReportContext ctx_;
    TrialBalanceOptions* tb_;
    DistTrialBalanceOptions* dist_;
};

// IDD_PNL_STMT for F3 (with department and comparisons), F4 and F5.
class StatementPanel : public PanelDialog {
public:
    StatementPanel(const ReportContext& ctx, bool income, StatementOptions& opt) : ctx_(ctx), income_(income), opt_(opt) {}
    UINT TemplateId() const override { return IDD_PNL_STMT; }

    bool Read() override
    {
        opt_.period = selectedPeriod(GetDlgItem(hwnd(), IDC_ST_PERIOD));
        if (income_) {
            const LRESULT d = SendDlgItemMessageW(hwnd(), IDC_ST_DEPT, CB_GETCURSEL, 0, 0);
            opt_.dept = d > 0 && static_cast<size_t>(d) < depts_.size() ? depts_[static_cast<size_t>(d)] : std::string();
            opt_.comparePrior = checked(hwnd(), IDC_ST_PRIOR);
            opt_.compareBudget = checked(hwnd(), IDC_ST_BUDGET);
        }
        opt_.showAccounts = checked(hwnd(), IDC_ST_ACCOUNTS);
        opt_.showZeroLines = checked(hwnd(), IDC_ST_ZEROS);
        return true;
    }

protected:
    bool OnInitDialog() override
    {
        fillPeriods(GetDlgItem(hwnd(), IDC_ST_PERIOD), ctx_, opt_.period > 0 ? opt_.period : ctx_.currentPeriod);
        if (income_) {
            HWND dept = GetDlgItem(hwnd(), IDC_ST_DEPT);
            SendMessageW(dept, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All departments"));
            depts_.push_back({});
            try {
                cb4sqlite::Query q(cb, "SELECT depart, descript FROM gldept WHERE _deleted = 0 ORDER BY depart");
                while (q.next()) {
                    depts_.push_back(q.text(0));
                    const std::wstring text = fx::Utf8ToWide(q.text(0) + "   " + q.text(1));
                    SendMessageW(dept, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
                }
            } catch (const std::exception&) {
                // No gldept: all departments only.
            }
            const auto it = std::find(depts_.begin(), depts_.end(), opt_.dept);
            SendMessageW(dept, CB_SETCURSEL, it == depts_.end() ? 0 : static_cast<WPARAM>(it - depts_.begin()), 0);
            check(hwnd(), IDC_ST_PRIOR, opt_.comparePrior);
            check(hwnd(), IDC_ST_BUDGET, opt_.compareBudget);
        } else {
            moveTo(hwnd(), IDC_ST_ACCOUNTS, IDC_ST_DEPT_LABEL);
            moveAfter(hwnd(), IDC_ST_ZEROS, IDC_ST_ACCOUNTS, 4);
            for (int id : {IDC_ST_DEPT_LABEL, IDC_ST_DEPT, IDC_ST_PRIOR, IDC_ST_BUDGET}) ShowWindow(GetDlgItem(hwnd(), id), SW_HIDE);
        }
        check(hwnd(), IDC_ST_ACCOUNTS, opt_.showAccounts);
        check(hwnd(), IDC_ST_ZEROS, opt_.showZeroLines);
        return false;
    }

private:
    ReportContext ctx_;
    bool income_;
    StatementOptions& opt_;
    std::vector<std::string> depts_;   // index 0: all departments
};

class SamplePanel : public PanelDialog {
public:
    explicit SamplePanel(SampleOptions& opt) : opt_(opt) {}
    UINT TemplateId() const override { return IDD_PNL_SAMPLE; }

    bool Read() override
    {
        BOOL ok = FALSE;
        const UINT repeat = GetDlgItemInt(hwnd(), IDC_SAMPLE_REPEAT, &ok, FALSE);
        if (!ok || repeat < 1 || repeat > 50) {
            Refuse(L"Enter a number from 1 to 50.", IDC_SAMPLE_REPEAT);
            return false;
        }
        opt_.includeZeros = checked(hwnd(), IDC_SAMPLE_ZEROS);
        opt_.landscape = checked(hwnd(), IDC_SAMPLE_LANDSCAPE);
        opt_.repeat = static_cast<int>(repeat);
        return true;
    }

protected:
    bool OnInitDialog() override
    {
        check(hwnd(), IDC_SAMPLE_ZEROS, opt_.includeZeros);
        check(hwnd(), IDC_SAMPLE_LANDSCAPE, opt_.landscape);
        SendDlgItemMessageW(hwnd(), IDC_SAMPLE_REPEAT, EM_LIMITTEXT, 2, 0);
        SetDlgItemInt(hwnd(), IDC_SAMPLE_REPEAT, static_cast<UINT>(opt_.repeat), FALSE);
        return false;
    }

private:
    SampleOptions& opt_;
};

} // namespace

std::unique_ptr<ReportPanel> MakeSamplePanel(SampleOptions& opt) { return std::make_unique<SamplePanel>(opt); }

std::unique_ptr<ReportPanel> MakeTrialBalancePanel(const ReportContext& ctx, TrialBalanceOptions& opt)
{
    return std::make_unique<TrialBalancePanel>(ctx, &opt, nullptr);
}

std::unique_ptr<ReportPanel> MakeDistTrialBalancePanel(const ReportContext& ctx, DistTrialBalanceOptions& opt)
{
    return std::make_unique<TrialBalancePanel>(ctx, nullptr, &opt);
}

std::unique_ptr<ReportPanel> MakeStatementPanel(const ReportContext& ctx, bool incomeStatement, StatementOptions& opt)
{
    return std::make_unique<StatementPanel>(ctx, incomeStatement, opt);
}
