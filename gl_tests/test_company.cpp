//
// gl_tests: the Company Profile page's model (spec #9 D3, D4; plan S9-M2),
// each tab through the tables its classic dialog wrote (CompanyProfile.cpp:
// 1021, 1028, 1033, 1034). No test drove those writes before. The dialogs'
// code is window code and can't run here, so each case checks with SQL every
// field the dialog wrote, and that the tabs not saved are untouched.
//
#include "GlTestDb.h"

#include "Accounts.h"   // TrimText
#include "pages/CompanyModel.h"

#include <string>
#include <vector>

using namespace gltest;
namespace m = fx::model;

namespace {

std::string scalar(Code4& code, const std::string& sql)
{
    cb4sqlite::Query q(code, sql);
    return q.next() ? q.text(0) : std::string("<none>");
}

// Every row of a table, for "nothing else changed".
std::string tableText(Code4& code, const std::string& table)
{
    cb4sqlite::Query q(code, "SELECT * FROM " + table + " ORDER BY _rowid");
    std::string all;
    while (q.next()) {
        for (int c = 0; c < q.columns(); ++c) all += q.text(c) + "|";
        all += "\n";
    }
    return all;
}

struct Company {
    FixtureDb db;
    std::string withholding = "T";
    CompanyModel m{db.code};
    m::EntryGridModel& periods;
    m::EntryGridModel& accounts;
    m::EntryGridModel& banks;

    Company() : periods(*m.Grid("periods")), accounts(*m.Grid("accounts")), banks(*m.Grid("banks"))
    {
        m.dataPath = "dev.db";
        m.readWithholding = [this] { return withholding; };
        m.writeWithholding = [this](const std::string& v) { withholding = v; };
        m.Load();
        REQUIRE(m.Select("company"));
    }
};

} // namespace

TEST_CASE("Company Profile page: the four tabs as the dialogs showed them (S9-M2)", "[company]")
{
    Company c;
    REQUIRE(c.m.rows().size() == 1);
    CHECK(c.m.rows()[0].key == "company");
    CHECK(c.m.Get("company") == "Dev Fixture Company Ltd.");
    CHECK(c.m.Get("address1") == "1 Main Street");
    CHECK(c.m.Get("phone") == "(604) 555 - 0100");
    CHECK(c.m.Get("contact") == "A. Accountant");
    CHECK(c.m.Get("firstDay") == "04/01/2026");   // the fiscal year: period 1's start, period 12's end
    CHECK(c.m.Get("lastDay") == "03/31/2027");
    CHECK(c.m.Get("dataPath") == "dev.db");
    CHECK(c.m.Get("withholding") == "T");

    // Periods: 12 rows, MM/DD/YYYY, closed Y.
    REQUIRE(c.periods.Rows() == 12);
    CHECK(c.periods.Cell(0, "period") == "1");
    CHECK(c.periods.Cell(0, "start") == "04/01/2026");
    CHECK(c.periods.Cell(0, "end") == "04/30/2026");
    CHECK(c.periods.Cell(0, "desc") == "Apr 26");
    CHECK(c.periods.Cell(0, "closed") == "Y");
    CHECK(c.periods.Cell(6, "closed").empty());   // period 7 open
    CHECK(c.periods.Cell(11, "end") == "03/31/2027");

    // Accounts: the eight GL rows in the gl_accts tag's order (SHORT: AP, AR, RE, ...).
    REQUIRE(c.accounts.Rows() == 8);
    CHECK(c.accounts.Cell(0, "name") == "Accounts Payable");
    CHECK(c.accounts.Cell(0, "account") == "221002");
    CHECK(c.accounts.Cell(2, "name") == "Retained Earnings");
    CHECK(c.accounts.Cell(2, "account") == "340000");

    // Banks: the CA rows in the cash tag's order (account + dept), then the blank row.
    REQUIRE(c.banks.Lines() == 2);
    CHECK(c.banks.Cell(0, "account") == "101000");
    CHECK(c.banks.Cell(0, "name") == "Petty Cash");
    CHECK(c.banks.Cell(1, "account") == "102101");
    CHECK(c.banks.Cell(1, "name") == "Checking Account");

    CHECK(c.m.Choices("withholding").size() == 2);
    CHECK_FALSE(c.m.Delete().ok);   // there is no deleting the profile
}

