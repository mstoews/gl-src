//
// ReportOptions.h - the reports' options, as panels above their previews
// (spec #3 3, 5.6; spec #8 G1). Each panel shows its options starting from
// `opt`; Read() stores what it shows in `opt`, or says what's wrong (on the
// field) and returns false. Their templates (IDD_PNL_*) are in gl/res/port.rc.
//
// Until Spec #8 these were modal dialogs before the report and again from the
// preview's Options... button. Now the report is built at once from the last
// options (the session's), and the panel's Refresh (or Enter in it) builds it
// again.
//
// The period lists show the context's fiscal periods ("7  Oct 26"); a
// period of 0 in `opt` starts at the current period.
//
#pragma once

#include "ReportContext.h"
#include "SampleReport.h"
#include "Statements.h"
#include "TrialBalances.h"

#include "win32fx.h"

#include <memory>

// A panel: a child dialog (DS_CONTROL) made over the preview. Create makes
// the window (registered for the dialog keys); Read is described above.
class ReportPanel {
public:
    virtual ~ReportPanel() = default;
    virtual HWND Create(HWND parent) = 0;
    virtual bool Read() = 0;   // false: refused, the reason shown and the focus on the field
};

// The options panels' common part.
class PanelDialog : public fx::Dialog, public ReportPanel {
public:
    HWND Create(HWND parent) override;

protected:
    virtual UINT TemplateId() const = 0;
    bool OnCommand(WORD id, WORD code, HWND ctl) override;   // IDOK (Enter): Refresh
    bool OnDestroy() override;
    // MessageBoxW over the panel, with the program's caption; the focus then goes to `id`.
    void Refuse(const wchar_t* text, int id) const;
};

// IDD_PNL_SAMPLE.
std::unique_ptr<ReportPanel> MakeSamplePanel(SampleOptions& opt);

// IDD_PNL_TB, as F1 Trial Balance (period; zero balances) and F2
// Distribution Trial Balance (from / to period; voucher lines; inactive accounts).
std::unique_ptr<ReportPanel> MakeTrialBalancePanel(const ReportContext& ctx, TrialBalanceOptions& opt);
std::unique_ptr<ReportPanel> MakeDistTrialBalancePanel(const ReportContext& ctx, DistTrialBalanceOptions& opt);

// IDD_PNL_STMT. `incomeStatement` shows the department (from gldept) and the
// comparisons, which only F3 has.
std::unique_ptr<ReportPanel> MakeStatementPanel(const ReportContext& ctx, bool incomeStatement, StatementOptions& opt);
