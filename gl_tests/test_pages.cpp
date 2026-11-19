//
// gl_tests: the list-and-detail pages' models (spec #8 5; plan S8-M2), each
// through its screen's master criterion on the dev fixture: the rules the
// classic dialog enforced, D9 (no "modify?"; a taken key refused), and the
// User Access page's MENUCODE written byte for byte.
//
#include "GlTestDb.h"

#include "Accounts.h"
#include "Ap.h"
#include "Assets.h"
#include "pages/RecordModels.h"

#include <string>
#include <vector>

using namespace gltest;

namespace {

std::string scalar(Code4& code, const std::string& sql)
{
    cb4sqlite::Query q(code, sql);
    return q.next() ? q.text(0) : std::string("<none>");
}

RecordModel::Result saveNew(RecordModel& m, std::initializer_list<std::pair<const char*, const char*>> values)
{
    m.New();
    for (const auto& [k, v] : values) m.Set(k, v);
    return m.Save();
}

} // namespace

TEST_CASE("Departments page: required fields, a taken code, no modify question (S8-M2)", "[pages]")
{
    FixtureDb db;
    auto m = MakeDepartmentsModel(db.code);
    m->Load();
    REQUIRE(m->rows().size() == 2);
    CHECK(m->Filter("research") == std::vector<size_t>{0});

    RecordModel::Result r = saveNew(*m, {{"depart", "30"}});
    CHECK_FALSE(r.ok);
    CHECK(r.field == "descript");
    r = saveNew(*m, {{"depart", "30"}, {"descript", "Sales"}});
    REQUIRE(r.ok);
    CHECK(r.message == "Department 30 saved.");
    CHECK_FALSE(m->isNew());
    CHECK(m->rows().size() == 3);

    m->Set("descript", "Sales and Marketing");   // saved, nothing asked (D9)
    REQUIRE(m->Save().ok);
    CHECK(scalar(db.code, "SELECT trim(descript) FROM gldept WHERE trim(depart) = '30'") == "Sales and Marketing");

    r = saveNew(*m, {{"depart", "30"}, {"descript", "Again"}});
    CHECK_FALSE(r.ok);
    CHECK(r.field == "depart");
    CHECK(r.message.find("already on file") != std::string::npos);

    REQUIRE(m->Select("30"));
    REQUIRE(m->Delete().ok);
    CHECK(m->rows().size() == 2);
    CHECK(m->isNew());
    CHECK_FALSE(m->Select("99"));
}

TEST_CASE("Medical Reps and Doctors pages: their own tables, terms and credit limit (S8-M2)", "[pages]")
{
    FixtureDb db;
    auto reps = MakeRepsModel(db.code, RepTable::Kind::MedicalRep);
    reps->Load();
    REQUIRE(reps->rows().size() == 3);
    CHECK(reps->Filter("tanaka") == std::vector<size_t>{0});

    RecordModel::Result r = saveNew(*reps, {{"code", "MR004"}, {"name", "Ana Cruz"}, {"terms", "abc"}});
    CHECK_FALSE(r.ok);
    CHECK(r.field == "terms");
    r = saveNew(*reps, {{"code", "MR004"}, {"name", "Ana Cruz"}, {"terms", "30"}, {"creditLimit", "1,500.00"}});
    REQUIRE(r.ok);
    CHECK(scalar(db.code, "SELECT creditlmt FROM gl_medrp WHERE trim(repcode) = 'MR004'") == "1500.0");
    REQUIRE(reps->Select("MR004"));
    CHECK(reps->Get("creditLimit") == "1,500.00");
    CHECK(reps->Get("terms") == "30");
    CHECK(saveNew(*reps, {{"code", ""}}).field == "code");

    auto doctors = MakeRepsModel(db.code, RepTable::Kind::Doctor);
    doctors->Load();
    const size_t before = doctors->rows().size();
    REQUIRE(saveNew(*doctors, {{"code", "DR9"}, {"name", "Dr. Reyes"}}).ok);
    CHECK(doctors->rows().size() == before + 1);
    CHECK(scalar(db.code, "SELECT count(*) FROM gl_medrp WHERE trim(repcode) = 'DR9'") == "0");
    REQUIRE(doctors->Delete().ok);
    CHECK(doctors->rows().size() == before);
}

