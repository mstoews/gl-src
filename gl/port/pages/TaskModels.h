//
// TaskModels.h - the task pages' logic, without a window (spec #8 5.3; plan
// S8-M4 task 6). Portable: TaskPages.cpp shows them and runs them as
// background jobs (fx::Job); gl_tests drives them through the sheets' batch
// criteria.
//
// A task lists what it will act on (rows with check boxes), sums it up, and
// has one named action. Load runs on the UI thread with the page's
// connection; Run runs on the job's worker with the job's own connection.
//
//   per item  Post, Cancel and Recover (GL, purchase, cash and sales vouchers),
//             Purge received receivables: each row is one call to the core
//             (its own transaction), so progress counts rows and Cancel stops
//             between two; a posting of three cancelled after one has posted one.
//   whole     Update AR and AP, the depreciation run, year end, Purge paid
//             payables, Create Cheques, Backup: one call to the core (one
//             transaction), so Cancel works only before it starts.
//
// The cores are the classic screens' (Vouchers.h, Ap.h, Ar.h, Assets.h); the
// refusals go on the rows, and the result names what was made ("GL voucher 105").
//
#pragma once

#include "cb4sqlite.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct TaskRow {
    long key = 0;                     // the voucher, the item, the asset
    std::vector<std::string> cells;
    bool checked = true;
    bool selectable = true;           // false: listed with why it can't be done (note)
    std::string note;                 // a refusal, or what was done
};

struct TaskStep {
    bool ok = true;
    std::string note;                 // for the row (per item) or the summary (whole)
};

// The groups of tables a task changes (spec #8 9.3, NotifyDataChanged).
namespace data_group {
constexpr unsigned kGlVouchers = 1, kGlBalances = 2, kAp = 4, kAr = 8, kAssets = 16, kMasters = 32, kReports = 64;
}

enum class VoucherKind { Gl, ApInvoice, ApCash, Ar };
enum class TaskOption { None, Date, Period };   // a date (yyyymmdd) or a period number the task needs

class TaskModel {
public:
    virtual ~TaskModel() = default;

    virtual std::string Title() const = 0;    // "Post GL vouchers"
    virtual std::string Action() const = 0;   // "Post"
    virtual std::vector<std::pair<std::string, int>> Columns() const = 0;   // heading, width at 96 DPI
    virtual bool PerItem() const { return true; }
    virtual bool Writes() const { return true; }
    virtual unsigned Groups() const = 0;
    virtual TaskOption Option() const { return TaskOption::None; }
    virtual std::string OptionLabel() const { return {}; }
    std::string option;   // as the page sets it: yyyymmdd, or the period's number

    virtual std::vector<TaskRow> Load(cb4sqlite::Code4& code) = 0;
    // Per item: one row. Whole: every checked row at once (most ignore them).
    virtual TaskStep RunItem(cb4sqlite::Code4& /*code*/, const TaskRow& /*row*/) { return {}; }
    virtual TaskStep RunAll(cb4sqlite::Code4& /*code*/, const std::vector<TaskRow>& /*checked*/) { return {}; }
    // "3 vouchers listed, 2 checked."
    virtual std::string Summary(const std::vector<TaskRow>& rows) const;
    // What a confirmation names before running ("Post 2 vouchers?").
    std::string Question(const std::vector<TaskRow>& rows) const;
    virtual std::string Noun(int n) const { return n == 1 ? "item" : "items"; }
};

// Runs a task as its job does, on `code`: per item, each checked selectable
// row in order, with `progress(done, of)` after each (false: stop there).
// Notes go on the rows; returns the summary's note. For the job and the tests.
struct TaskRun {
    int done = 0, of = 0;
    bool stoppedEarly = false;
    bool ok = true;
    std::string message;
};
TaskRun RunTask(TaskModel& task, cb4sqlite::Code4& code, std::vector<TaskRow>& rows,
                const std::function<bool(int done, int of)>& progress = {});

std::unique_ptr<TaskModel> MakePostTask(VoucherKind kind);
std::unique_ptr<TaskModel> MakeCancelTask(VoucherKind kind);
std::unique_ptr<TaskModel> MakeRecoverTask(VoucherKind kind);
std::unique_ptr<TaskModel> MakeTransferTask(long iniBatchno);
std::unique_ptr<TaskModel> MakeDepreciationTask(long iniBatchno);
std::unique_ptr<TaskModel> MakeYearEndTask();
std::unique_ptr<TaskModel> MakePurgePayablesTask();
std::unique_ptr<TaskModel> MakePurgeReceivablesTask();
std::unique_ptr<TaskModel> MakeCreateChequesTask(const std::string& payDate);
std::unique_ptr<TaskModel> MakeBackupTask(const std::string& databaseUtf8, const std::string& todayYyyymmdd);
