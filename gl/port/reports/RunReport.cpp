//
// RunReport.cpp - see RunReport.h.
//
#include "RunReport.h"

#include "AccountReports.h"
#include "ApReports.h"
#include "ArReports.h"
#include "AssetReports.h"
#include "../Ap.h"       // EnsureApTables
#include "../Ar.h"       // EnsureArTables
#include "../ReportWriter.h"   // user reports (spec #7)
#include "../PortIds.h"  // CM_AR_STATEMENTS
#include "../Assets.h"   // EnsureAssetTables
#include "MasterReports.h"
#include "ReportOptions.h"
#include "ReportPrompt.h"
#include "SampleReport.h"
#include "Statements.h"
#include "TrialBalances.h"
#include "VoucherReports.h"

#include "../Globals.h"

#include "report/win32/Preview.h"

#include <cstdio>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

class WaitCursor {
public:
    WaitCursor() : old_(SetCursor(LoadCursorW(nullptr, IDC_WAIT))) {}
    ~WaitCursor() { SetCursor(old_); }
    WaitCursor(const WaitCursor&) = delete;
    WaitCursor& operator=(const WaitCursor&) = delete;

private:
    HCURSOR old_;
};

std::string today()
{
    SYSTEMTIME t{};
    GetLocalTime(&t);
    char buf[16] = {};
    std::snprintf(buf, sizeof buf, "%04u%02u%02u", t.wYear, t.wMonth, t.wDay);
    return buf;
}

// Steps 1-3 of RunReport.h; nullopt when cancelled or failed (the failure shown).
std::optional<rpt::Report> build(HWND owner, const ReportOptionsFn& options, const ReportBuilder& builder)
{
    if (options && !options(owner)) return std::nullopt;
    try {
        WaitCursor wait;
        const ReportContext ctx = CurrentReportContext();
        rpt::Report r = builder(ctx);
        if (r.company.empty()) r.company = ctx.company;
        if (r.printedOn.empty()) r.printedOn = ctx.printedOn;
        return r;
    } catch (const std::exception& e) {
        MessageBoxW(owner, (L"The report could not be prepared:\n\n" + fx::Utf8ToWide(e.what())).c_str(), szApp,
                    MB_OK | MB_ICONSTOP);
        return std::nullopt;
    }
}

// G1: a ReportPanel and its builder as the preview's options panel. The
// builder is declared first so the panel (which may refer to what the
// builder holds) goes first.
class PanelAdapter : public rpt::win32::OptionsPanel {
public:
    PanelAdapter(std::unique_ptr<ReportPanel> panel, ReportBuilder builder)
        : builder_(std::move(builder)), panel_(std::move(panel))
    {
    }
    HWND CreatePanel(HWND parent) override { return panel_->Create(parent); }
    std::optional<rpt::Report> Rebuild(HWND owner) override
    {
        if (!panel_->Read()) return std::nullopt;
        return build(owner, {}, builder_);
    }

private:
    ReportBuilder builder_;
    std::unique_ptr<ReportPanel> panel_;
};

// The sample report's options, kept for the session.
SampleOptions& sampleOptions()
{
    static SampleOptions opt;
    return opt;
}

// The options each report keeps for the session (spec #3 5.6).
TrialBalanceOptions& trialBalanceOptions()
{
    static TrialBalanceOptions opt;
    return opt;
}

DistTrialBalanceOptions& distTrialBalanceOptions()
{
    static DistTrialBalanceOptions opt;
    return opt;
}

// One set per statement (0 IS, 1 BS, 2 CF).
StatementOptions& statementOptions(int which)
{
    static StatementOptions opt[3];
    return opt[which];
}

using StatementBuilder = rpt::Report (*)(cb4sqlite::Code4&, const ReportContext&, const StatementOptions&);

void runStatement(fx::PageHost& frame, int which, StatementBuilder builder)
{
    RunReportWithPanel(frame, MakeStatementPanel(CurrentReportContext(), which == 0, statementOptions(which)),
                       [which, builder](const ReportContext& ctx) { return builder(cb, ctx, statementOptions(which)); });
}

// A hidden workspace for /stress: the previews open as its tabs, as in the
// program (spec #8 S8-M0). Not the main window: no WM_QUIT when destroyed.
class StressFrame : public fx::Workspace {
protected:
    bool OnDestroy() override { return true; }
};

