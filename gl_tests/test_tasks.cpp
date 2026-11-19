//
// gl_tests: background jobs and the task pages' models (spec #8 5.3, 9;
// plan S8-M4 task 8): the job state machine, one writing job at a time,
// cancelling at a voucher boundary, and each task compared table by table
// with the classic screen's call on a second fixture copy.
//
#include "GlTestDb.h"

#include "Accounts.h"
#include "Ap.h"
#include "Ar.h"
#include "Assets.h"
#include "Vouchers.h"
#include "model/JobState.h"
#include "pages/TaskModels.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace gltest;
namespace m = fx::model;

namespace {

std::string value(const Field4& f)
{
    const std::string t = TrimText(f.str());
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (t.empty() || (end && *end == '\0' && v == 0.0)) return "";
    return t;
}

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

void same(Code4& a, Code4& b, std::initializer_list<const char*> tables)
{
    for (const char* t : tables) {
        INFO(t);
        CHECK(snapshot(a, t) == snapshot(b, t));
    }
}

TaskRun runAll(TaskModel& task, Code4& code)
{
    std::vector<TaskRow> rows = task.Load(code);
    return RunTask(task, code, rows);
}

// A balanced two-line GL voucher, as Voucher Entry saves one.
long balancedVoucher(Code4& code, double amount)
{
    VoucherHeaders h(code);
    VoucherLines l(code);
    VoucherHeader header;
    header.mode = voucher_mode::kOpen;
    header.description = "Task test";
    header.date = "20261020";
    header.period = 7;
    VoucherLine a;
    a.line = 1;
    a.period = 7;
    a.date = "20261020";
    a.reference = "T";
    a.account = "101000";
    a.debit = amount;
    long n = SaveLine(h, l, 0, header, a, 0);
    VoucherLine b = a;
    b.line = 2;
    b.account = "102101";
    b.debit = 0;
    b.credit = amount;
    SaveLine(h, l, n, header, b, 0);
    return n;
}

} // namespace

TEST_CASE("Job state: queued, running, cancelling, done, failed, cancelled (S8-M4)", "[tasks]")
{
    m::JobState s;
    CHECK(s.phase() == m::JobPhase::Queued);
    CHECK(s.active());
    CHECK_FALSE(s.Progress(1, 2));   // not running yet
    CHECK(s.Start());
    CHECK_FALSE(s.Start());
    CHECK(s.Progress(1, 3, "Voucher 101"));
    CHECK(s.done() == 1);
    CHECK(s.of() == 3);
    CHECK(s.Finish(true, false, "3 done."));
    CHECK(s.phase() == m::JobPhase::Done);
    CHECK_FALSE(s.active());
    CHECK_FALSE(s.Cancel());

    m::JobState c;
    c.Start();
    CHECK(c.Cancel());
    CHECK(c.phase() == m::JobPhase::Cancelling);
    CHECK(c.cancelRequested());
    CHECK(c.Progress(2, 3));   // still reporting while it reaches the boundary
    CHECK(c.Finish(true, true, "stopped"));
    CHECK(c.phase() == m::JobPhase::Cancelled);

    m::JobState late;   // asked to cancel, but it had nothing left: done
    late.Start();
    late.Cancel();
    late.Finish(true, false, "");
    CHECK(late.phase() == m::JobPhase::Done);

    m::JobState f;
    f.Start();
    f.Finish(false, false, "disk full");
    CHECK(f.phase() == m::JobPhase::Failed);
    CHECK(f.message() == "disk full");

    m::JobState q;
    CHECK(q.Cancel());   // cancelled before it started
    CHECK(q.phase() == m::JobPhase::Cancelled);
    CHECK_FALSE(q.Start());
}

TEST_CASE("Job gate: one writing job at a time (S8-M4)", "[tasks]")
{
    REQUIRE_FALSE(m::JobGate::Busy());
    std::string holder;
    CHECK(m::JobGate::TryAcquire("Post GL vouchers"));
    CHECK(m::JobGate::Busy());
    CHECK_FALSE(m::JobGate::TryAcquire("Year end", &holder));
    CHECK(holder == "Post GL vouchers");
    m::JobGate::Release("Year end");   // not the holder: nothing
    CHECK(m::JobGate::Busy());
    m::JobGate::Release("Post GL vouchers");
    CHECK_FALSE(m::JobGate::Busy());
    CHECK(m::JobGate::TryAcquire("Year end"));
    m::JobGate::Release("Year end");
}

TEST_CASE("Post task: cancelled after one voucher, exactly one is posted (S8-M4)", "[tasks]")
{
    FixtureDb db;
    const long a = balancedVoucher(db.code, 10), b = balancedVoucher(db.code, 20);
    auto task = MakePostTask(VoucherKind::Gl);
    std::vector<TaskRow> rows = task->Load(db.code);
    int postable = 0;
    for (const TaskRow& r : rows)
        if (r.checked && r.selectable) ++postable;
    REQUIRE(postable == 4);   // 101, 103 (an adjustment), and the two new ones; 102 is out of balance, unchecked
    const TaskRun run = RunTask(*task, db.code, rows, [](int done, int) { return done < 1; });   // cancel after the first
    CHECK(run.stoppedEarly);
    CHECK(run.done == 1);
    CHECK(run.of == 4);
    VoucherHeaders h(db.code);
    int posted = 0;
    for (long n : {101L, 103L, a, b}) {
        REQUIRE(SeekVoucher(h, n));
        if (ReadHeader(h).mode == voucher_mode::kPosted) ++posted;
    }
    CHECK(posted == 1);
    CHECK(run.message.find("stopped") != std::string::npos);
}