TEST_CASE("Products page: cost and unit price with three decimals (S8-M2)", "[pages]")
{
    FixtureDb db;
    auto m = MakeProductsModel(db.code);
    m->Load();
    REQUIRE(m->rows().size() == 3);
    REQUIRE(m->Select("P100"));
    CHECK(m->Get("fullName") == "Analgesic tablets 100mg");
    CHECK(std::stod(m->Get("onHand")) == 120.0);
    CHECK(m->Filter("syrup") == std::vector<size_t>{1});

    RecordModel::Result r = saveNew(*m, {{"code", "P400"}, {"fullName", "Eye drops"}, {"onHand", "1.2345"}});
    CHECK_FALSE(r.ok);
    CHECK(r.field == "onHand");
    r = saveNew(*m, {{"code", "P400"}, {"fullName", "Eye drops"}, {"onHand", "12.5"}, {"avPrice", "2.125"}});
    REQUIRE(r.ok);
    CHECK(std::stod(scalar(db.code, "SELECT favprice FROM gl_itemm WHERE trim(fpcode) = 'P400'")) == 2.125);
}

TEST_CASE("Vendors page: default accounts, CheckVendor's refusals, a vendor in use (S8-M2)", "[pages]")
{
    FixtureDb db;
    auto m = MakeVendorsModel(db.code);
    m->Load();
    REQUIRE(m->rows().size() == 3);
    m->New();
    const ApAccounts defaults = LoadApAccounts(db.code);
    CHECK(m->Get("ap") == defaults.ap.account);
    CHECK(m->Get("vat") == defaults.vatInput.account);

    m->Set("number", "V004");
    m->Set("shortName", "PHARMA");
    m->Set("name", "Pharma Supply");
    RecordModel::Result r = m->Save();
    CHECK_FALSE(r.ok);
    CHECK(r.field == "type");   // neither Company nor Individual
    m->Set("type", "C");
    m->Set("vatRate", "120");
    r = m->Save();
    CHECK(r.field == "vatRate");
    m->Set("vatRate", "12");
    m->Set("terms", "30");
    r = m->Save();
    INFO(r.message);
    REQUIRE(r.ok);
    CHECK(FindVendor(db.code, "V004")->vatRate == 12);
    CHECK(m->rows().size() == 4);

    REQUIRE(m->Select("V001"));
    m->Set("phone", "555-0101");
    REQUIRE(m->Save().ok);   // no "update it?" (D9)
    CHECK(TrimText(FindVendor(db.code, "V001")->phone) == "555-0101");
    r = m->Delete();
    CHECK_FALSE(r.ok);
    CHECK(r.message.find("can't be deleted") != std::string::npos);

    REQUIRE(m->Select("V004"));
    REQUIRE(m->Delete().ok);
    CHECK_FALSE(FindVendor(db.code, "V004"));
}