TEST_CASE("Company Profile page: Company tab saved alone, as 1021 wrote it (S9-M2)", "[company]")
{
    Company c;
    const std::string pds = tableText(c.db.code, "gl_pds"), setat = tableText(c.db.code, "gl_setat");
    c.m.Set("phone", "(604) 555 - 0199");
    c.m.Set("company", "Dev Fixture Co.");
    c.m.Set("withholding", "F");
    const RecordModel::Result r = c.m.Save();
    REQUIRE(r.ok);
    CHECK(r.message == "Company profile saved.");
    CHECK(scalar(c.db.code, "SELECT trim(telephone) || '|' || trim(company) FROM gl_prof") ==
          "(604) 555 - 0199|Dev Fixture Co.");
    CHECK(scalar(c.db.code, "SELECT count(*) FROM gl_prof") == "1");   // replaced, not appended
    CHECK(c.withholding == "F");                                       // GL.INI [WITHHOLDING] TAX
    CHECK(tableText(c.db.code, "gl_pds") == pds);                      // the other tabs untouched
    CHECK(tableText(c.db.code, "gl_setat") == setat);
    CHECK(c.m.Get("company") == "Dev Fixture Co.");                    // shown as saved

    // Nothing changed: nothing written, withholding not touched.
    const std::string prof = tableText(c.db.code, "gl_prof");
    c.withholding = "?";
    c.m.Set("withholding", "?");
    REQUIRE(c.m.Select("company"));
    REQUIRE(c.m.Save().ok);
    CHECK(tableText(c.db.code, "gl_prof") == prof);
    CHECK(c.withholding == "?");
}

TEST_CASE("Company Profile page: Periods tab, as 1028 wrote it, with no checks (S9-M2, Q1)", "[company]")
{
    Company c;
    const std::string prof = tableText(c.db.code, "gl_prof"), setat = tableText(c.db.code, "gl_setat");
    c.periods.SetCell(2, "desc", "June 26");
    c.periods.SetCell(2, "closed", "");      // reopen period 3
    c.periods.SetCell(7, "closed", "Y");     // close period 8
    c.periods.SetCell(11, "end", "4/30/2027");   // out of order is allowed, as 1028 allowed it (Q1)
    REQUIRE(c.m.Save().ok);
    CHECK(scalar(c.db.code, "SELECT trim(pd_desc) || '|' || status FROM gl_pds WHERE period = 3") == "June 26|0");
    CHECK(scalar(c.db.code, "SELECT status FROM gl_pds WHERE period = 8") == "1");
    CHECK(scalar(c.db.code, "SELECT periode FROM gl_pds WHERE period = 12") == "20270430");
    CHECK(scalar(c.db.code, "SELECT periods || periode FROM gl_pds WHERE period = 1") == "2026040120260430");
    CHECK(c.m.Get("lastDay") == "04/30/2027");
    CHECK(tableText(c.db.code, "gl_prof") == prof);
    CHECK(tableText(c.db.code, "gl_setat") == setat);

    // A date that isn't one is refused, and nothing is written.
    const std::string pds = tableText(c.db.code, "gl_pds");
    c.periods.SetCell(0, "start", "13/45/2026");   // past the cell's own check, as if typed
    const RecordModel::Result r = c.m.Save();
    CHECK_FALSE(r.ok);
    CHECK(r.message == "Period 1's start isn't a date (Periods tab): enter it as MM/DD/YYYY.");
    CHECK(tableText(c.db.code, "gl_pds") == pds);
}

TEST_CASE("Company Profile page: Accounts tab, as 1033 wrote it, refusing 8121 (S9-M2)", "[company]")
{
    Company c;
    const std::string setat = tableText(c.db.code, "gl_setat");
    c.accounts.SetCell(0, "account", "999999");
    RecordModel::Result r = c.m.Save();
    CHECK_FALSE(r.ok);
    CHECK(r.message == "Unable to locate the account number: 999999 (Accounts tab, Accounts Payable).");
    CHECK(tableText(c.db.code, "gl_setat") == setat);

    c.accounts.SetCell(0, "account", "221002");
    c.accounts.SetCell(3, "account", "410001");   // SALE: dept 10's sales account
    c.accounts.SetCell(3, "dept", "10");
    r = c.m.Save();
    REQUIRE(r.ok);
    CHECK(scalar(c.db.code, "SELECT trim(account) || '|' || trim(dept) FROM gl_setat WHERE trim(short) = 'SALE'") ==
          "410001|10");
    CHECK(scalar(c.db.code, "SELECT trim(account) FROM gl_setat WHERE trim(short) = 'AP'") == "221002");
    CHECK(scalar(c.db.code, "SELECT count(*) FROM gl_setat") == "10");   // no row added
}

