//
// gl_tests: the Spec #8 acceptance gaps fixed after the sheet (S8-M7; gaps G2,
// G8 and G9 of gl/docs/superpowers/specs/2026-11-19-spec8-acceptance.md):
//   G2  the report gallery lists a report imported from a .gldef file;
//   G8  Create Cheques' options (pay date, bank, first number) and hold /
//       release / force through the task page's check boxes;
//   G9  the receipts page reverses the latest receipt.
//
#include "GlTestDb.h"

#include "Accounts.h"
#include "Ap.h"
#include "Ar.h"
#include "ReportWriter.h"
#include "pages/HomeModels.h"
#include "pages/SubledgerModels.h"
#include "pages/TaskModels.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace gltest;

namespace {

std::string controlOf(Code4& code, long batchno)
{
    for (const PaymentItem& p : PaymentItems(code, "99991231", true))
        if (p.item.batchno == batchno) return TrimText(p.item.paymentCtl);
    return "<none>";
}

std::string one(Code4& code, const std::string& sql)
{
    cb4sqlite::Query q(code, sql);
    return q.next() ? q.text(0) : std::string("<none>");
}

} // namespace

TEST_CASE("Report gallery: an imported report is listed with the user reports (S8-M7 G2)", "[gaps8]")
{
    FixtureDb db;
    const size_t before = LoadGalleryEntries(db.code).size();
    const rw::ImportResult r =
        rw::ImportDefinition(db.code, "writer 1\nname \"Imported sales\"\nsource sales_lines 1\ncolumn invoice\n", "", "20261119120000");
    INFO(r.refusal);
    REQUIRE(r.number > 0);
    const std::vector<GalleryEntry> after = LoadGalleryEntries(db.code);
    CHECK(after.size() == before + 1);
    const auto it = std::find_if(after.begin(), after.end(), [&](const GalleryEntry& e) { return e.definition == r.number; });
    REQUIRE(it != after.end());
    CHECK(it->name == "Imported sales");
    CHECK(it->group == "User reports");
    // What Export writes is the stored text: it imports again, numbered.
    const auto stored = rw::FindDefinition(db.code, r.number);
    REQUIRE(stored);
    const rw::ImportResult again = rw::ImportDefinition(db.code, stored->text, "", "20261119120001");
    CHECK(again.number > r.number);
    CHECK(again.renamed);
}

TEST_CASE("Create Cheques task: pay date, bank, first number; the boxes hold and force (S8-M7 G8)", "[gaps8]")
{
    FixtureDb db;
    Code4& code = db.code;
    PostApVouchers(code, ApKind::Invoice);   // invoices 1 and 2 become open items

    auto task = MakeCreateChequesTask("20261031");
    CHECK(task->Checks());
    CHECK(task->HasPaymentOptions());
    const PaymentOptions defaults = task->DefaultPaymentOptions(code);
    REQUIRE_FALSE(defaults.banks.empty());
    CHECK(defaults.bank == defaults.banks.front().first);
    CHECK(defaults.firstNumber == NextChequeNumber(code));
    CHECK(defaults.payDate == "20261031");

    // Due by the end of the year: what's due is checked.
    task->option = "20261231";
    std::vector<TaskRow> rows = task->Load(code);
    REQUIRE(rows.size() >= 2);
    for (const TaskRow& r : rows) {
        INFO(r.key);
        CHECK(r.selectable);
    }
    REQUIRE(rows[0].checked);
    REQUIRE(rows[1].checked);
    const long first = rows[0].key, second = rows[1].key;

    // The first unchecked: held; the cheques start past the fixture's.
    const long chosen = defaults.firstNumber + 100;
    rows[0].checked = false;
    task->payment = defaults;
    task->payment.firstNumber = chosen;
    CHECK(task->Question(rows).find("for " + std::to_string(rows.size() - 1)) != std::string::npos);
    TaskRun run = RunTask(*task, code, rows);
    INFO(run.message);
    REQUIRE(run.ok);
    CHECK(run.message.find("cheque " + std::to_string(chosen) + " ") != std::string::npos);
    CHECK(run.message.find("held 1") != std::string::npos);
    CHECK(controlOf(code, first) == ap_payment::kHeld);
    CHECK(controlOf(code, second) == ap_payment::kPaid);

    // Listed again: held, unchecked, with why. Checking it with a number on file is refused,
    // and the hold is put back.
    rows = task->Load(code);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].key == first);
    CHECK_FALSE(rows[0].checked);
    CHECK(rows[0].note == "Held: check it to pay it.");
    rows[0].checked = true;
    task->payment.firstNumber = chosen;
    run = RunTask(*task, code, rows);
    CHECK_FALSE(run.ok);
    CHECK(run.message == "Cheque " + std::to_string(chosen) + " is already on file.");
    CHECK(controlOf(code, first) == ap_payment::kHeld);

    // A blank number is the next one; released and paid.
    task->payment.firstNumber = 0;
    const long next = NextChequeNumber(code);
    run = RunTask(*task, code, rows);
    INFO(run.message);
    REQUIRE(run.ok);
    CHECK(run.message.find("cheque " + std::to_string(next)) != std::string::npos);
    CHECK(run.message.find("released 1") != std::string::npos);
    CHECK(controlOf(code, first) == ap_payment::kPaid);

    // A closed or unknown pay date is refused before any box changes a control.
    task->payment.payDate = "19990101";
    CHECK(RunTask(*task, code, rows).message == "The payment date isn't in an open period.");
}