int guiObjects()
{
    HANDLE self = GetCurrentProcess();
    return static_cast<int>(GetGuiResources(self, GR_GDIOBJECTS) + GetGuiResources(self, GR_USEROBJECTS));
}

// One preview: open, page through at each zoom (painting each page), close.
bool stressOnce(fx::PageHost& frame, const ReportBuilder& builder)
{
    HWND p = rpt::win32::OpenPreview(frame, builder(CurrentReportContext()));
    if (!p) return false;
    HWND view = FindWindowExW(p, nullptr, L"rptPageView", nullptr);
    for (int zoom = 0; zoom < 3; ++zoom) {
        for (int i = 0; i < rpt::win32::PreviewPageCount(p); ++i) {
            if (view) {
                InvalidateRect(view, nullptr, FALSE);
                UpdateWindow(view);
                SendMessageW(view, WM_KEYDOWN, VK_NEXT, 0);
            }
        }
        if (view) SendMessageW(view, WM_KEYDOWN, VK_HOME, 0);
        rpt::win32::CyclePreviewZoom(p);
    }
    frame.ClosePage(p);
    return !IsWindow(p);
}

} // namespace

HWND RunReportWithPanel(fx::PageHost& frame, std::unique_ptr<ReportPanel> panel, ReportBuilder builder,
                        std::optional<rpt::Report> first)
{
    std::optional<rpt::Report> r = first ? std::move(first) : build(frame.hostWindow(), {}, builder);
    if (!r) return nullptr;
    HWND p = rpt::win32::OpenPreview(frame, std::move(*r), std::make_unique<PanelAdapter>(std::move(panel), std::move(builder)));
    if (!p) MessageBoxW(frame.hostWindow(), L"The report preview could not be opened.", szApp, MB_OK | MB_ICONSTOP);
    return p;
}

void RunReport(fx::PageHost& frame, ReportOptionsFn options, ReportBuilder builder)
{
    std::optional<rpt::Report> r = build(frame.hostWindow(), options, builder);
    if (!r) return;
    rpt::win32::Rebuild rebuild;
    if (options)
        rebuild = [options, builder](HWND owner) { return build(owner, options, builder); };
    if (!rpt::win32::OpenPreview(frame, std::move(*r), std::move(rebuild)))
        MessageBoxW(frame.hostWindow(), L"The report preview could not be opened.", szApp, MB_OK | MB_ICONSTOP);
}

ReportContext CurrentReportContext()
{
    return LoadReportContext(cb, today());
}

void RunSampleReport(fx::PageHost& frame)
{
    RunReportWithPanel(frame, MakeSamplePanel(sampleOptions()),
                       [](const ReportContext& ctx) { return BuildSampleReport(cb, ctx, sampleOptions()); });
}

void RunTrialBalance(fx::PageHost& frame)
{
    RunReportWithPanel(frame, MakeTrialBalancePanel(CurrentReportContext(), trialBalanceOptions()),
                       [](const ReportContext& ctx) { return TrialBalance(cb, ctx, trialBalanceOptions()); });
}

void RunDistributionTrialBalance(fx::PageHost& frame)
{
    RunReportWithPanel(frame, MakeDistTrialBalancePanel(CurrentReportContext(), distTrialBalanceOptions()),
                       [](const ReportContext& ctx) { return DistributionTrialBalance(cb, ctx, distTrialBalanceOptions()); });
}

void RunIncomeStatement(fx::PageHost& frame) { runStatement(frame, 0, &IncomeStatement); }
void RunBalanceSheet(fx::PageHost& frame) { runStatement(frame, 1, &BalanceSheet); }
void RunCashFlowStatement(fx::PageHost& frame) { runStatement(frame, 2, &CashFlowStatement); }

void RunTrialBalanceByDept(fx::PageHost& frame)
{
    RunReport(frame, {}, [](const ReportContext& ctx) { return TrialBalanceByDept(cb, ctx); });
}

void RunChartOfAccountsReport(fx::PageHost& frame)
{
    RunReport(frame, {}, [](const ReportContext& ctx) { return ChartOfAccountsReport(cb, ctx); });
}

void RunBudgetReport(fx::PageHost& frame)
{
    RunReport(frame, {}, [](const ReportContext& ctx) { return BudgetReport(cb, ctx); });
}

void RunVoucherSlips(fx::PageHost& frame)
{
    RunReport(frame, {}, [](const ReportContext& ctx) { return VoucherSlips(cb, ctx); });
}