TEST_CASE("Company Profile page: Banks tab, as 1034 added, changed and deleted them (S9-M2)", "[company]")
{
    Company c;
    // Add Savings (111101) in the blank row; rename Petty Cash; remove Checking.
    c.banks.SetCell(2, "account", "111101");
    c.banks.SetCell(2, "name", "Savings");
    c.banks.SetCell(0, "name", "Cash Box");
    c.banks.SetCurrent(1, 0);
    REQUIRE(c.banks.Key(m::GridKey::CtrlDelete) == m::GridAction::Removed);
    REQUIRE(c.m.Save().ok);
    CHECK(scalar(c.db.code, "SELECT group_concat(trim(account) || ':' || trim(name), ',') FROM "
                            "(SELECT * FROM gl_setat WHERE type = 'CA' ORDER BY account)") ==
          "101000:Cash Box,111101:Savings");
    CHECK(scalar(c.db.code, "SELECT count(*) FROM gl_setat WHERE _deleted <> 0") == "0");   // packed
    CHECK(scalar(c.db.code, "SELECT trim(short) || '|' || trim(dept) || '|' || type FROM gl_setat "
                            "WHERE trim(account) = '111101' AND type = 'CA'") == "||CA");   // a blank record
    CHECK(scalar(c.db.code, "SELECT count(*) FROM gl_setat WHERE type = 'GL'") == "8");
    REQUIRE(c.banks.Lines() == 2);   // read back
    CHECK(c.banks.Cell(1, "name") == "Savings");

    // An account not in the chart, and a bank listed twice, are refused.
    const std::string setat = tableText(c.db.code, "gl_setat");
    c.banks.SetCell(2, "account", "999999");
    RecordModel::Result r = c.m.Save();
    CHECK_FALSE(r.ok);
    CHECK(r.message == "Unable to locate the account number: 999999 (Banks tab).");
    c.banks.SetCell(2, "account", "101000");
    r = c.m.Save();
    CHECK_FALSE(r.ok);
    CHECK(r.message == "Bank account 101000 is listed twice (Banks tab). Keep one row for it.");
    CHECK(tableText(c.db.code, "gl_setat") == setat);
}

TEST_CASE("Company Profile page: Reset depreciation flag, as 1028's ID_FA (S9-M2)", "[company]")
{
    Company c;
    REQUIRE(scalar(c.db.code, "SELECT count(*) FROM gl_fa WHERE trim(mode) <> ''") == "1");
    CHECK(c.m.ResetDepreciationFlag() == 4);
    CHECK(scalar(c.db.code, "SELECT count(*) FROM gl_fa WHERE trim(mode) <> ''") == "0");
}

TEST_CASE("Company Profile page: dates as MM/DD/YYYY (S9-M2)", "[company]")
{
    CHECK(CompanyModel::ToDisplayDate("19940401") == "04/01/1994");
    CHECK(CompanyModel::ToDisplayDate("") == "");
    CHECK(CompanyModel::FromDisplayDate("04/01/1994") == "19940401");
    CHECK(CompanyModel::FromDisplayDate("4/1/1994") == "19940401");
    CHECK(CompanyModel::FromDisplayDate("04/01/94") == "19940401");
    CHECK(CompanyModel::FromDisplayDate("19940401") == "19940401");
    CHECK(CompanyModel::FromDisplayDate("02/30/2026").empty());   // not a day
    CHECK(CompanyModel::FromDisplayDate("1/2").empty());
    CHECK(CompanyModel::FromDisplayDate("a/b/c").empty());
    CHECK(CompanyModel::FromDisplayDate("").empty());
}

TEST_CASE("Company Profile page: saving leaves other pages' tables open (S9-M2, spec #8 D7)", "[company]")
{
    Company c;
    Data4 other(c.db.code, "glaccts");   // as another open page holds one
    Field4 account(other, "account");
    REQUIRE(other.top() == r4success);
    c.m.Set("contact", "B. Bookkeeper");
    c.banks.SetCell(0, "name", "Cash Box");
    REQUIRE(c.m.Save().ok);
    CHECK(other.isValid());              // no closeAll: 1021's Ok ran cb.closeAll(); cb.init()
    CHECK(other.skip() == r4success);
    CHECK_FALSE(TrimText(account.str()).empty());
}
