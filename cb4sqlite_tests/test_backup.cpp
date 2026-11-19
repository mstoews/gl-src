//
// Layer-1 tests: Code4::backupTo (spec #2 5.3).
//

#include "TestDb.h"

#include <atomic>
#include <filesystem>
#include <thread>

using namespace test;
using Catch::Matchers::ContainsSubstring;
namespace fs = std::filesystem;

TEST_CASE("backupTo copies the database, including the changed record in a buffer", "[backup]")
{
    TempDb db, copy;
    Code4 code;
    db.open(code);
    createAccounts(code);
    Data4 d(code, "accts");
    addSampleAccounts(d);
    REQUIRE(d.SetIndex("accounts") == r4success);
    REQUIRE(d.seek("100    01") == r4success);
    Field4(d, "descript").assign("Cash edited");   // still only in the buffer

    code.backupTo(copy.path().c_str());
    CHECK_FALSE(fs::exists(copy.path() + ".part"));

    Code4 other;
    REQUIRE(other.open(copy.path().c_str()) == r4success);
    CHECK(raw(other, "SELECT count(*) FROM accts") == "5");
    CHECK(raw(other, "SELECT rtrim(descript) FROM accts WHERE account LIKE '100%' AND dept = '01 '") == "Cash edited");
    Data4 o(other, "accts");                 // tags and field info came along
    REQUIRE(o.SetIndex("accounts") == r4success);
    CHECK(walk(o) == std::vector<std::string>{"100/01", "100/02", "200/01", "300/01", "400/01"});
}

TEST_CASE("backupTo replaces an existing file", "[backup]")
{
    TempDb db, copy;
    Code4 code;
    db.open(code);
    createAccounts(code);
    {
        Data4 d(code, "accts");
        addSampleAccounts(d);
    }
    code.backupTo(copy.path().c_str());
    {
        Data4 d(code, "accts");
        addAccount(d, "900", "01", "Later", 0);
    }
    code.backupTo(copy.path().c_str());
    Code4 other;
    REQUIRE(other.open(copy.path().c_str()) == r4success);
    CHECK(raw(other, "SELECT count(*) FROM accts") == "6");
}

TEST_CASE("backupTo failures leave no file behind", "[backup]")
{
    TempDb db;
    Code4 code;
    db.open(code);
    createAccounts(code);
    const fs::path dir = fs::temp_directory_path() / "cb4sqlite_no_such_dir_xyz";
    const std::string target = (dir / "copy.db").string();
    CHECK_THROWS_AS(code.backupTo(target.c_str()), cb4sqlite::Error);
    CHECK_FALSE(fs::exists(target));
    CHECK_FALSE(fs::exists(target + ".part"));

    CHECK_THROWS_WITH(code.backupTo(""), ContainsSubstring("no file name"));
    code.begin();
    CHECK_THROWS_WITH(code.backupTo((fs::temp_directory_path() / "x.db").string().c_str()),
                      ContainsSubstring("transaction is open"));
    code.rollback();
    Code4 closed;
    CHECK_THROWS_WITH(closed.backupTo("x.db"), ContainsSubstring("no database is open"));
}

// ---- WAL (spec #8 D8, plan S8-M4 task 2) ----------------------------------------------

TEST_CASE("WAL: a second connection reads while the first writes (S8-M4)", "[backup][wal]")
{
    TempDb db;
    Code4 writer, reader;
    db.open(writer);
    createAccounts(writer);
    {
        Data4 d(writer, "accts");
        addSampleAccounts(d);
    }
    db.open(reader);
    writer.begin();
    {
        Data4 d(writer, "accts");
        addAccount(d, "900", "01", "In a transaction", 0);
    }
    // The reader isn't blocked, and sees the last committed state.
    CHECK(raw(reader, "SELECT count(*) FROM accts") == "5");
    writer.commit();
    CHECK(raw(reader, "SELECT count(*) FROM accts") == "6");
    // A second writer waits for the first (busy_timeout) rather than failing.
    writer.begin();
    {
        Data4 d(writer, "accts");
        addAccount(d, "901", "01", "First", 0);
    }
    std::thread release([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        writer.commit();
    });
    {
        Data4 d(reader, "accts");
        addAccount(d, "902", "01", "Second, after waiting", 0);
    }
    release.join();
    CHECK(raw(reader, "SELECT count(*) FROM accts") == "8");
}

TEST_CASE("WAL: a backup taken while a job writes is one file and passes integrity_check (S8-M4, R3)", "[backup][wal]")
{
    TempDb db, copy;
    Code4 code;
    db.open(code);
    createAccounts(code);
    {
        Data4 d(code, "accts");
        addSampleAccounts(d);
    }
    std::atomic<bool> stop{false};
    std::atomic<int> written{0};
    std::thread job([&] {   // a background job on its own connection
        Code4 own;
        REQUIRE(own.open(db.path().c_str()) == r4success);
        Data4 d(own, "accts");
        for (int i = 0; !stop; ++i) {
            own.begin();
            addAccount(d, std::to_string(1000 + i % 9000).c_str(), "01", "Job", i);
            own.commit();
            ++written;
        }
    });
    while (written < 20) std::this_thread::yield();
    code.backupTo(copy.path().c_str());
    stop = true;
    job.join();
    CHECK_FALSE(fs::exists(copy.path() + "-wal"));
    // As copied, before anything opens it: rollback-journal mode, one file.
    sqlite3* raw3 = nullptr;
    REQUIRE(sqlite3_open_v2(copy.path().c_str(), &raw3, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(raw3, "PRAGMA journal_mode", -1, &st, nullptr);
    REQUIRE(sqlite3_step(st) == SQLITE_ROW);
    CHECK(std::string(reinterpret_cast<const char*>(sqlite3_column_text(st, 0))) == "delete");
    sqlite3_finalize(st);
    sqlite3_close(raw3);
    Code4 other;   // (Code4::open turns WAL on for its own use)
    REQUIRE(other.open(copy.path().c_str()) == r4success);
    CHECK(raw(other, "PRAGMA integrity_check") == "ok");
    const int rows = std::stoi(raw(other, "SELECT count(*) FROM accts"));
    CHECK(rows >= 25);
    CHECK(rows <= 5 + written);
}
