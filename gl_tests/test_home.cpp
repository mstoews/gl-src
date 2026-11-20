//
// gl_tests: the home page's figures, the report gallery and command search
// (spec #8 4, 5.4, 5.5; plan S8-M5 task 9). The figures are checked against
// the reports and lists that cover them, on the dev fixture.
//
#include "GlTestDb.h"

#include "Ap.h"
#include "Ar.h"
#include "Assets.h"
#include "PortIds.h"
#include "pages/HomeModels.h"
#include "pages/TaskModels.h"
#include "reports/ApReports.h"
#include "reports/ArReports.h"
#include "reports/ReportContext.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace gltest;

namespace {

std::int64_t totalAt(const rpt::Report& r, size_t col)
{
    for (auto it = r.rows.rbegin(); it != r.rows.rend(); ++it)
        if (it->kind == rpt::RowKind::Total) return col < it->cells.size() ? it->cells[col].value : -1;
    return -1;
}

int checkable(TaskModel& task, Code4& code)
{
    int n = 0;
    for (const TaskRow& r : task.Load(code)) n += r.selectable ? 1 : 0;
    return n;
}

} // namespace

TEST_CASE("Home figures agree with the reports and lists that cover them (S8-M5)", "[home]")
{
    FixtureDb db;
    const HomeFigures f = LoadHomeFigures(db.code, "20261119");
    const ReportContext ctx = LoadReportContext(db.code, "20261119");

    // AR owed = Open Receivables Detail; AP owed = Open Payables Detail.
    CHECK(f.arOwed == totalAt(ArOpenReceivables(db.code, ctx), 5));
    CHECK(f.apOwed == totalAt(ApOpenPayables(db.code, ctx), 5));
    CHECK(f.arOwed > 0);
    CHECK(f.apOwed > 0);
    CHECK(f.arOverdue <= f.arOwed);
    CHECK(f.apOverdue <= f.apOwed);

    // Waiting vouchers = the posting lists.
    CHECK(f.glWaiting == checkable(*MakePostTask(VoucherKind::Gl), db.code));
    CHECK(f.apWaiting == checkable(*MakePostTask(VoucherKind::ApInvoice), db.code) +
                             checkable(*MakePostTask(VoucherKind::ApCash), db.code));
    CHECK(f.arWaiting == checkable(*MakePostTask(VoucherKind::Ar), db.code));

    // Waiting journals = the transfer's count.
    int journal = 0;
    for (const TaskRow& r : MakeTransferTask(0)->Load(db.code)) journal += std::atoi(r.cells[1].c_str());
    CHECK(f.journalRows == journal);
    CHECK(f.journalRows > 0);

    // The period entry selects, and the depreciation run's state for it.
    CHECK(f.period == 7);
    const RunPlan plan = PlanRun(db.code, 7);
    CHECK(f.depreciationRun == plan.alreadyRun);
    CHECK(f.assetsDue == static_cast<int>(plan.due.size()));

    // After posting everything and transferring, the counts fall to what's left.
    for (VoucherKind k : {VoucherKind::Gl, VoucherKind::ApInvoice, VoucherKind::ApCash, VoucherKind::Ar}) {
        auto post = MakePostTask(k);
        std::vector<TaskRow> rows = post->Load(db.code);
        RunTask(*post, db.code, rows);
    }
    const HomeFigures after = LoadHomeFigures(db.code, "20261119");
    CHECK(after.glWaiting == 1);   // 102, out of balance
    CHECK(after.apWaiting == 0);
    CHECK(after.arWaiting == 0);
    CHECK(after.journalRows > f.journalRows);   // posting adds journal rows to transfer
    CHECK(after.arOwed == totalAt(ArOpenReceivables(db.code, ctx), 5));
    CHECK(after.apOwed == totalAt(ApOpenPayables(db.code, ctx), 5));
}

