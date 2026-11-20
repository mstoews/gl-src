//
// GlVoucherModel.h - the GL voucher page's logic, without a window (spec #8
// 5.2; plan S8-M3 task 3). Portable: GlVoucherPage shows it, and gl_tests
// drives it through Spec #2's entry criteria.
//
// The header (description, date, period, cheque) and the lines in an entry
// grid: account, dept, document, the account's name, debit, credit, medical
// rep, product and doctor. Saving writes through Vouchers.h's SaveLine and
// DeleteLine, the calls Voucher Entry makes, so the gl_btch and gl_btchd rows
// are the same; Post is PostVoucher.
//
// Voucher Entry's checks, per line: a document, an account and dept in the
// chart, and a debit or a credit but not both; for the voucher: a
// description, a date and an open period. What changes (spec #8 D4, D9):
// the whole voucher is typed and saved at once, not a line at a time, and
// there is no "save the current transaction?" (the page asks about unsaved
// changes instead).
//
// Adjustment mode is Prior Period Entry's: new vouchers get mode "3", and
// only adjustment vouchers open.
//
#pragma once

#include "../Vouchers.h"
#include "cb4sqlite.h"
#include "model/EntryGridModel.h"
#include "model/LookupModel.h"

#include <string>
#include <vector>

class GlVoucherModel {
public:
    GlVoucherModel(cb4sqlite::Code4& code, bool adjustment, long iniBatchno);

    bool adjustment() const noexcept { return adjustment_; }
    fx::model::EntryGridModel& grid() noexcept { return grid_; }
    const fx::model::EntryGridModel& grid() const noexcept { return grid_; }

    // ---- the header ----
    long number = 0;              // 0 for a new voucher (nextNumber() shows what it will get)
    std::string description, date, cheque;   // date yyyymmdd
    int period = 0;
    bool isNew() const noexcept { return number == 0; }
    long nextNumber() const;
    std::vector<OpenPeriod> periods() const { return periods_; }
    bool PeriodOpen(int p) const;

    // A new voucher: no lines, the lowest open period, `today` (yyyymmdd).
    void New(const std::string& today);

    enum class Open { Opened, NotFound, Posted, NotAdjustment, WrongMode, NoLines };
    Open Load(long voucher);
    // The open voucher before (step -1) or after (1) this one, in this page's mode; 0 if none.
    long Neighbour(int step) const;

    struct Result {
        bool ok = false;
        std::string field;   // a header field: "description", "date", "period"; or "" with a cell
        int row = -1;        // a grid row, with `column`
        std::string column;
        std::string message;
    };
    Result Save();
    Result Delete();
    // Saved, balanced, and in an open period.
    bool CanPost() const;
    Result Post();

    // The chart and the code tables, for the grid's lookups.
    const fx::model::LookupModel& accounts() const noexcept { return accounts_; }
    const fx::model::LookupModel& reps() const noexcept { return reps_; }
    const fx::model::LookupModel& products() const noexcept { return products_; }
    const fx::model::LookupModel& doctors() const noexcept { return doctors_; }
    std::string AccountName(const std::string& account, const std::string& dept) const;

private:
    void LoadLookups();
    std::vector<VoucherLine> Lines(Result& refusal) const;

    cb4sqlite::Code4& code_;
    bool adjustment_;
    long iniBatchno_;
    fx::model::EntryGridModel grid_;
    std::vector<OpenPeriod> periods_;
    fx::model::LookupModel accounts_, reps_, products_, doctors_;
    int savedLines_ = 0;   // the voucher's lines on file
};
