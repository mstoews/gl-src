//
// gl_tests: the Spec #9 acceptance figures (plan S9-M5 task 2): the steps of
// the Spec #2 sheet that moved to Spec #9's pages, in the sheet's order on one
// copy of the fixture, through the page models, with every figure the sheet
// quotes (gl/docs/superpowers/specs/2026-11-08-spec2-acceptance.md, #10, #12,
// #13). The Spec #5 sheet's cheque steps (#7, #8) on the Cheques and Bank
// Reconciliation pages are a section of "The Spec #5 acceptance walkthrough's
// figures" (test_ap.cpp), so they run after that sheet's own steps 1-6.
//
#include "GlTestDb.h"

#include "Accounts.h"
#include "CompanyFiles.h"
#include "Vouchers.h"
#include "pages/CompanyModel.h"
#include "pages/RecordModels.h"
#include "pages/TaskModels.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

using namespace gltest;

namespace {

std::string one(Code4& code, const std::string& sql)
{
    cb4sqlite::Query q(code, sql);
    return q.next() ? q.text(0) : std::string("<none>");
}

std::string utf8(const fs::path& p)
{
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

// The company in a folder of its own, as `dev.db` sits beside gl.exe: the
// Restore page lists the files beside it.
struct Company {
    fs::path dir = fs::temp_directory_path() / tempPath("gl_acceptance9").stem();
    fs::path db = dir / "dev.db";
    Code4 code;

    Company()
    {
        fs::create_directories(dir);
        fs::copy_file(masterFixture(), db);
        REQUIRE(OpenCompany(code, db) == r4success);
    }
    ~Company()
    {
        code.close();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    Company(const Company&) = delete;
    Company& operator=(const Company&) = delete;
};

// Voucher 104 as the sheet's #2 and #3 leave it: 102101 Dr 300.00, 410001-10
// Cr 250.00 (MR001 / P100 / DR001), 410001-20 Cr 50.00.
long voucher104(Code4& code)
{
    VoucherHeaders h(code);
    VoucherLines l(code);
    VoucherHeader header;
    header.mode = voucher_mode::kOpen;
    header.description = "ACCEPTANCE 104";
    header.date = "20261020";
    header.period = 7;
    auto line = [](int n, const char* account, const char* dept, double debit, double credit) {
        VoucherLine v;
        v.line = n;
        v.period = 7;
        v.date = "20261020";
        v.reference = "ACCEPTANCE 104";
        v.descript = "Acceptance";
        v.account = account;
        v.dept = dept;
        v.debit = debit;
        v.credit = credit;
        return v;
    };
    VoucherLine b = line(2, "410001", "10", 0, 250);
    b.medrep = "MR001";
    b.prod = "P100";
    b.drcode = "DR001";
    const long n = SaveLine(h, l, 0, header, line(1, "102101", "", 300, 0), 0);
    SaveLine(h, l, n, header, b, 0);
    SaveLine(h, l, n, header, line(3, "410001", "20", 0, 50), 0);
    return n;
}

} // namespace

TEST_CASE("The Spec #9 acceptance figures: the Spec #2 sheet's #10, #12 and #13 on the pages (S9-M5)", "[acceptance9]")
{
    Company c;

    // #2-#5 as the sheet leaves them: voucher 104, then Post all (102 refused, out of balance).
    REQUIRE(voucher104(c.code) == 104);
    const PostResult posted = PostAll(c.code);
    CHECK(std::find(posted.posted.begin(), posted.posted.end(), 104L) != posted.posted.end());
    CHECK(std::find(posted.posted.begin(), posted.posted.end(), 101L) != posted.posted.end());

    // ---- #10 Budget Data: the Chart of Accounts page's Budget tab ----
    auto accounts = MakeAccountsModel(c.code);
    accounts->Load();
    fx::model::EntryGridModel& budget = *accounts->Grid("budget");
    REQUIRE(accounts->Select("410001-10"));
    REQUIRE(budget.Rows() == 12);
    CHECK(budget.Cell(6, "current") == "-850.00");   // "Current Year period 7 at -850.00"
    const std::string others = one(c.code, "SELECT group_concat(budget1 || '/' || budget6 || '/' || budget12, ';') FROM glaccts "
                                           "WHERE NOT (trim(account) = '410001' AND trim(dept) = '10')");
    budget.SetCell(0, "budget", "1,000.00");
    budget.SetCell(5, "budget", "2,000.00");
    budget.SetCell(11, "budget", "3,000.00");
    REQUIRE(accounts->Save().ok);   // Save: no 8098 question
    accounts->New();
    REQUIRE(accounts->Select("410001-10"));   // "Close, reopen, pick 410001 dept 10: the three amounts are there"
    CHECK(budget.Cell(0, "budget") == "1,000.00");
    CHECK(budget.Cell(5, "budget") == "2,000.00");
    CHECK(budget.Cell(11, "budget") == "3,000.00");
    CHECK(one(c.code, "SELECT budget1 || '|' || budget6 || '|' || budget12 FROM glaccts WHERE trim(account) = '410001' "
                      "AND trim(dept) = '10'") == "1000.0|2000.0|3000.0");
    CHECK(one(c.code, "SELECT group_concat(budget1 || '/' || budget6 || '/' || budget12, ';') FROM glaccts "
                      "WHERE NOT (trim(account) = '410001' AND trim(dept) = '10')") == others);   // "No other account's budget changed"

    // ---- #12 Backup and Restore: the Backup and Restore task pages ----
    auto backup = MakeBackupTask(utf8(c.db), "20261120");
    std::vector<TaskRow> rows = backup->Load(c.code);
    REQUIRE(RunTask(*backup, c.code, rows).ok);
    const fs::path copy = c.dir / "dev-20261120.db";   // "suggests dev-<today>.db beside dev.db"
    REQUIRE(fs::exists(copy));
    {
        accounts->New();
        accounts->Set("account", "150002");
        accounts->Set("descript", "AFTER BACKUP");
        accounts->Set("type", "B");
        REQUIRE(accounts->Save().ok);
    }
    auto restore = MakeRestoreTask(utf8(c.db));
    rows = restore->Load(c.code);
    const TaskRow* chosen = nullptr;
    for (const TaskRow& r : rows)
        if (r.cells[0] == "dev-20261120.db") chosen = &r;
    REQUIRE(chosen);
    REQUIRE(chosen->selectable);
    const TaskStep restored = restore->RunRowAction(c.code, *chosen);
    INFO(restored.note);
    REQUIRE(restored.ok);
    CHECK(restored.note == "The company database was restored. The previous one is dev.db.bak.");
    CHECK(one(c.code, "SELECT count(*) FROM glaccts WHERE trim(account) = '150002'") == "0");   // "150002 is gone"
    {
        Code4 bak;
        REQUIRE(OpenCompany(bak, c.dir / "dev.db.bak") == r4success);
        CHECK(one(bak, "SELECT count(*) FROM glaccts WHERE trim(account) = '150002'") == "1");   // "dev.db.bak has it"
        bak.close();
    }
    // "Restore a text file renamed to .db, and a copy of GL.INI: each is refused with a reason"
    std::ofstream(c.dir / "notes.db") << "this is not a database, just some text that is long enough to matter\n";
    const fs::path ini = c.dir / "GL.INI.copy";
    std::ofstream(ini) << "[ACCESS]\nPSW=T\n";
    const int at = restore->AddFile(c.code, utf8(ini));
    rows = restore->Load(c.code);
    REQUIRE(at >= 0);
    for (const TaskRow& r : rows) {
        INFO(r.cells[0]);
        if (r.cells[0] == "notes.db" || static_cast<int>(r.key) == at) {
            CHECK_FALSE(r.selectable);
            CHECK(r.note.rfind("Can't be restored: ", 0) == 0);
            CHECK_FALSE(restore->RunRowAction(c.code, r).ok);
        }
    }
    CHECK(c.code.isOpen());   // "the company stays open"

    // ---- #13 Company Profile: the Accounts and Banks tabs ----
    CompanyModel profile(c.code);
    profile.Load();
    REQUIRE(profile.Select("company"));
    const fx::model::EntryGridModel& banks = *profile.Grid("banks");
    REQUIRE(banks.Lines() == 2);   // "lists the cash accounts (102101 and 101000)"
    CHECK(banks.Cell(0, "account") == "101000");
    CHECK(banks.Cell(1, "account") == "102101");
    CHECK(profile.Grid("accounts")->Rows() == 8);
}