TEST_CASE("Chart of Accounts page: types, retained earnings, activity (S8-M2)", "[pages]")
{
    FixtureDb db;
    auto m = MakeAccountsModel(db.code);
    m->Load();
    REQUIRE(m->rows().size() == 25);
    REQUIRE(m->Select("101000"));
    CHECK(m->Get("type") == "B");
    CHECK(m->Get("currBal") == "250.00");
    CHECK(m->Get("openBal") == "500.00");   // previous12, as the dialog showed it

    RecordModel::Result r = saveNew(*m, {{"account", "998000"}, {"descript", "Suspense"}, {"type", "R"}});
    CHECK_FALSE(r.ok);
    CHECK(r.field == "type");   // 16011: one retained earnings account
    r = saveNew(*m, {{"account", "998000"}, {"descript", "Suspense"}});
    CHECK(r.field == "type");   // 4071: choose a type
    r = saveNew(*m, {{"account", "998000"}, {"descript", ""}, {"type", "B"}});
    CHECK(r.field == "descript");
    r = saveNew(*m, {{"account", "998000"}, {"dept", "10"}, {"descript", "Suspense"}, {"type", "B"}});
    REQUIRE(r.ok);
    CHECK(m->key() == "998000-10");
    CHECK(m->rows().size() == 26);
    r = saveNew(*m, {{"account", "998000"}, {"dept", "10"}, {"descript", "Twice"}, {"type", "B"}});
    CHECK(r.field == "account");   // taken

    REQUIRE(m->Select("101000"));
    r = m->Delete();
    CHECK_FALSE(r.ok);   // 1027: balances
    REQUIRE(m->Select("998000-10"));
    REQUIRE(m->Delete().ok);
    CHECK(m->rows().size() == 25);
}

TEST_CASE("Asset Types page: accounts in the chart, a type in use (S8-M2)", "[pages]")
{
    FixtureDb db;
    auto m = MakeAssetTypesModel(db.code);
    m->Load();
    REQUIRE(m->rows().size() == 2);
    REQUIRE(m->Select("EQ"));
    CHECK(m->Get("asset") == "151000");
    RecordModel::Result r = m->Delete();
    CHECK_FALSE(r.ok);
    CHECK(r.message.find("asset E00") != std::string::npos);

    r = saveNew(*m, {{"code", "VH"}, {"description", "Vehicles"}, {"asset", "999999"}, {"expense", "761000"},
                     {"accum", "161000"}});
    CHECK_FALSE(r.ok);
    CHECK(r.field == "asset");
    r = saveNew(*m, {{"code", "VH"}, {"description", "Vehicles"}, {"asset", "151000"}, {"expense", "761000"},
                     {"accum", "161000"}});
    REQUIRE(r.ok);
    const auto t = FindAssetType(db.code, "VH");
    REQUIRE(t);
    CHECK_FALSE(t->asset.descript.empty());   // the chart's name of 151000
    REQUIRE(m->Delete().ok);
}

TEST_CASE("Fixed Assets page: disposed, depreciated, the monthly amount (S8-M2)", "[pages]")
{
    FixtureDb db;
    auto m = MakeAssetsModel(db.code);
    m->Load();
    REQUIRE(m->rows().size() == 4);

    REQUIRE(m->Select("E004"));   // disposed
    m->Set("description", "Changed");
    RecordModel::Result r = m->Save();
    CHECK_FALSE(r.ok);
    CHECK(r.message.find("disposed of") != std::string::npos);

    REQUIRE(m->Select("E001"));   // depreciated by a run: its figures stand (spec #4 D6)
    m->Set("accumulated", "0");
    m->Set("monthsUsed", "0");
    r = m->Save();
    INFO(r.message);
    REQUIRE(r.ok);
    CHECK(FindAsset(db.code, "E001")->accumulated == 650000);
    CHECK(FindAsset(db.code, "E001")->monthsUsed == 13);
    r = m->Delete();
    CHECK_FALSE(r.ok);   // depreciated: dispose of it instead

    r = saveNew(*m, {{"code", "E009"}, {"type", "EQ"}, {"description", "Printer"}, {"datePurchased", "20260101"},
                     {"cost", "1,200.00"}, {"life", "12"}, {"salvage", "1,300.00"}});
    CHECK(r.field == "salvage");   // 9097
    m->Set("salvage", "");
    CHECK(m->Get("monthly") == "100.00");
    CHECK(m->Get("bookValue") == "1,200.00");
    r = m->Save();
    REQUIRE(r.ok);
    CHECK(FindAsset(db.code, "E009")->monthly == 10000);
    REQUIRE(m->Delete().ok);
    CHECK(m->rows().size() == 4);
}

