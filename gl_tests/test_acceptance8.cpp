//
// gl_tests: the Spec #8 acceptance walkthrough's figures (plan S8-M7 task 1;
// gl/docs/superpowers/specs/2026-11-19-spec8-acceptance.md). The moved steps
// are run in the sheet's order, on one fixture copy, through the page models
// the redesigned screens use; every figure the sheet quotes is checked here.
//
#include "GlTestDb.h"

#include "Accounts.h"
#include "Ar.h"
#include "Vouchers.h"
#include "pages/CustomersModel.h"
#include "pages/GlVoucherModel.h"
#include "pages/HomeModels.h"
#include "pages/RecordModels.h"
#include "pages/SubledgerModels.h"
#include "pages/TaskModels.h"

#include <string>
#include <vector>

using namespace gltest;
namespace m = fx::model;

namespace {

std::string one(Code4& code, const std::string& sql)
{
    cb4sqlite::Query q(code, sql);
    return q.next() ? q.text(0) : std::string("<none>");
}

void typeCell(m::EntryGridModel& g, const std::string& text)
{
    if (!text.empty()) {
        REQUIRE(g.BeginEdit(false));
        g.SetEditText(text);
    }
    INFO(text);
    REQUIRE(g.Key(m::GridKey::Tab) != m::GridAction::Refused);
}

} // namespace

TEST_CASE("The Spec #8 acceptance walkthrough's figures (S8-M7)", "[acceptance8]")
{
    FixtureDb db;
    Code4& code = db.code;

    // Home, before anything: what needs doing.
    HomeFigures home = LoadHomeFigures(code, "20261119");
    CHECK(home.period == 7);
    CHECK(home.glWaiting == 3);
    CHECK(home.apWaiting == 3);
    CHECK(home.arWaiting == 2);
    CHECK(home.journalRows == 19);
    CHECK(home.arOwed == 280000);
    CHECK(home.apOwed == 490000);

    // 4. List and detail: C004 on the Customers page, found by part of its name, changed without a question.
    {
        CustomersModel customers(code);
        customers.Load();
        customers.New();
        customers.edit().number = "C004";
        customers.edit().shortName = "EASTCLINIC";
        customers.edit().name = "East Clinic";
        customers.edit().sales.dept = "20";
        REQUIRE(customers.Save("15", "10").ok);
        CHECK(customers.Filter("east").size() == 1);
        REQUIRE(customers.Select("C004"));
        CustomersModel::Result r = customers.Save("45", "10");   // no "already added" question (D9)
        CHECK(r.ok);
        CHECK(one(code, "SELECT terms FROM ar_cust WHERE trim(vendorno) = 'C004'") == "45");
    }

    // 5. Entry: a five-line GL voucher, keyboard only; saved as 104 and posted.
    GlVoucherModel gl(code, false, 0);
    gl.New("20261020");
    gl.description = "Acceptance";
    const char* lines[][5] = {{"101000", "", "ACC-1", "100.00", ""},
                              {"102101", "", "ACC-1", "200.50", ""},
                              {"111101", "", "ACC-1", "49.50", ""},
                              {"410001", "10", "ACC-1", "", "300.00"},
                              {"410001", "20", "ACC-1", "", "50.00"}};
    for (int i = 0; i < 5; ++i) {
        typeCell(gl.grid(), lines[i][0]);
        typeCell(gl.grid(), lines[i][1]);
        typeCell(gl.grid(), lines[i][2]);
        typeCell(gl.grid(), "");
        typeCell(gl.grid(), lines[i][3]);
        typeCell(gl.grid(), lines[i][4]);
        typeCell(gl.grid(), "");
        typeCell(gl.grid(), "");
        typeCell(gl.grid(), "");
        if (i == 3) CHECK_FALSE(gl.grid().Balances("debit", "credit"));   // Post isn't shown yet
    }
    CHECK(gl.grid().Total("debit") == 35000);
    CHECK(gl.grid().Balances("debit", "credit"));
    REQUIRE(gl.Save().ok);
    CHECK(gl.number == 104);
    REQUIRE(gl.Post().ok);
    CHECK(one(code, "SELECT count(*) FROM gl_dist WHERE batchno = 104 AND _deleted = 0") == "5");

    // 6. Feedback: a wrong account and a closed period are refused, on their fields.
    {
        GlVoucherModel bad(code, false, 0);
        bad.New("20261020");
        bad.description = "Refused";
        REQUIRE(bad.grid().BeginEdit(false));
        bad.grid().SetEditText("999999");
        CHECK(bad.grid().Key(m::GridKey::Tab) == m::GridAction::Refused);
        CHECK(bad.grid().message() == "Not an account in the chart.");
        bad.grid().CancelEdit();
        bad.period = 6;   // closed
        typeCell(bad.grid(), "101000");
        CHECK(bad.Save().field == "period");
    }

    // 7. Background work: Post GL vouchers posts 101 and 103; 102 is listed, unchecked, with why.
    {
        auto post = MakePostTask(VoucherKind::Gl);
        std::vector<TaskRow> rows = post->Load(code);
        REQUIRE(rows.size() == 3);
        CHECK(rows[1].key == 102);
        CHECK_FALSE(rows[1].checked);
        CHECK(rows[1].note == "Out of balance by 10.00.");
        const TaskRun run = RunTask(*post, code, rows);
        CHECK(run.done == 2);
        CHECK(run.message == "2 GL vouchers done.");
    }

    // 2. The figures don't change: sales voucher 7 (3 x 1,100.00) and its receipt, on the pages.
    {
        ArInvoiceModel sale(code);
        sale.New("20261020");
        REQUIRE(sale.SelectCustomer("C001"));
        sale.invoiceNo = "S-1007";
        sale.reference = "DR-7";
        for (int i = 0; i < 3; ++i) {
            for (const std::string& t : {std::string(), sale.sales.account, sale.sales.dept, std::string(), std::string(),
                                         std::string(), std::string("1,100.00"), std::string(), std::string()})
                typeCell(sale.grid(), t);
        }
        CHECK(sale.Split().total == 330000);
        CHECK(sale.Split().vat == 30000);
        REQUIRE(sale.Save().ok);
        CHECK(sale.number == 7);
        REQUIRE(sale.Post().ok);
        ReceiptsModel receipts(code);
        receipts.Load();
        REQUIRE(receipts.Select(7));
        receipts.date = "20261025";
        receipts.amount = "1,000.00";
        REQUIRE(receipts.Record().ok);
        CHECK(receipts.selected()->item.remainder == 230000);
    }

    // Update AR and AP: the GL vouchers it makes.
    {
        auto transfer = MakeTransferTask(0);
        const TaskRun t = RunTask(*transfer, code, *std::make_unique<std::vector<TaskRow>>(transfer->Load(code)));
        CHECK(t.ok);
        CHECK(t.message == "Made GL voucher 105 (AP, period 7), GL voucher 106 (AR, period 7).");
    }

    // Home, after: the figures follow.
    home = LoadHomeFigures(code, "20261119");
    CHECK(home.glWaiting == 3);   // 102, and the transfer's 105 and 106
    CHECK(home.arWaiting == 2);
    CHECK(home.journalRows == 0);
    CHECK(home.arOwed == 280000 + 230000);
    CHECK(home.apOwed == 490000);
    CHECK(one(code, "SELECT count(*) FROM gl_btch WHERE _deleted = 0 AND trim(mode) IN ('0', '2')") == "4");   // 100, 101, 104; 103 as a posted adjustment
}