void RunVoucherDetail(fx::PageHost& frame)
{
    RunReport(frame, {}, [](const ReportContext& ctx) { return VoucherDetail(cb, ctx); });
}

void RunVoucherSummary(fx::PageHost& frame)
{
    RunReport(frame, {}, [](const ReportContext& ctx) { return VoucherSummary(cb, ctx); });
}

void RunPrepostingDistribution(fx::PageHost& frame)
{
    RunReport(frame, {}, [](const ReportContext& ctx) { return PrepostingDistribution(cb, ctx); });
}

void RunPrePostingReport(fx::PageHost& frame)
{
    RunReport(frame, {}, [](const ReportContext& ctx) { return PrePostingReport(cb, ctx); });
}

void RunMedicalRepsListing(fx::PageHost& frame)
{
    RunReport(frame, {}, [](const ReportContext& ctx) { return MedicalRepsListing(cb, ctx); });
}

void RunProductCodesListing(fx::PageHost& frame)
{
    RunReport(frame, {}, [](const ReportContext& ctx) { return ProductCodesListing(cb, ctx); });
}

void RunDoctorCodesListing(fx::PageHost& frame)
{
    RunReport(frame, {}, [](const ReportContext& ctx) { return DoctorCodesListing(cb, ctx); });
}

void RunFixedAssetListing(fx::PageHost& frame)
{
    EnsureAssetTables(cb);
    RunReport(frame, {}, [](const ReportContext& ctx) { return FixedAssetListing(cb, ctx); });
}

void RunFixedAssetTypes(fx::PageHost& frame)
{
    EnsureAssetTables(cb);
    RunReport(frame, {}, [](const ReportContext& ctx) { return FixedAssetTypesListing(cb, ctx); });
}

namespace {

using ApBuilder = rpt::Report (*)(cb4sqlite::Code4&, const ReportContext&);

struct ApReportEntry {
    WORD id;
    ApBuilder build;
};

// The AP reports by command (spec #5 7). 1063 (AP_AGEDC.RPT) was the same
// report as 1062, so it runs it.
const ApReportEntry kApReportTable[] = {
    {1078, ApInvoicePrePosting}, {1081, ApCashPrePosting},
    {8077, ApInvoiceVoucherDetail}, {8078, ApCashVoucherDetail},
    {1065, ApVoucherStatus}, {1049, ApPostingJournal},
    {1060, ApVendorList}, {1061, ApOpenPayables},
    {1062, ApAgedPayables}, {1063, ApAgedPayables}, {1064, ApCashRequirement},
    {1077, ApRegister}, {1058, ApPrepayments},
    {4091, ApCheques}, {4092, ApPaymentDetail}, {8073, ApCashChequeSummary},
    {8067, ApVatAp}, {8072, ApVatGl}, {8071, ApWithholding},
    {8068, ApJournalSummary}, {8069, ApJournalDetail},
    {8075, ApJournalCash}, {8076, ApJournalInvoices},
};

// The AR reports by command (spec #6 7). 4061 printed the aged report again
// and 4069 (ID_ARRETAINED, on no menu) the forecast: both run the forecast.
const ApReportEntry kArReportTable[] = {
    {4063, ArSalesPrePosting}, {4073, ArSalesVoucherDetail}, {4070, ArVoucherStatus},
    {4056, ArCustomerList}, {4058, ArCustomerSummary}, {4057, ArOpenReceivables},
    {4060, ArRegister}, {4068, ArAgedReceivables}, {4061, ArCollectionsForecast},
    {4069, ArCollectionsForecast}, {4076, ArReceiptsRegister}, {4071, ArVatSales},
    {4072, ArVatGl}, {4074, ArJournalSummary}, {4075, ArJournalDetail},
    {CM_AR_STATEMENTS, ArStatements},
};

}   // namespace

bool RunApReport(fx::PageHost& frame, WORD id)
{
    for (const ApReportEntry& r : kApReportTable)
        if (r.id == id) {
            EnsureApTables(cb);
            const ApBuilder build = r.build;
            RunReport(frame, {}, [build](const ReportContext& ctx) { return build(cb, ctx); });
            return true;
        }
    return false;
}

bool RunArReport(fx::PageHost& frame, WORD id)
{
    for (const ApReportEntry& r : kArReportTable)
        if (r.id == id) {
            EnsureArTables(cb);
            const ApBuilder build = r.build;
            RunReport(frame, {}, [build](const ReportContext& ctx) { return build(cb, ctx); });
            return true;
        }
    return false;
}

