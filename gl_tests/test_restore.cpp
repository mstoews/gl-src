//
// gl_tests: the Restore task page's model (spec #9 D7; plan S9-M4): the
// backups beside the company file listed and checked as RunRestore checks a
// chosen one, a file added by Browse, the question, and a restore through
// RestoreDatabase on the page's connection, as Backup.cpp's RunRestore did.
//
#include "GlTestDb.h"

#include "Accounts.h"
#include "CompanyFiles.h"
#include "pages/TaskModels.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

using namespace gltest;

namespace {

std::string utf8(const fs::path& p)
{
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

// A folder of its own: the company file, a good backup, a file that isn't a database.
struct Folder {
    fs::path dir = fs::temp_directory_path() / tempPath("gl_restore_page").stem();
    fs::path live = dir / "dev.db", good = dir / "dev-20261120.db", junk = dir / "notes.db";
    Code4 code;

    Folder()
    {
        fs::create_directories(dir);
        fs::copy_file(masterFixture(), live);
        REQUIRE(OpenCompany(code, live) == r4success);
        code.backupTo(good.string().c_str());
        std::ofstream(junk) << "this is not a database, just some text that is long enough to matter\n";
        // A change after the backup: a new account.
        AccountsTable gl(code);
        AccountInput in;
        in.account = "999001";
        in.descript = "After the backup";
        in.type = "B";
        AppendAccount(gl, in);
    }
    ~Folder()
    {
        code.close();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    Folder(const Folder&) = delete;
    Folder& operator=(const Folder&) = delete;
};

// A backup beside the company file is listed by its name.
const TaskRow* rowOf(const std::vector<TaskRow>& rows, const fs::path& p)
{
    for (const TaskRow& r : rows)
        if (!r.cells.empty() && r.cells[0] == utf8(p.filename())) return &r;
    return nullptr;
}

} // namespace

TEST_CASE("Restore page: the backups beside the company file, each checked (S9-M4)", "[restore]")
{
    Folder f;
    auto task = MakeRestoreTask(utf8(f.live));
    CHECK(task->Title() == "Restore");
    CHECK(task->ActionOnRow());
    CHECK(task->ClosesOtherPages());
    CHECK_FALSE(task->Checks());
    std::vector<TaskRow> rows = task->Load(f.code);
    REQUIRE(rows.size() == 2);   // the backup and notes.db; not the company file itself
    CHECK(rowOf(rows, f.live) == nullptr);
    const TaskRow* good = rowOf(rows, f.good);
    const TaskRow* junk = rowOf(rows, f.junk);
    REQUIRE(good);
    REQUIRE(junk);
    CHECK(good->selectable);
    CHECK(good->note.empty());
    CHECK(good->cells[2].find(" KB") != std::string::npos);
    CHECK_FALSE(junk->selectable);
    CHECK(junk->note.rfind("Can't be restored: ", 0) == 0);
    CHECK(task->Summary(rows).rfind("2 backups. Select the one to restore", 0) == 0);

    // The question says what's lost and where the current file goes, as RunRestore asked.
    CHECK(task->RowQuestion(*good) == "Replace the company database with " + utf8(f.good) +
                                          "? Everything entered since that backup will be lost. "
                                          "The current database is kept as dev.db.bak.");
    // A file that can't be restored is refused with why.
    const TaskStep refused = task->RunRowAction(f.code, *junk);
    CHECK_FALSE(refused.ok);
    CHECK(refused.note == junk->note);
}

TEST_CASE("Restore page: restore replaces the company file and keeps it as .bak (S9-M4)", "[restore]")
{
    Folder f;
    auto task = MakeRestoreTask(utf8(f.live));
    const std::vector<TaskRow> rows = task->Load(f.code);
    const TaskRow* good = rowOf(rows, f.good);
    REQUIRE(good);
    const TaskStep s = task->RunRowAction(f.code, *good);
    INFO(s.note);
    REQUIRE(s.ok);
    CHECK(s.note == "The company database was restored. The previous one is dev.db.bak.");
    CHECK(f.code.isOpen());   // on the restored file
    {
        AccountsTable gl(f.code);
        CHECK_FALSE(SeekAccount(gl, "999001", ""));   // entered after the backup: gone
        CHECK(SeekAccount(gl, "340000", ""));
    }
    CHECK(fs::exists(f.dir / "dev.db.bak"));
    {
        Code4 old;
        REQUIRE(OpenCompany(old, f.dir / "dev.db.bak") == r4success);
        AccountsTable gl(old);
        CHECK(SeekAccount(gl, "999001", ""));   // the replaced file, with the new account
        old.close();
    }
    // The .bak is listed now too, as a backup that can be restored.
    const std::vector<TaskRow> after = task->Load(f.code);
    const TaskRow* bak = rowOf(after, f.dir / "dev.db.bak");
    REQUIRE(bak);
    CHECK(bak->selectable);
}

TEST_CASE("Restore page: Browse adds a file from elsewhere (S9-M4)", "[restore]")
{
    Folder f;
    TempFile elsewhere(tempPath("gl_restore_elsewhere"));
    f.code.backupTo(elsewhere.path().string().c_str());
    auto task = MakeRestoreTask(utf8(f.live));
    const int at = task->AddFile(f.code, utf8(elsewhere.path()));
    REQUIRE(at >= 0);
    const std::vector<TaskRow> rows = task->Load(f.code);   // as the page lists again after Browse
    REQUIRE(rows.size() == 3);
    REQUIRE(static_cast<size_t>(at) < rows.size());
    CHECK(rows[static_cast<size_t>(at)].cells[0] == utf8(elsewhere.path()));   // from elsewhere: the whole path
    CHECK(rows[static_cast<size_t>(at)].selectable);
    CHECK(task->AddFile(f.code, utf8(elsewhere.path())) == at);   // added once
}
