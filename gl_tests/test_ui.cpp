//
// gl_tests: the redesign's portable models (spec #8 5.1, 6, 10; plan S8-M1):
// anchors, validators, lookup matching, GL_UISTATE, and the Customers page's
// model through the Spec #6 sheet's criterion 1.
//
#include "GlTestDb.h"

#include "Accounts.h"   // TrimText
#include "Ar.h"
#include "UiState.h"
#include "model/Anchors.h"
#include "model/LookupModel.h"
#include "model/Validate.h"
#include "pages/CustomersModel.h"

#include <string>
#include <vector>

using namespace gltest;
namespace m = fx::model;

TEST_CASE("Anchors place controls as the page resizes (S8-M1)", "[ui]")
{
    const m::Size original{400, 300};
    const m::Box b{10, 20, 110, 40};
    CHECK(m::Place(b, m::kTopLeft, original, {500, 400}) == b);                        // stays
    CHECK(m::Place(b, m::kTopWide, original, {500, 400}) == m::Box{10, 20, 210, 40});  // stretches across
    CHECK(m::Place(b, m::kTopRight, original, {500, 400}) == m::Box{110, 20, 210, 40});   // moves right
    CHECK(m::Place(b, m::kAll, original, {500, 400}) == m::Box{10, 20, 210, 140});
    CHECK(m::Place(b, m::kBottomRight, original, {450, 350}) == m::Box{60, 70, 160, 90});
    // Never laid out smaller than the template: below it the page scrolls.
    CHECK(m::Place(b, m::kAll, original, {200, 100}) == b);
    CHECK(m::Clamp({200, 500}, original).cx == 400);
    CHECK(m::Clamp({200, 500}, original).cy == 500);
}

TEST_CASE("Validators and amounts (S8-M1)", "[ui]")
{
    CHECK(m::Required()("  ") == std::optional<std::string>("Required."));
    CHECK_FALSE(m::Required()(" x "));
    CHECK(m::MaxLength(6)("C00001X") == std::optional<std::string>("At most 6 characters."));
    CHECK_FALSE(m::MaxLength(6)("C0001 "));
    CHECK_FALSE(m::MaxLength(3)("\xC3\xA9\xC3\xA9\xC3\xA9"));   // three code points, six bytes
    CHECK(m::WholeNumber(0, 99)("100") == std::optional<std::string>("A whole number from 0 to 99."));
    CHECK(m::WholeNumber(0, 99)("1.5"));
    CHECK_FALSE(m::WholeNumber(0, 99)(""));
    CHECK_FALSE(m::WholeNumber(0, 99)(" 10 "));
    CHECK(m::Date()("20260231") == std::optional<std::string>("Not a date."));
    CHECK_FALSE(m::Date()("20240229"));
    CHECK(m::Date()("20250229"));
    CHECK(m::Exists([](const std::string& s) { return s == "111101"; }, "Not an account in the chart.")("4") ==
          std::optional<std::string>("Not an account in the chart."));
    const m::Validator v{m::Required(), m::MaxLength(2)};
    CHECK(v.Run("") == std::optional<std::string>("Required."));
    CHECK(v.Run("abc") == std::optional<std::string>("At most 2 characters."));
    CHECK_FALSE(v.Run("ab"));

    CHECK(m::ParseAmount("1,250.50") == std::optional<std::int64_t>(125050));
    CHECK(m::ParseAmount("-12.5") == std::optional<std::int64_t>(-1250));
    CHECK(m::ParseAmount("1250") == std::optional<std::int64_t>(125000));
    CHECK_FALSE(m::ParseAmount("1.234"));
    CHECK_FALSE(m::ParseAmount("12a"));
    CHECK_FALSE(m::ParseAmount("."));
    CHECK(m::FormatAmount(125050) == "1,250.50");
    CHECK(m::FormatAmount(-1250) == "-12.50");
    CHECK(m::FormatAmount(123456789) == "1,234,567.89");
    CHECK(m::FormatAmount(0) == "0.00");
    CHECK(m::Amount()("1.0.0") == std::optional<std::string>("Not an amount."));
    CHECK_FALSE(m::Amount()("1,000"));
}

TEST_CASE("Lookup matching: code, code prefix, name words, anywhere (S8-M1)", "[ui]")
{
    const m::LookupModel model({{"111101", "", "Accounts receivable - trade"},
                                {"410001", "10", "Sales revenue - R&D"},
                                {"410001", "20", "Sales revenue - general"},
                                {"411000", "", "Sales - cash"},
                                {"260007", "", "VAT payable"}});
    auto keys = [&](const std::string& q) {
        std::vector<std::string> out;
        for (size_t i : model.Match(q)) out.push_back(model.items()[i].key());
        return out;
    };
    CHECK(keys("410001-20") == std::vector<std::string>{"410001-20"});
    CHECK(keys("41") == std::vector<std::string>{"410001-10", "410001-20", "411000"});
    CHECK(keys("sales") == std::vector<std::string>{"410001-10", "410001-20", "411000"});
    CHECK(keys("rev") == std::vector<std::string>{"410001-10", "410001-20"});
    CHECK(keys("payable") == std::vector<std::string>{"260007"});
    CHECK(keys("able") == std::vector<std::string>{"111101", "260007"});   // receivABLE, payABLE: anywhere
    CHECK(keys("").size() == 5);
    CHECK(model.Match("", 2).size() == 2);
    CHECK(model.Exact("111101") == 0);
    CHECK(model.Exact("410001") == -1);   // it has depts: "410001-10" is the key
    CHECK(model.Exact("410001-10") == 1);
    CHECK(model.Exact(" 260007 ") == 4);
}

