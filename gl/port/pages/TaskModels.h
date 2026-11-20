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
constexpr unsigned kCompany = 128;   // the company profile and its periods: the frame's status bar (spec #9 S9-M2)
constexpr unsigned kRights = 256;    // accessm / accessl: the frame reads the rights again (Restore, spec #9 S9-M4)
constexpr unsigned kAll = 511;       // every table: a restored company (S9-M4)
}

enum class VoucherKind { Gl, ApInvoice, ApCash, Ar };
enum class TaskOption { None, Date, Period };   // a date (yyyymmdd) or a period number the task needs

// Create Cheques' further options (spec #8 G8): the payment date, the bank the
// cheques are drawn on and the first cheque's number, as the classic screen
// asks them. DefaultPaymentOptions fills them as that screen does: today, the
// first bank, NextChequeNumber.
struct PaymentOptions {
    std::string payDate;                                      // yyyymmdd
    std::vector<std::pair<std::string, std::string>> banks;   // the bank's short name, what the list shows
    std::string bank;                                         // a short name from banks
    long firstNumber = 0;
};

class TaskModel {
public:
    virtual ~TaskModel() = default;

    virtual std::string Title() const = 0;    // "Post GL vouchers"
    virtual std::string Action() const = 0;   // "Post"
    virtual std::vector<std::pair<std::string, int>> Columns() const = 0;   // heading, width at 96 DPI
    virtual bool PerItem() const { return true; }
    // The rows have check boxes: every per-item task, and Create Cheques
    // (whole, but its boxes pay or hold each invoice).
    virtual bool Checks() const { return PerItem(); }
    virtual bool Writes() const { return true; }
    virtual unsigned Groups() const = 0;
    virtual TaskOption Option() const { return TaskOption::None; }
    virtual std::string OptionLabel() const { return {}; }
    // Whether the option changes what's listed (a new date lists again, clearing the checks); Bank
    // Reconciliation's date doesn't (S9-M3).
    virtual bool OptionFiltersList() const { return true; }
    std::string option;   // as the page sets it: yyyymmdd, or the period's number
    virtual bool HasPaymentOptions() const { return false; }
    virtual PaymentOptions DefaultPaymentOptions(cb4sqlite::Code4& /*code*/) const { return {}; }
    PaymentOptions payment;   // as the page sets it (HasPaymentOptions)

    virtual std::vector<TaskRow> Load(cb4sqlite::Code4& code) = 0;
    // Per item: one row. Whole: every checked row at once (most ignore them).
    virtual TaskStep RunItem(cb4sqlite::Code4& /*code*/, const TaskRow& /*row*/) { return {}; }
    virtual TaskStep RunAll(cb4sqlite::Code4& /*code*/, const std::vector<TaskRow>& /*checked*/) { return {}; }
    // "3 vouchers listed, 2 checked."
    virtual std::string Summary(const std::vector<TaskRow>& rows) const;
    // What a confirmation names before running ("Post 2 vouchers?"), and what it adds below that
    // ("" for the page's own words about running in the background).
    std::string Question(const std::vector<TaskRow>& rows) const;
    virtual std::string QuestionNote() const { return {}; }
    virtual std::string Noun(int n) const { return n == 1 ? "item" : "items"; }

    // ---- a command on the selected row (spec #9 S9-M3) ----
    // Renumber a spoiled cheque, void one. Run at once on the page's connection
    // (one cheque, one transaction), not as a job; the list is loaded again after.
    virtual std::string RowAction() const { return {}; }                           // the button ("&Void"); "" for none
    virtual std::string RowQuestion(const TaskRow& /*row*/) const { return {}; }   // asked first; "" asks nothing
    virtual TaskStep RunRowAction(cb4sqlite::Code4& /*code*/, const TaskRow& /*row*/) { return {false, ""}; }
    // A number the row command takes (Renumber's new number), beside the buttons.
    virtual std::string RowInputLabel() const { return {}; }   // "" for none
    virtual long RowInputDefault(cb4sqlite::Code4& /*code*/) const { return 0; }
    std::string rowInput;   // as the page sets it
    // Another command the page offers (Print cheques: 4091, its preview); 0 for none.
    virtual unsigned short PageCommand() const { return 0; }
    virtual std::string PageCommandLabel() const { return {}; }

    // ---- Restore (spec #9 D7, S9-M4) ----
    // The action button is the row command (on the selected row, on the page, not a job).
    virtual bool ActionOnRow() const { return false; }
    // The row command replaces the company file: the workspace's other pages close first.
    virtual bool ClosesOtherPages() const { return false; }
    // A file the page lets the user choose (Browse...), added to the list and selected.
    virtual std::string BrowseLabel() const { return {}; }   // "" for none
    virtual int AddFile(cb4sqlite::Code4& /*code*/, const std::string& /*utf8Path*/) { return -1; }   // its row, after Load
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
// Create Cheques (TAPCreatePayment): every open item, checked when this run
// would pay it (due by the option's date, or forced). Checking a held or
// not-yet-due invoice releases or forces it; unchecking one holds it (H), as
// the classic Hold/Force does; then the cheques are made from `payment`. A run
// that's refused puts the holds back as they were.
std::unique_ptr<TaskModel> MakeCreateChequesTask(const std::string& payDate);
std::unique_ptr<TaskModel> MakeBackupTask(const std::string& databaseUtf8, const std::string& todayYyyymmdd);
// The cheque cycle (spec #9 D5, S9-M3): the made cheques, each checked to be
// confirmed printed (ConfirmPrinted, one cheque at a time); Renumber on a row
// (RenumberCheque, the new number beside it, NextChequeNumber to start);
// Print cheques (4091) opens the cheques' preview.
std::unique_ptr<TaskModel> MakeChequesTask();
// Bank Reconciliation (spec #9 D6, S9-M3): the cheques not cleared; the
// checked printed ones cleared on the option's date (ClearCheque, one at a
// time); a made one listed with why it can't be; Void on a row (VoidCheque,
// dated by the same date), asked first.
std::unique_ptr<TaskModel> MakeBankReconciliationTask();
// Restore Databases (spec #9 D7, S9-M4): the backups beside the company file
// `currentUtf8` (newest first), and any added by Browse; each checked with
// CheckRestoreFile, a file that fails listed with why. Restore (on the
// selected file, asked first, with the consequence) is RestoreDatabase on the
// page's connection, after the page has closed the others.
std::unique_ptr<TaskModel> MakeRestoreTask(const std::string& currentUtf8);