TEST_CASE("Report gallery: grouped entries, search, favourites and recent (S8-M5)", "[home]")
{
    FixtureDb db;
    GalleryModel g;
    g.SetEntries(LoadGalleryEntries(db.code));
    REQUIRE(g.entries().size() > 40);
    auto find = [&](std::uint16_t command) {
        for (size_t i = 0; i < g.entries().size(); ++i)
            if (g.entries()[i].command == command) return static_cast<int>(i);
        return -1;
    };
    const int tb = find(6076), openAr = find(4057), chart = find(10071);
    REQUIRE(tb >= 0);
    REQUIRE(openAr >= 0);
    REQUIRE(chart >= 0);
    CHECK(g.entries()[static_cast<size_t>(tb)].name == "Trial Balance");
    CHECK(g.entries()[static_cast<size_t>(tb)].group == "General Ledger");
    CHECK(g.entries()[static_cast<size_t>(openAr)].group == "Accounts Receivable");
    CHECK(find(CM_REPORT_LIST) < 0);   // the list itself isn't a report

    // Search: a word of the name first, then anywhere (the group too).
    const std::vector<size_t> trial = g.Filter("trial");
    REQUIRE(trial.size() >= 2);
    CHECK(g.entries()[trial[0]].command == 6076);
    const std::vector<size_t> vat = g.Filter("vat");
    REQUIRE(vat.size() >= 4);   // the four VAT reports first, by a word of the name; then any with "vat" inside
    for (size_t i = 0; i < 4; ++i) CHECK(g.entries()[vat[i]].name.rfind("VAT", 0) == 0);
    CHECK_FALSE(g.Filter("receivable").empty());   // the group
    CHECK(g.Filter("").size() == g.entries().size());

    // Favourites and recent, kept as text in GL_UISTATE.
    g.LoadState("", "");
    g.ToggleFavourite(g.entries()[static_cast<size_t>(tb)]);
    g.ToggleFavourite(g.entries()[static_cast<size_t>(openAr)]);
    CHECK(g.favouritesText() == "c6076,c4057");
    CHECK(g.IsFavourite(g.entries()[static_cast<size_t>(tb)]));
    g.ToggleFavourite(g.entries()[static_cast<size_t>(tb)]);
    CHECK(g.favouritesText() == "c4057");
    for (int i = 0; i < 10; ++i) g.Opened(g.entries()[static_cast<size_t>(i)]);
    g.Opened(g.entries()[static_cast<size_t>(tb)]);
    CHECK(g.Recent().size() == GalleryModel::kRecent);
    CHECK(g.Recent().front() == static_cast<size_t>(tb));
    GalleryModel again;
    again.SetEntries(g.entries());
    again.LoadState(g.favouritesText(), g.recentText() + ",c9999");   // an entry that no longer exists is skipped
    CHECK(again.Favourites() == std::vector<size_t>{static_cast<size_t>(openAr)});
    CHECK(again.Recent().front() == static_cast<size_t>(tb));
    CHECK(again.Recent().size() == GalleryModel::kRecent);
}

TEST_CASE("Command search: codes, then label words, commands before masters (S8-M5)", "[home]")
{
    FixtureDb db;
    SearchModel s;
    s.SetItems(LoadSearchItems(db.code, [](std::uint16_t id) { return id != 1068; }));   // year end greyed
    auto first = [&](const std::string& text) -> const SearchItem& {
        const std::vector<size_t> hits = s.Match(text);
        REQUIRE_FALSE(hits.empty());
        return s.items()[hits.front()];
    };
    CHECK(first("trial bal").command == 6076);
    CHECK(first("chart").command == 1004);                 // the command before the account names
    CHECK(first("101000").kind == SearchItem::Kind::Account);
    CHECK(first("C001").kind == SearchItem::Kind::Customer);
    CHECK(first("V002").kind == SearchItem::Kind::Vendor);
    CHECK(first("102").kind == SearchItem::Kind::Voucher);   // voucher 102's number before account 102101
    CHECK(first("102").code == "102");
    CHECK(first("hospital").kind == SearchItem::Kind::Customer);
    CHECK(first("post gen").command == 10121);              // Post Voucher > General Ledger
    for (size_t i : s.Match("year end")) CHECK(s.items()[i].command != 1068);   // greyed: left out
    CHECK(s.Match("").empty());
    CHECK(s.Match("zzzz").empty());
    CHECK(s.Match("a", 5).size() == 5);
}