TEST_CASE("Create Cheques task: checking an invoice not yet due forces it (S8-M7 G8)", "[gaps8]")
{
    FixtureDb db;
    Code4& code = db.code;
    PostApVouchers(code, ApKind::Invoice);
    auto task = MakeCreateChequesTask("20261031");
    task->option = "20000101";   // nothing is due yet
    std::vector<TaskRow> rows = task->Load(code);
    REQUIRE(rows.size() >= 2);
    for (const TaskRow& r : rows) {
        CHECK_FALSE(r.checked);
        CHECK(r.note == "Not due: check it to pay it now.");
    }
    rows[1].checked = true;
    const TaskRun run = RunTask(*task, code, rows);
    INFO(run.message);
    REQUIRE(run.ok);
    CHECK(run.message.find("forced 1") != std::string::npos);
    CHECK(controlOf(code, rows[1].key) == ap_payment::kPaid);
    CHECK(controlOf(code, rows[0].key) == ap_payment::kOpen);   // not due, unchecked: left as it was
}

TEST_CASE("Receipts page: the latest receipt reversed (S8-M7 G9)", "[gaps8]")
{
    FixtureDb db;
    Code4& code = db.code;
    ReceiptsModel receipts(code);
    receipts.Load();
    REQUIRE_FALSE(receipts.items().empty());
    const ReceiptItem item = receipts.items().front();
    REQUIRE(receipts.Select(item.item.batchno));
    const std::int64_t owed = receipts.selected()->item.remainder;
    const std::int64_t received = receipts.selected()->item.received;
    const std::string rcpts = one(code, "SELECT count(*) FROM ar_rcpt");

    receipts.date = "20261025";
    receipts.bank = 0;
    receipts.amount = "10.00";
    REQUIRE(receipts.Record().ok);
    CHECK(receipts.selected()->item.remainder == owed - 1000);
    REQUIRE(receipts.HasReceipt());

    receipts.date = "";
    CHECK(receipts.Reverse().field == "date");
    receipts.date = "20261025";
    const EntryResult r = receipts.Reverse();
    INFO(r.message);
    REQUIRE(r.ok);
    CHECK(r.message.find("reversed: " + fx::model::FormatAmount(owed) + " is owed") != std::string::npos);
    CHECK(receipts.selected()->item.remainder == owed);
    CHECK(receipts.selected()->item.received == received);
    CHECK(one(code, "SELECT count(*) FROM ar_rcpt") == rcpts);
    if (received == 0) CHECK_FALSE(receipts.HasReceipt());
}
