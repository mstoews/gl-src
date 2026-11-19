//
// RunReport.h - running a report: the port's PrintReport (spec #3 2.1, 4;
// plan S3-M1 task 5).
//
// The original's PrintReport(name, hwnd) (gl/src/ap_repts.cpp:112) opened a
// Crystal job, set the company name and current period, and showed Crystal's
// preview. RunReport does the same steps with the native engine:
//
//   1. the report's options dialog, if it has one (Cancel stops here);
//   2. a wait cursor; the ReportContext (company, current period, periods);
//   3. the builder, whose exceptions are shown as the other screens show them;
//   4. a preview (report/win32/Preview.h), whose Options... repeats 1-3.
//
// Spec #8 G1: the reports with options (the sample, F1 to F5, a user report
// that asks) run through RunReportWithPanel instead: steps 2-4 at once with
// the session's last options, and the options as a panel above the preview,
// whose Refresh reads them and repeats 2-3.
//
// A builder that leaves the report's company or "Printed on" line empty gets
// the context's.
//
#pragma once

#include "ReportContext.h"
#include "ReportOptions.h"   // ReportPanel

#include "report/Report.h"

#include "win32fx.h"

#include <functional>
#include <memory>
#include <optional>

using ReportOptionsFn = std::function<bool(HWND owner)>;   // false: cancelled; null: no options
using ReportBuilder = std::function<rpt::Report(const ReportContext&)>;

void RunReport(fx::PageHost& frame, ReportOptionsFn options, ReportBuilder build);

// G1: builds the report (or shows `first` when given: a user report not yet
// answered) and opens its preview with `panel` above it. The panel's Read and
// then `build` make each Refresh. Returns the preview, or null.
HWND RunReportWithPanel(fx::PageHost& frame, std::unique_ptr<ReportPanel> panel, ReportBuilder build,
                        std::optional<rpt::Report> first = std::nullopt);

// The context for the open company database, printed today.
ReportContext CurrentReportContext();

// The sample report (SampleReport.h): CM_SAMPLE_REPORT, gl.exe /samplereport.
void RunSampleReport(fx::PageHost& frame);

// F1 Trial Balance (6076) and F2 Distribution Trial Balance (1075), spec #3
// 5.2-5.3: their options dialog, then the preview. The options are kept for
// the session.
void RunTrialBalance(fx::PageHost& frame);
void RunDistributionTrialBalance(fx::PageHost& frame);

// F3 Income Statement (1015), F4 Balance Sheet (1016), F5 Cash Flow
// Statement (1018), spec #3 5.4, D13: options, then the preview; the options
// are kept for the session, per statement. The layout tables are created on
// the first run (StatementLayout.h).
void RunIncomeStatement(fx::PageHost& frame);
void RunBalanceSheet(fx::PageHost& frame);
void RunCashFlowStatement(fx::PageHost& frame);

// The original account reports (AccountReports.h): 6080 Trial Balance by
// Dept., 10071 Chart of Accounts Report (also the toolbar's inquiry button),
// 10073 Budget Report. No options, as the originals: straight to the preview.
void RunTrialBalanceByDept(fx::PageHost& frame);
void RunChartOfAccountsReport(fx::PageHost& frame);
void RunBudgetReport(fx::PageHost& frame);

// The original voucher reports (VoucherReports.h): 6074 Voucher Slips, 8082
// Voucher Detail, 6075 Voucher Summary, 6078 Preposting Distribution List,
// 8136 Pre-Posting Report. No options. (Voucher Entry's Print shows one
// slip itself: VoucherEntry.cpp.)
void RunVoucherSlips(fx::PageHost& frame);
void RunVoucherDetail(fx::PageHost& frame);
void RunVoucherSummary(fx::PageHost& frame);
void RunPrepostingDistribution(fx::PageHost& frame);
void RunPrePostingReport(fx::PageHost& frame);

// The master-data listings (MasterReports.h): 6039 Medical Representatives,
// 6038 Product code summary, 6041 Doctor Codes. No options.
void RunMedicalRepsListing(fx::PageHost& frame);
void RunProductCodesListing(fx::PageHost& frame);
void RunDoctorCodesListing(fx::PageHost& frame);

// The fixed-asset reports (AssetReports.h, spec #4 7): 1019 Fixed Asset
// Summary, with the check against the ledger, and 1020 Fixed Asset Types.
// No options.
void RunFixedAssetListing(fx::PageHost& frame);
void RunFixedAssetTypes(fx::PageHost& frame);

// The AP reports (ApReports.h, spec #5 7): runs the report of command `id`
// (1049 ... 8078, 4091 Cheques), straight to the preview, as the originals had
// no options. False if `id` isn't an AP report.
bool RunApReport(fx::PageHost& frame, WORD id);

// The AR reports (ArReports.h, spec #6 7): runs the report of command `id`
// (4056 ... 4076, CM_AR_STATEMENTS), straight to the preview. False if `id`
// isn't an AR report.
bool RunArReport(fx::PageHost& frame, WORD id);

// A user report (spec #7, ReportWriter.h): gl_rptdef's definition `number`,
// built by the report writer, then the preview. A definition that can't be
// read or built says why, and opens nothing.
void RunUserReport(fx::PageHost& frame, long number);

// /stress: opens each report in a preview `times` times (the sample report
// with three copies, F1, F2 with voucher lines, F3 with every column, F4,
// F5, then every original report), pages through it at every zoom, and closes it. Returns the GDI + USER
// object growth, or -1 if a preview couldn't be opened.
int StressReports(int times);