TEST_CASE("Spec #2 on task pages: posting, recover and year end, as the classic calls (S8-M4)", "[tasks]")
{
    FixtureDb page, classic;
    {
        auto post = MakePostTask(VoucherKind::Gl);
        std::vector<TaskRow> rows = post->Load(page.code);
        const auto r102 = std::find_if(rows.begin(), rows.end(), [](const TaskRow& r) { return r.key == 102; });
        REQUIRE(r102 != rows.end());
        CHECK_FALSE(r102->checked);
        CHECK(r102->note.find("Out of balance") != std::string::npos);
        const TaskRun run = RunTask(*post, page.code, rows);
        CHECK(run.done == 2);   // 101 and 103
        PostAll(classic.code);  // Post's "all": 102 refused
        same(page.code, classic.code, {"gl_btch", "gl_btchd", "gl_dist", "glaccts"});
    }
    {
        auto recover = MakeRecoverTask(VoucherKind::Gl);
        std::vector<TaskRow> rows = recover->Load(page.code);
        for (TaskRow& r : rows) r.checked = r.key == 101;
        CHECK(RunTask(*recover, page.code, rows).done == 1);
        CHECK(RecoverVoucher(classic.code, 101) == RecoverResult::Recovered);
        same(page.code, classic.code, {"gl_btch", "gl_btchd", "gl_dist", "glaccts"});
    }
    {
        auto yearEnd = MakeYearEndTask();
        const TaskRun run = runAll(*yearEnd, page.code);
        INFO(run.message);
        CHECK(run.ok);
        CHECK(YearEnd(classic.code).closed);
        same(page.code, classic.code, {"gl_btch", "gl_btchd", "gl_dist", "gl_distb", "glaccts"});
    }
}

TEST_CASE("Spec #4 on a task page: the depreciation run (S8-M4)", "[tasks]")
{
    FixtureDb page, classic;
    auto run = MakeDepreciationTask(0);
    run->option = "7";
    std::vector<TaskRow> rows = run->Load(page.code);
    CHECK_FALSE(rows.empty());
    const TaskRun r = RunTask(*run, page.code, rows);
    INFO(r.message);
    CHECK(r.ok);
    CHECK(r.message.find("GL voucher") != std::string::npos);
    RunDepreciation(classic.code, 7, 0);
    same(page.code, classic.code, {"gl_fa", "gl_fadep", "gl_btch", "gl_btchd"});
}

TEST_CASE("Spec #5 on task pages: posting, cheques and the purge (S8-M4)", "[tasks]")
{
    FixtureDb page, classic;
    auto post = MakePostTask(VoucherKind::ApInvoice);
    CHECK(runAll(*post, page.code).done == 2);   // invoices 1 and 2
    PostApVouchers(classic.code, ApKind::Invoice);
    same(page.code, classic.code, {"ap_btch", "ap_dist", "ap_jsum"});

    auto cheques = MakeCreateChequesTask("20261031");
    cheques->option = "20261231";
    const TaskRun made = runAll(*cheques, page.code);
    INFO(made.message);
    CHECK(made.ok);
    const ApAccounts setat = LoadApAccounts(classic.code);
    REQUIRE_FALSE(setat.banks.empty());
    CreateCheques(classic.code, "20261231", "20261031", setat.banks.front().account, NextChequeNumber(classic.code));
    same(page.code, classic.code, {"ap_dist", "ap_reqst", "ap_jsum"});

    auto purge = MakePurgePayablesTask();
    purge->option = "20270101";
    runAll(*purge, page.code);
    PurgePaid(classic.code, "20270101");
    same(page.code, classic.code, {"ap_dist", "ap_reqst"});
}

TEST_CASE("Spec #6 on task pages: the transfer and the purge (S8-M4)", "[tasks]")
{
    FixtureDb page, classic;
    auto transfer = MakeTransferTask(0);
    const TaskRun t = runAll(*transfer, page.code);
    INFO(t.message);
    CHECK(t.ok);
    TransferAp(classic.code, 0);
    TransferAr(classic.code, 0);
    same(page.code, classic.code, {"ap_jsum", "ar_jsum", "gl_btch", "gl_btchd"});

    auto purge = MakePurgeReceivablesTask();
    purge->option = "20270101";
    runAll(*purge, page.code);
    PurgeReceived(classic.code, "20270101");
    same(page.code, classic.code, {"ar_dist", "ar_rcpt"});
}

TEST_CASE("Backup task: a copy that opens and passes integrity_check (S8-M4)", "[tasks]")
{
    FixtureDb db;
    const auto dir = std::filesystem::temp_directory_path() / "gl_tests_backup_task";
    std::filesystem::create_directories(dir);
    const auto file = dir / "company.db";
    db.code.backupTo(file.string().c_str());
    Code4 company;
    REQUIRE(company.open(file.string().c_str()) == r4success);
    auto backup = MakeBackupTask(file.string(), "20261119");
    std::vector<TaskRow> rows = backup->Load(company);
    REQUIRE(rows.size() == 1);
    const TaskRun r = RunTask(*backup, company, rows);
    INFO(r.message);
    REQUIRE(r.ok);
    company.close();
    const std::string copy = rows[0].cells[1];
    Code4 other;
    REQUIRE(other.open(copy.c_str()) == r4success);
    cb4sqlite::Query q(other, "PRAGMA integrity_check");
    REQUIRE(q.next());
    CHECK(q.text(0) == "ok");
    other.close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