void RunUserReport(fx::PageHost& frame, long number)
{
    std::optional<rw::StoredDefinition> stored;
    rw::BuildResult b;
    std::string name;
    try {
        stored = rw::FindDefinition(cb, number);
        if (!stored) {
            MessageBoxW(frame.hostWindow(), L"The report is no longer on file.", szApp, MB_OK | MB_ICONINFORMATION);
            return;
        }
        name = stored->name;
        const rw::ParseResult p = rw::ParseDefinition(stored->text);
        if (!p.definition) {
            MessageBoxW(frame.hostWindow(),
                        fx::Format(L"The report \u201C%s\u201D can't be read (line %d):\n\n%s",
                                   fx::Utf8ToWide(name).c_str(), p.line, fx::Utf8ToWide(p.error).c_str())
                            .c_str(),
                        szApp, MB_OK | MB_ICONEXCLAMATION);
            return;
        }
        bool asks = false;
        for (const rw::Filter& f : p.definition->filters) asks = asks || f.ask;
        if (asks) {
            // Asked when run (D6): the questions are the report's options, a
            // panel above the preview (G1). The answers last until exit; the
            // first time there are none, so the preview starts empty and
            // Refresh builds it.
            static std::map<long, rw::Answers> lastAnswers;
            auto def = std::make_shared<rw::ReportDefinition>(*p.definition);
            const bool answered = lastAnswers.count(number) != 0;
            rw::Answers& answers = lastAnswers[number];
            std::optional<rpt::Report> first;
            if (!answered) {
                rpt::Report empty;
                empty.title = name;
                empty.subtitles = {"Answer the report's questions above, then Refresh."};
                empty.company = CurrentReportContext().company;
                first = std::move(empty);
            }
            HWND preview = RunReportWithPanel(
                frame, MakeQuestionsPanel(*def, answers),
                [def, number](const ReportContext& ctx) {
                    rw::BuildResult r = rw::BuildReport(cb, ctx, *def, lastAnswers[number]);
                    if (!r.report) throw std::runtime_error(r.refusal);
                    return std::move(*r.report);
                },
                std::move(first));
            if (preview && !answered)
                if (HWND panel = rpt::win32::PreviewPanel(preview)) SetFocus(GetNextDlgTabItem(panel, nullptr, FALSE));
            return;
        }
        b = rw::BuildReport(cb, CurrentReportContext(), *p.definition);
    } catch (const std::exception& e) {
        MessageBoxW(frame.hostWindow(), (L"The report could not be prepared:\n\n" + fx::Utf8ToWide(e.what())).c_str(), szApp,
                    MB_OK | MB_ICONSTOP);
        return;
    }
    if (!b.report) {
        MessageBoxW(frame.hostWindow(),
                    fx::Format(L"\u201C%s\u201D:\n\n%s", fx::Utf8ToWide(name).c_str(), fx::Utf8ToWide(b.refusal).c_str())
                        .c_str(),
                    szApp, MB_OK | MB_ICONINFORMATION);
        return;
    }
    // Built once; the preview takes it as it is (it has no options to rebuild with).
    RunReport(frame, {}, [report = std::move(*b.report)](const ReportContext&) { return report; });
}

