//
// gl_tests: the Cheques and Bank Reconciliation task pages (spec #9 D5, D6;
// plan S9-M3), through their models, each step compared table by table with
// the classic screen's call on a second fixture copy (ApPayments.cpp:
// ConfirmDialog's ConfirmPrinted and RenumberCheque, BankRecDialog's
// ClearCheque and VoidCheque). The Spec #5 sheet's cheque steps.
//
#include "GlTestDb.h"

#include "Accounts.h"
#include "Ap.h"
#include "pages/TaskModels.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace gltest;

namespace {

std::string value(const Field4& f)
{
    const std::string t = TrimText(f.str());
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (t.empty() || (end && *end == '\0' && v == 0.0)) return "";
    return t;
}

// A table's rows (not deleted), sorted; update / user / time left out.
std::vector<std::string> snapshot(Code4& code, const char* table)
{
    Data4 d(code, table);
    REQUIRE(d.isValid());
    std::vector<Field4> fields;
    for (int i = 1; i <= d.numFields(); ++i) {
        Field4 f(d, i);
        std::string name = f.name();
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name == "update" || name == "user" || name == "time") continue;
        fields.push_back(f);
    }
    std::vector<std::string> rows;
    for (d.top(); !d.eof(); d.skip()) {
        if (d.deleted()) continue;
        std::string row;
        for (const Field4& f : fields) row += value(f) + "|";
        rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

void same(Code4& a, Code4& b)
{
    for (const char* t : {"ap_dist", "ap_reqst", "ap_jsum"}) {
        INFO(t);
        CHECK(snapshot(a, t) == snapshot(b, t));
    }
}

// The purchase invoices posted and their cheques made, as the Spec #5 sheet's
// steps before Confirm Printed Cheques leave them.
void madeCheques(Code4& code)
{
    PostApVouchers(code, ApKind::Invoice);
    const ApAccounts setat = LoadApAccounts(code);
    REQUIRE_FALSE(setat.banks.empty());
    const CreateChequesResult r = CreateCheques(code, "20261231", "20261031", setat.banks.front().account, NextChequeNumber(code));
    REQUIRE_FALSE(LoadChequesIn(code, ap_payment::kPaid).empty());
    (void)r;
}

int selectable(const std::vector<TaskRow>& rows)
{
    return static_cast<int>(std::count_if(rows.begin(), rows.end(), [](const TaskRow& r) { return r.selectable; }));
}

} // namespace

TEST_CASE("Cheques page: renumber a spoiled cheque, confirm the rest, as 8074 did (S9-M3)", "[cheques]")
{
    FixtureDb page, classic;
    madeCheques(page.code);
    madeCheques(classic.code);
    same(page.code, classic.code);

    auto task = MakeChequesTask();
    CHECK(task->Title() == "Cheques");
    CHECK(task->PageCommand() == 4091);   // Print cheques: the preview (Q3)
    std::vector<TaskRow> rows = task->Load(page.code);
    REQUIRE_FALSE(rows.empty());
    CHECK(rows.size() == LoadChequesIn(page.code, ap_payment::kPaid).size());
    CHECK(rows[0].checked);
    const long spoiled = rows[0].key;

    // Renumber: the new number starts as the next free one.
    const long next = task->RowInputDefault(page.code);
    CHECK(next == NextChequeNumber(page.code));
    task->rowInput = "";
    TaskStep s = task->RunRowAction(page.code, rows[0]);
    CHECK_FALSE(s.ok);
    CHECK(s.note == "Enter the new cheque number.");
    if (rows.size() > 1) {   // a number in use
        task->rowInput = std::to_string(rows[1].key);
        s = task->RunRowAction(page.code, rows[0]);
        CHECK_FALSE(s.ok);
        CHECK(s.note == "Cheque number " + std::to_string(rows[1].key) + " has already been used.");
    }
    task->rowInput = std::to_string(next);
    s = task->RunRowAction(page.code, rows[0]);
    REQUIRE(s.ok);
    CHECK(s.note == "Cheque " + std::to_string(spoiled) + " is now cheque " + std::to_string(next) + ". Print it again.");
    REQUIRE(RenumberCheque(classic.code, spoiled, next) == RenumberResult::Renumbered);
    same(page.code, classic.code);

    // Confirm printed: every checked cheque, one at a time. (The fixture has a printed cheque already.)
    const size_t printedBefore = LoadChequesIn(page.code, ap_payment::kPrinted).size();
    rows = task->Load(page.code);
    long lo = rows.front().key, hi = rows.front().key;
    for (const TaskRow& r : rows) {
        lo = std::min(lo, r.key);
        hi = std::max(hi, r.key);
    }
    const TaskRun run = RunTask(*task, page.code, rows);
    CHECK(run.done == static_cast<int>(rows.size()));
    for (const TaskRow& r : rows) CHECK(r.note == "Confirmed printed.");
    CHECK(ConfirmPrinted(classic.code, lo, hi) == static_cast<int>(rows.size()));
    same(page.code, classic.code);
    CHECK(task->Load(page.code).empty());
    CHECK(LoadChequesIn(page.code, ap_payment::kPrinted).size() == printedBefore + rows.size());
}

TEST_CASE("Cheques page: an unchecked cheque isn't confirmed (S9-M3)", "[cheques]")
{
    FixtureDb db;
    madeCheques(db.code);
    auto task = MakeChequesTask();
    std::vector<TaskRow> rows = task->Load(db.code);
    for (TaskRow& r : rows) r.checked = false;
    rows[0].checked = true;
    RunTask(*task, db.code, rows);
    CHECK(ChequeState(db.code, rows[0].key) == ap_payment::kPrinted);
    for (size_t i = 1; i < rows.size(); ++i) CHECK(ChequeState(db.code, rows[i].key) == ap_payment::kPaid);
}

TEST_CASE("Bank Reconciliation page: made cheques listed with why, printed ones cleared, as 6085 did (S9-M3)", "[cheques]")
{
    FixtureDb page, classic;
    madeCheques(page.code);
    madeCheques(classic.code);

    auto task = MakeBankReconciliationTask();
    CHECK(task->Option() == TaskOption::Date);
    CHECK_FALSE(task->OptionFiltersList());
    std::vector<long> made;
    for (const ChequeToPrint& c : LoadChequesIn(page.code, ap_payment::kPaid)) made.push_back(c.cheque.number);
    std::vector<TaskRow> rows = task->Load(page.code);
    REQUIRE(rows.size() > made.size());   // and the fixture's printed cheque
    for (const TaskRow& r : rows) {
        INFO(r.key);
        CHECK_FALSE(r.checked);   // a statement's cheques are checked one by one
        if (std::find(made.begin(), made.end(), r.key) != made.end()) {   // made, not printed: no check box
            CHECK_FALSE(r.selectable);
            CHECK(r.cells[2] == "Made");
            CHECK(r.note == "Not confirmed printed: confirm it on the Cheques page.");
        } else {
            CHECK(r.selectable);
            CHECK(r.cells[2] == "Printed");
        }
    }

    for (Code4* c : {&page.code, &classic.code}) ConfirmPrinted(*c, 0, 999999999);
    rows = task->Load(page.code);
    CHECK(selectable(rows) == static_cast<int>(rows.size()));   // all printed now

    rows[0].checked = true;
    task->option = "20261115";
    const TaskRun run = RunTask(*task, page.code, rows);
    CHECK(run.done == 1);
    CHECK(rows[0].note == "Cleared 11/15/2026.");
    REQUIRE(ClearCheque(classic.code, rows[0].key, "20261115") == ClearResult::Cleared);
    same(page.code, classic.code);
    CHECK(ChequeState(page.code, rows[0].key) == ap_payment::kCleared);

    // A cleared cheque leaves the list.
    const std::vector<TaskRow> after = task->Load(page.code);
    CHECK(std::none_of(after.begin(), after.end(), [&](const TaskRow& r) { return r.key == rows[0].key; }));
}

TEST_CASE("Bank Reconciliation page: Void asks, then reverses, as 1051's Void did (S9-M3)", "[cheques]")
{
    FixtureDb page, classic;
    madeCheques(page.code);
    madeCheques(classic.code);
    for (Code4* c : {&page.code, &classic.code}) ConfirmPrinted(*c, 0, 999999999);

    auto task = MakeBankReconciliationTask();
    task->option = "20261031";
    const std::vector<TaskRow> rows = task->Load(page.code);
    REQUIRE_FALSE(rows.empty());
    const TaskRow& r = rows[0];
    CHECK(task->RowAction() == "&Void");
    const std::string q = task->RowQuestion(r);
    CHECK(q.rfind("Void cheque " + std::to_string(r.key) + " (", 0) == 0);
    CHECK(q.find("dated 10/31/2026") != std::string::npos);

    const TaskStep s = task->RunRowAction(page.code, r);
    INFO(s.note);
    REQUIRE(s.ok);
    CHECK(s.note.rfind("Cheque " + std::to_string(r.key) + " voided: ", 0) == 0);
    const VoidOutcome v = VoidCheque(classic.code, r.key, "20261031");
    REQUIRE(v.result == VoidResult::Voided);
    CHECK(s.note == "Cheque " + std::to_string(r.key) + " voided: " + std::to_string(v.itemsReopened) +
                        " invoice(s) open again; " + std::to_string(v.rowsRemoved) + " journal row(s) removed, " +
                        std::to_string(v.rowsReversed) + " reversed.");
    same(page.code, classic.code);
}