TEST_CASE("GL_UISTATE: per user, replaced, removed (S8-M1)", "[ui]")
{
    FixtureDb db;
    CHECK_FALSE(GetUiState(db.code, "", "window"));   // no table yet
    SetUiState(db.code, "", "window", "0,1,10,10,800,600");
    SetUiState(db.code, "CLERK", "window", "maximized");
    CHECK(GetUiState(db.code, "", "window") == std::optional<std::string>("0,1,10,10,800,600"));
    CHECK(GetUiState(db.code, "CLERK", "window") == std::optional<std::string>("maximized"));
    SetUiState(db.code, "", "window", "replaced");
    CHECK(GetUiState(db.code, "", "window") == std::optional<std::string>("replaced"));
    SetUiState(db.code, "", "window", "");
    CHECK_FALSE(GetUiState(db.code, "", "window"));
    CHECK(GetUiState(db.code, "CLERK", "window") == std::optional<std::string>("maximized"));
    CHECK(UiStateUser("NOLOGIN") == "");
    CHECK(UiStateUser("ADMIN     ") == "ADMIN");
    // A long value goes in the memo whole.
    const std::string pins(500, 'x');
    SetUiState(db.code, "ADMIN", "pins", pins);
    CHECK(GetUiState(db.code, "ADMIN", "pins") == std::optional<std::string>(pins));
}

TEST_CASE("The Customers page's model through the Spec #6 sheet's criterion 1 (S8-M1)", "[ui]")
{
    FixtureDb db;
    CustomersModel model(db.code);
    model.Load();
    REQUIRE(model.rows().size() == 3);
    CHECK(model.rows()[0].number == "C001");
    CHECK(model.rows()[0].owed == 220000);   // S-1003
    CHECK(model.rows()[1].owed == 60000);    // S-1004's remainder
    CHECK(model.Filter("reyes") == std::vector<size_t>{1});
    CHECK(model.Filter("C00").size() == 3);
    CHECK(model.Filter("pharm") == std::vector<size_t>{2});

    // New C004: refused, the default sales account (410001, no dept) isn't in the chart.
    model.New();
    CHECK(model.isNew());
    CHECK(TrimText(model.current().ar.account) == "111101");
    CHECK(TrimText(model.current().sales.account) == "410001");
    CHECK(TrimText(model.current().vat.account) == "260007");
    Customer& c = model.edit();
    c.number = "C004";
    c.shortName = "EASTCLINIC";
    c.name = "East Clinic";
    c.address1 = "5 East Road";
    CustomersModel::Result r = model.Save("", "");
    CHECK_FALSE(r.ok);
    CHECK(r.input == CustomerInput::SalesAccount);
    CHECK(r.message == "Not an account in the chart.");

    // Sales dept 20, VAT 10, terms 15: saved.
    model.edit().sales.dept = "20";
    r = model.Save("15", "10");
    INFO(r.message);
    REQUIRE(r.ok);
    CHECK(r.message == "Customer C004 saved.");
    CHECK_FALSE(model.isNew());
    CHECK(model.rows().size() == 4);
    CHECK(model.RowOf("C004") == 3);

    // Terms 45: saved without "Do you want to modify the entry?" (D9).
    r = model.Save("45", "10");
    REQUIRE(r.ok);
    CHECK(FindCustomer(db.code, "C004")->terms == 45);
    CHECK(model.rows().size() == 4);

    // Bad numbers are marked on their fields.
    r = model.Save("1000", "10");
    CHECK(r.input == CustomerInput::Terms);
    r = model.Save("45", "x");
    CHECK(r.input == CustomerInput::VatRate);

    // A second new C004 is refused: the number is taken.
    model.New();
    model.edit().number = "C004";
    model.edit().shortName = "X";
    model.edit().name = "X";
    model.edit().sales.dept = "20";
    r = model.Save("0", "0");
    CHECK(r.input == CustomerInput::Number);

    // C001 can't be deleted.
    REQUIRE(model.Select("C001"));
    CHECK(model.OpenItems().size() == 1);
    r = model.Delete();
    CHECK_FALSE(r.ok);
    CHECK(r.message == "Customer C001 can't be deleted: sales voucher 1 is theirs.");

    // C004 is deleted; three customers again.
    REQUIRE(model.Select("C004"));
    r = model.Delete();
    CHECK(r.ok);
    CHECK(model.isNew());
    cb4sqlite::Query q(db.code, "SELECT count(*) FROM ar_cust WHERE _deleted = 0");
    REQUIRE(q.next());
    CHECK(q.integer(0) == 3);

    // C999 isn't on file.
    CHECK_FALSE(model.Select("C999"));
    CHECK(model.AccountExists("410001", "20"));
    CHECK_FALSE(model.AccountExists("410001", ""));
}