int StressReports(int times)
{
    StressFrame frame;
    if (!frame.CreateWorkspace(hInst, L"GLReportStress", L"Report stress", nullptr, -1)) return -1;
    SetWindowPos(frame.hostWindow(), nullptr, 0, 0, 900, 700, SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(frame.hostWindow(), SW_SHOWNOACTIVATE);   // hidden windows don't paint
    SampleOptions three;
    three.repeat = 3;
    DistTrialBalanceOptions lines;
    lines.from = 1;
    lines.voucherLines = true;
    StatementOptions everything;
    everything.comparePrior = everything.compareBudget = everything.showAccounts = true;
    // The sample report (several pages), F1, F2 for periods 1 to the current
    // one with voucher lines (landscape), F3 with every column and its
    // accounts (landscape), F4 and F5; then every original report.
    const ReportBuilder builders[] = {
        [three](const ReportContext& ctx) { return BuildSampleReport(cb, ctx, three); },
        [](const ReportContext& ctx) { return TrialBalance(cb, ctx, {}); },
        [lines](const ReportContext& ctx) { return DistributionTrialBalance(cb, ctx, lines); },
        [everything](const ReportContext& ctx) { return IncomeStatement(cb, ctx, everything); },
        [](const ReportContext& ctx) { return BalanceSheet(cb, ctx, {}); },
        [](const ReportContext& ctx) { return CashFlowStatement(cb, ctx, {}); },
        [](const ReportContext& ctx) { return TrialBalanceByDept(cb, ctx); },
        [](const ReportContext& ctx) { return ChartOfAccountsReport(cb, ctx); },
        [](const ReportContext& ctx) { return BudgetReport(cb, ctx); },
        [](const ReportContext& ctx) { return VoucherSlips(cb, ctx); },
        [](const ReportContext& ctx) { return VoucherDetail(cb, ctx); },
        [](const ReportContext& ctx) { return VoucherSummary(cb, ctx); },
        [](const ReportContext& ctx) { return PrepostingDistribution(cb, ctx); },
        [](const ReportContext& ctx) { return PrePostingReport(cb, ctx); },
        [](const ReportContext& ctx) { return MedicalRepsListing(cb, ctx); },
        [](const ReportContext& ctx) { return ProductCodesListing(cb, ctx); },
        [](const ReportContext& ctx) { return DoctorCodesListing(cb, ctx); },
        [](const ReportContext& ctx) { return FixedAssetListing(cb, ctx); },
        [](const ReportContext& ctx) { return FixedAssetTypesListing(cb, ctx); },
    };
    EnsureApTables(cb);
    EnsureArTables(cb);
    auto round = [&] {
        for (const ReportBuilder& b : builders)
            if (!stressOnce(frame, b)) return false;
        for (const ApReportEntry& r : kApReportTable) {
            const ApBuilder build = r.build;
            if (!stressOnce(frame, [build](const ReportContext& ctx) { return build(cb, ctx); })) return false;
        }
        for (const ApReportEntry& r : kArReportTable) {
            const ApBuilder build = r.build;
            if (!stressOnce(frame, [build](const ReportContext& ctx) { return build(cb, ctx); })) return false;
        }
        // G1: the options panels, each opened, refreshed and closed (the session's options).
        {
            const ReportContext ctx = CurrentReportContext();
            std::unique_ptr<ReportPanel> panels[] = {
                MakeSamplePanel(sampleOptions()),
                MakeTrialBalancePanel(ctx, trialBalanceOptions()),
                MakeDistTrialBalancePanel(ctx, distTrialBalanceOptions()),
                MakeStatementPanel(ctx, true, statementOptions(0)),
                MakeStatementPanel(ctx, false, statementOptions(1)),
            };
            const ReportBuilder panelBuilders[] = {
                [](const ReportContext& c) { return BuildSampleReport(cb, c, sampleOptions()); },
                [](const ReportContext& c) { return TrialBalance(cb, c, trialBalanceOptions()); },
                [](const ReportContext& c) { return DistributionTrialBalance(cb, c, distTrialBalanceOptions()); },
                [](const ReportContext& c) { return IncomeStatement(cb, c, statementOptions(0)); },
                [](const ReportContext& c) { return BalanceSheet(cb, c, statementOptions(1)); },
            };
            for (size_t i = 0; i < std::size(panels); ++i) {
                HWND pv = RunReportWithPanel(frame, std::move(panels[i]), panelBuilders[i]);
                if (!pv || !rpt::win32::PreviewPanel(pv) || !rpt::win32::RefreshPreview(pv)) return false;
                frame.ClosePage(pv);
                if (IsWindow(pv)) return false;
            }
        }
        // The user reports (spec #7) that build.
        for (const rw::StoredDefinition& d : rw::LoadDefinitions(cb)) {
            const rw::ParseResult p = rw::ParseDefinition(d.text);
            if (!p.definition) continue;
            const rw::ReportDefinition def = *p.definition;
            if (!rw::BuildReport(cb, CurrentReportContext(), def).report) continue;
            if (!stressOnce(frame, [def](const ReportContext& ctx) { return *rw::BuildReport(cb, ctx, def).report; }))
                return false;
        }
        return true;
    };
    int growth = -1;
    // Two warm-ups: the first registers the classes and creates cached GDI
    // objects, and one of those is released during the second, so measuring
    // after a single warm-up reads -1.
    if (round() && round()) {
        const int before = guiObjects();
        bool ok = true;
        for (int i = 0; i < times && ok; ++i) ok = round();
        if (ok) growth = guiObjects() - before;
    }
    DestroyWindow(frame.hostWindow());
    return growth;
}