TEST_CASE("User Access page: users, and the level's MENUCODE byte for byte (S8-M2)", "[pages]")
{
    CHECK(AccessModel::Allowed("0110", 1) == false);
    CHECK(AccessModel::Allowed("0110", 0));
    CHECK(AccessModel::Allowed("01", 9));                         // past the end: allowed
    CHECK(AccessModel::WithAllowed("01", 9, true) == "01");       // unchanged: not a byte moves
    CHECK(AccessModel::WithAllowed("01", 5, false) == "010001");
    CHECK(AccessModel::WithAllowed("0 1 ", 2, true) == "0 0 ");   // other characters stay

    FixtureDb db;
    UsersModel m(db.code);
    REQUIRE(m.access().rows().size() > 100);
    m.Load();
    REQUIRE(m.rows().size() == 2);
    const std::string original = scalar(db.code, "SELECT menucode FROM accessl WHERE trim(level) = 'B'");
    const std::string sqlA = scalar(db.code, "SELECT menucode FROM accessl WHERE trim(level) = 'A'");

    REQUIRE(m.Select("CLERK"));
    CHECK(m.Get("level") == "B");
    m.ShowLevel("B");
    CHECK(m.levelCode().substr(0, original.size()) == original);
    m.Set("descript", "Data entry clerk");
    REQUIRE(m.Save().ok);   // the commands weren't changed: accessl isn't written
    CHECK(scalar(db.code, "SELECT menucode FROM accessl WHERE trim(level) = 'B'") == original);

    // Row 0 (File > New) grayed for level B, then allowed again: back to the same bytes.
    m.ShowLevel("B");
    m.SetAllowed(0, false);
    CHECK(m.levelChanged());
    RecordModel::Result r = m.Save();
    REQUIRE(r.ok);
    CHECK(r.message == "User CLERK saved, and level B's commands.");
    const std::string grayed = scalar(db.code, "SELECT menucode FROM accessl WHERE trim(level) = 'B'");
    CHECK(grayed[0] == '1');
    CHECK(grayed.substr(1) == original.substr(1));
    m.ShowLevel("B");
    m.SetAllowed(0, true);
    REQUIRE(m.Save().ok);
    CHECK(scalar(db.code, "SELECT menucode FROM accessl WHERE trim(level) = 'B'") == original);

    // A new user at a level accessl hasn't got: the first level's code applies, and nothing is added.
    r = [&] {
        m.New();
        m.Set("username", "TEMP");
        m.Set("level", "C");
        return m.Save();
    }();
    REQUIRE(r.ok);
    m.ShowLevel("C");
    CHECK(m.levelCode() == AccessModel(db.code).CodeOf("A"));
    CHECK_FALSE(m.access().HasLevel("C"));
    CHECK(scalar(db.code, "SELECT menucode FROM accessl WHERE trim(level) = 'A'") == sqlA);
    CHECK(saveNew(m, {{"username", "TEMP"}, {"level", "C"}}).field == "username");   // taken
    REQUIRE(m.Select("TEMP"));
    REQUIRE(m.Delete().ok);
    CHECK(m.rows().size() == 2);

    // Every fixture level saved unchanged through the model (a row toggled and
    // back, so accessl is written): the same string, byte for byte.
    for (const char* user : {"ADMIN", "CLERK"}) {
        REQUIRE(m.Select(user));
        const std::string level = m.Get("level");
        const std::string sql = "SELECT menucode FROM accessl WHERE trim(level) = '" + level + "'";
        const std::string stored = scalar(db.code, sql);
        m.ShowLevel(level);
        m.SetAllowed(3, !AccessModel::Allowed(m.levelCode(), 3));
        m.SetAllowed(3, !AccessModel::Allowed(m.levelCode(), 3));
        CHECK(m.levelChanged());
        r = m.Save();
        INFO(user);
        REQUIRE(r.ok);
        CHECK(r.message.find("commands") != std::string::npos);   // written
        CHECK(scalar(db.code, sql) == stored);
    }
}
