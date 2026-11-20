//
// TaskModels.cpp - see TaskModels.h.
//
#include "TaskModels.h"

#include "../Accounts.h"   // TrimText
#include "../Ap.h"
#include "../Ar.h"
#include "../Assets.h"
#include "../CompanyFiles.h"
#include "../Vouchers.h"
#include "model/Validate.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <filesystem>

namespace m = fx::model;

namespace {

std::string money(std::int64_t cents) { return m::FormatAmount(cents); }
std::string moneyOf(double v) { return m::FormatAmount(static_cast<std::int64_t>(std::llround(v * 100))); }

std::string dateText(const std::string& yyyymmdd)
{
    return yyyymmdd.size() == 8 ? yyyymmdd.substr(4, 2) + "/" + yyyymmdd.substr(6, 2) + "/" + yyyymmdd.substr(0, 4) : yyyymmdd;
}

const char* kindName(VoucherKind k)
{
    switch (k) {
    case VoucherKind::Gl: return "GL";
    case VoucherKind::ApInvoice: return "purchase";
    case VoucherKind::ApCash: return "cash";
    case VoucherKind::Ar: return "sales";
    }
    return "";
}

unsigned groupsOf(VoucherKind k)
{
    using namespace data_group;
    switch (k) {
    case VoucherKind::Gl: return kGlVouchers | kGlBalances | kReports;
    case VoucherKind::ApInvoice:
    case VoucherKind::ApCash: return kAp | kReports;
    case VoucherKind::Ar: return kAr | kReports;
    }
    return 0;
}

// The vouchers of a kind in some modes, as rows: number, date, who / what, amount.
std::vector<TaskRow> voucherRows(cb4sqlite::Code4& code, VoucherKind kind, bool posted)
{
    std::vector<TaskRow> rows;
    switch (kind) {
    case VoucherKind::Gl: {
        VoucherHeaders h(code);
        const std::vector<std::string> modes =
            posted ? std::vector<std::string>{voucher_mode::kPosted}
                   : std::vector<std::string>{voucher_mode::kOpen, voucher_mode::kAdjustment};
        for (const VoucherHeader& v : ListVouchers(h, modes)) {
            TaskRow r;
            r.key = v.number;
            r.cells = {std::to_string(v.number), dateText(v.date), TrimText(v.description), moneyOf(v.debit)};
            if (!posted && !Balanced(v.debit, v.credit)) {   // Post refuses it (string 8116)
                r.checked = false;
                r.note = "Out of balance by " + moneyOf(std::fabs(v.debit - v.credit)) + ".";
            }
            rows.push_back(std::move(r));
        }
        break;
    }
    case VoucherKind::ApInvoice:
    case VoucherKind::ApCash:
        for (const ApVoucher& v : LoadApVouchers(code, kind == VoucherKind::ApCash ? ApKind::Cash : ApKind::Invoice)) {
            if (v.mode != (posted ? ap_mode::kPosted : ap_mode::kOpen)) continue;
            rows.push_back({v.number,
                            {std::to_string(v.number), dateText(v.invoiceDate),
                             TrimText(v.vendor) + "  " + TrimText(v.vendorName) + ", " + TrimText(v.invoiceNo), money(v.payable)}});
        }
        break;
    case VoucherKind::Ar:
        for (const ArVoucher& v : LoadArVouchers(code)) {
            if (v.mode != (posted ? ar_mode::kPosted : ar_mode::kOpen)) continue;
            rows.push_back({v.number,
                            {std::to_string(v.number), dateText(v.invoiceDate),
                             TrimText(v.customer) + ", " + TrimText(v.invoiceNo), money(v.total)}});
        }
        break;
    }
    return rows;
}

std::string apRefusal(const ApPostResult& r, long n)
{
    for (const ApRefusal& f : r.refused)
        if (f.number == n) switch (f.reason) {
            case ApPostRefusal::OutOfBalance: return "Out of balance.";
            case ApPostRefusal::NoLines: return "It has no lines.";
            case ApPostRefusal::UnknownAccount:
                return "Account " + f.account.account + (f.account.dept.empty() ? "" : "-" + f.account.dept) + " isn't in the chart.";
            }
    return "Not posted.";
}

class VoucherTask : public TaskModel {
public:
    explicit VoucherTask(VoucherKind kind) : kind_(kind) {}
    std::vector<std::pair<std::string, int>> Columns() const override
    {
        return {{"Voucher", 64}, {"Date", 80}, {kind_ == VoucherKind::Gl ? "Description" : "Who and invoice", 260}, {"Amount", 100}};
    }
    unsigned Groups() const override { return groupsOf(kind_); }
    std::string Noun(int n) const override { return std::string(kindName(kind_)) + (n == 1 ? " voucher" : " vouchers"); }

protected:
    VoucherKind kind_;
};

class PostTask : public VoucherTask {
public:
    using VoucherTask::VoucherTask;
    std::string Title() const override { return std::string("Post ") + kindName(kind_) + " vouchers"; }
    std::string Action() const override { return "Post"; }
    std::vector<TaskRow> Load(cb4sqlite::Code4& code) override { return voucherRows(code, kind_, false); }
    TaskStep RunItem(cb4sqlite::Code4& code, const TaskRow& row) override
    {
        const long n = row.key;
        switch (kind_) {
        case VoucherKind::Gl: {
            const PostResult r = PostVoucher(code, n);
            if (std::find(r.posted.begin(), r.posted.end(), n) != r.posted.end()) return {true, "Posted."};
            for (const VoucherRefusal& f : r.refused)
                if (f.number == n)
                    return {false, f.reason == PostRefusal::OutOfBalance ? "Out of balance."
                                                                          : "Account " + f.account + (f.dept.empty() ? "" : "-" + f.dept) + " isn't in the chart."};
            return {false, "Not posted."};
        }
        case VoucherKind::ApInvoice:
        case VoucherKind::ApCash: {
            const ApPostResult r = PostApVouchers(code, kind_ == VoucherKind::ApCash ? ApKind::Cash : ApKind::Invoice, n);
            if (std::find(r.posted.begin(), r.posted.end(), n) != r.posted.end()) return {true, "Posted."};
            return {false, apRefusal(r, n)};
        }
        case VoucherKind::Ar: {
            const ApPostResult r = PostArVouchers(code, n);
            if (std::find(r.posted.begin(), r.posted.end(), n) != r.posted.end()) return {true, "Posted."};
            return {false, apRefusal(r, n)};
        }
        }
        return {false, ""};
    }
};

class CancelTask : public VoucherTask {
public:
    using VoucherTask::VoucherTask;
    std::string Title() const override { return std::string("Cancel ") + kindName(kind_) + " vouchers"; }
    std::string Action() const override { return "Cancel vouchers"; }
    std::vector<TaskRow> Load(cb4sqlite::Code4& code) override
    {
        std::vector<TaskRow> rows = voucherRows(code, kind_, false);
        for (TaskRow& r : rows) {   // nothing is cancelled unless it's checked
            r.checked = false;
            if (kind_ == VoucherKind::Gl) r.note.clear();
        }
        return rows;
    }
    TaskStep RunItem(cb4sqlite::Code4& code, const TaskRow& row) override
    {
        const long n = row.key;
        if (kind_ == VoucherKind::Gl) switch (CancelVoucher(code, n)) {
            case CancelResult::Cancelled: return {true, "Cancelled."};
            case CancelResult::Posted: return {false, "It has been posted."};
            case CancelResult::NotFound: return {false, "No longer on file."};
            }
        const ApCancelResult r = kind_ == VoucherKind::Ar ? CancelArVoucher(code, n)
                                                          : CancelApVoucher(code, kind_ == VoucherKind::ApCash ? ApKind::Cash : ApKind::Invoice, n);
        switch (r) {
        case ApCancelResult::Cancelled: return {true, "Cancelled."};
        case ApCancelResult::NotOpen: return {false, "It isn't open."};
        case ApCancelResult::NotFound: return {false, "No longer on file."};
        }
        return {false, ""};
    }
};

class RecoverTask : public VoucherTask {
public:
    using VoucherTask::VoucherTask;
    std::string Title() const override { return std::string("Recover ") + kindName(kind_) + " vouchers"; }
    std::string Action() const override { return "Recover"; }
    std::vector<TaskRow> Load(cb4sqlite::Code4& code) override
    {
        std::vector<TaskRow> rows = voucherRows(code, kind_, true);
        for (TaskRow& r : rows) r.checked = false;
        return rows;
    }
    TaskStep RunItem(cb4sqlite::Code4& code, const TaskRow& row) override
    {
        const long n = row.key;
        switch (kind_) {
        case VoucherKind::Gl: {
            std::string account, dept;
            switch (RecoverVoucher(code, n, &account, &dept)) {
            case RecoverResult::Recovered: return {true, "Recovered: open again."};
            case RecoverResult::NotPosted: return {false, "It isn't posted."};
            case RecoverResult::UnknownAccount: return {false, "Account " + account + (dept.empty() ? "" : "-" + dept) + " isn't in the chart."};
            }
            break;
        }
        case VoucherKind::ApInvoice:
        case VoucherKind::ApCash:
            switch (RecoverApVoucher(code, kind_ == VoucherKind::ApCash ? ApKind::Cash : ApKind::Invoice, n)) {
            case ApRecoverResult::Recovered: return {true, "Recovered: open again."};
            case ApRecoverResult::NotPosted: return {false, "It isn't posted."};
            case ApRecoverResult::Transferred: return {false, "Its journal is in a GL voucher: cancel that first."};
            case ApRecoverResult::Paid: return {false, "Its invoice has been paid."};
            }
            break;
        case VoucherKind::Ar:
            switch (RecoverArVoucher(code, n)) {
            case ArRecoverResult::Recovered: return {true, "Recovered: open again."};
            case ArRecoverResult::NotPosted: return {false, "It isn't posted."};
            case ArRecoverResult::Transferred: return {false, "Its journal is in a GL voucher: cancel that first."};
            case ArRecoverResult::Received: return {false, "A receipt has been entered against it."};
            }
            break;
        }
        return {false, ""};
    }
};

// ---- whole tasks ----------------------------------------------------------------------

class TransferTask : public TaskModel {
public:
    explicit TransferTask(long ini) : ini_(ini) {}
    std::string Title() const override { return "Update AR and AP"; }
    std::string Action() const override { return "Update the GL"; }
    bool PerItem() const override { return false; }
    unsigned Groups() const override { return data_group::kGlVouchers | data_group::kAp | data_group::kAr | data_group::kReports; }
    std::vector<std::pair<std::string, int>> Columns() const override { return {{"Journal", 160}, {"Waiting", 300}}; }
    std::string Noun(int n) const override { return n == 1 ? "journal" : "journals"; }
    std::vector<TaskRow> Load(cb4sqlite::Code4& code) override
    {
        auto waiting = [&](const char* table) {
            try {
                cb4sqlite::Query q(code, std::string("SELECT count(*) FROM ") + table +
                                             " WHERE _deleted = 0 AND trim(coalesce(mode, '')) IN ('', '1')");
                return q.next() ? q.text(0) : std::string("0");
            } catch (const std::exception&) {
                return std::string("?");
            }
        };
        return {{1, {"Purchases (AP)", waiting("ap_jsum") + " journal rows waiting"}},
                {2, {"Sales and receipts (AR)", waiting("ar_jsum") + " journal rows waiting"}}};
    }
    TaskStep RunAll(cb4sqlite::Code4& code, const std::vector<TaskRow>& checked) override
    {
        std::string made;
        auto describe = [&](const char* what, const ApTransferResult& r) {
            for (const ApTransferVoucher& v : r.vouchers)
                if (v.glVoucher) made += std::string(made.empty() ? "" : ", ") + "GL voucher " + std::to_string(v.glVoucher) + " (" + what + ", period " + std::to_string(v.period) + ")";
            for (int p : r.closedPeriods) made += std::string(made.empty() ? "" : "; ") + what + " period " + std::to_string(p) + " left waiting: it's closed";
        };
        for (const TaskRow& r : checked) {
            if (r.key == 1) describe("AP", TransferAp(code, ini_));
            if (r.key == 2) describe("AR", TransferAr(code, ini_));
        }
        return {true, made.empty() ? "Nothing was waiting." : "Made " + made + "."};
    }

private:
    long ini_;
};

class DepreciationTask : public TaskModel {
public:
    explicit DepreciationTask(long ini) : ini_(ini) {}
    std::string Title() const override { return "Depreciation run"; }
    std::string Action() const override { return "Run depreciation"; }
    bool PerItem() const override { return false; }
    unsigned Groups() const override { return data_group::kAssets | data_group::kGlVouchers | data_group::kReports; }
    TaskOption Option() const override { return TaskOption::Period; }
    std::string OptionLabel() const override { return "Period:"; }
    std::vector<std::pair<std::string, int>> Columns() const override { return {{"Asset", 70}, {"Description", 220}, {"This month", 100}}; }
    std::string Noun(int n) const override { return n == 1 ? "asset" : "assets"; }
    std::vector<TaskRow> Load(cb4sqlite::Code4& code) override
    {
        EnsureAssetTables(code);
        const RunPlan plan = PlanRun(code, std::atoi(option.c_str()));
        std::vector<TaskRow> rows;
        for (const ::RunItem& i : plan.due) rows.push_back({0, {i.asset.code, i.asset.description, money(i.amount)}});
        for (const RunSkip& s : plan.skipped) {
            TaskRow r{0, {s.asset.code, s.asset.description, ""}, false, false, ""};
            switch (s.reason) {
            case SkipReason::Disposed: r.note = "Disposed."; break;
            case SkipReason::FullyDepreciated: r.note = "Fully depreciated."; break;
            case SkipReason::NoType: r.note = "Its type isn't on file."; break;
            case SkipReason::BadAccount: r.note = "Its type's accounts aren't in the chart."; break;
            case SkipReason::NotYetBought: r.note = "Bought after the period."; break;
            }
            rows.push_back(std::move(r));
        }
        alreadyRun_ = plan.alreadyRun ? plan.existingVoucher : -1;
        return rows;
    }
    std::string Summary(const std::vector<TaskRow>& rows) const override
    {
        if (alreadyRun_ >= 0) return "Period " + option + " has been run" + (alreadyRun_ ? ": GL voucher " + std::to_string(alreadyRun_) : "") + ".";
        return TaskModel::Summary(rows);
    }
    TaskStep RunAll(cb4sqlite::Code4& code, const std::vector<TaskRow>&) override
    {
        const RunResult r = RunDepreciation(code, std::atoi(option.c_str()), ini_);
        switch (r.status) {
        case RunStatus::Created: return {true, "Made GL voucher " + std::to_string(r.batchno) + ": " + std::to_string(r.assets) + " assets, " + money(r.total) + "."};
        case RunStatus::NothingDue: return {true, "No asset is due: no voucher."};
        case RunStatus::AlreadyRun: return {false, "Period " + option + " has been run: GL voucher " + std::to_string(r.batchno) + "."};
        case RunStatus::NoPeriodEnd: return {false, "Period " + option + " has no end date."};
        }
        return {false, ""};
    }

private:
    long ini_;
    long alreadyRun_ = -1;
};

class YearEndTask : public TaskModel {
public:
    std::string Title() const override { return "Year end"; }
    std::string Action() const override { return "Close the year"; }
    bool PerItem() const override { return false; }
    unsigned Groups() const override { return data_group::kGlVouchers | data_group::kGlBalances | data_group::kReports; }
    std::vector<std::pair<std::string, int>> Columns() const override { return {{"Account", 80}, {"Description", 240}, {"Balance", 110}}; }
    std::string Noun(int n) const override { return n == 1 ? "account" : "accounts"; }
    std::vector<TaskRow> Load(cb4sqlite::Code4& code) override
    {
        // What closes: the income statement accounts' balances go to retained earnings.
        std::vector<TaskRow> rows;
        cb4sqlite::Query q(code, "SELECT trim(account) || CASE WHEN trim(coalesce(dept, '')) = '' THEN '' ELSE '-' || trim(dept) END, "
                                 "trim(descript), curr_bal, trim(type) FROM glaccts WHERE _deleted = 0 AND "
                                 "(trim(type) <> 'B' OR trim(type) = 'R') ORDER BY 1");
        while (q.next()) {
            TaskRow r{0, {q.text(0), q.text(1), moneyOf(std::atof(q.text(2).c_str()))}, true, false, ""};
            if (q.text(3) == "R") r.note = "Retained earnings: takes the year's result.";
            rows.push_back(std::move(r));
        }
        return rows;
    }
    std::string Summary(const std::vector<TaskRow>& rows) const override
    {
        return std::to_string(rows.size()) + " accounts close to retained earnings; the periods move to the previous year.";
    }
    TaskStep RunAll(cb4sqlite::Code4& code, const std::vector<TaskRow>&) override
    {
        const YearEndResult r = YearEnd(code);
        if (!r.closed)
            return {false, r.retained.size() == 1 ? "The year wasn't closed."
                                                  : "The chart needs exactly one retained earnings account (type R); it has " + std::to_string(r.retained.size()) + "."};
        return {true, "The year is closed: " + moneyOf(r.transferred) + " to retained earnings, " + std::to_string(r.archived) +
                          " ledger lines archived, " + std::to_string(r.closedVouchers) + " vouchers closed."};
    }
};

class PurgePayablesTask : public TaskModel {
public:
    std::string Title() const override { return "Purge paid payables"; }
    std::string Action() const override { return "Purge"; }
    bool PerItem() const override { return false; }
    unsigned Groups() const override { return data_group::kAp | data_group::kReports; }
    TaskOption Option() const override { return TaskOption::Date; }
    std::string OptionLabel() const override { return "Paid before:"; }
    std::vector<std::pair<std::string, int>> Columns() const override { return {{"Voucher", 64}, {"Vendor and invoice", 220}, {"Paid", 80}, {"Amount", 100}}; }
    std::string Noun(int n) const override { return n == 1 ? "cleared invoice" : "cleared invoices"; }
    std::vector<TaskRow> Load(cb4sqlite::Code4& code) override
    {
        std::vector<TaskRow> rows;
        for (const OpenItem& i : PurgeCandidates(code, option))
            rows.push_back({i.batchno, {std::to_string(i.batchno), TrimText(i.vendor) + ", " + TrimText(i.invoiceNo), dateText(i.datePaid), money(i.amount)}, true, false, ""});
        return rows;
    }
    TaskStep RunAll(cb4sqlite::Code4& code, const std::vector<TaskRow>&) override
    {
        const PurgeOutcome o = PurgePaid(code, option);
        return {true, "Purged " + std::to_string(o.items) + " invoices and " + std::to_string(o.cheques) + " cheques."};
    }
};

class PurgeReceivablesTask : public TaskModel {
public:
    std::string Title() const override { return "Purge received receivables"; }
    std::string Action() const override { return "Purge"; }
    unsigned Groups() const override { return data_group::kAr | data_group::kReports; }
    TaskOption Option() const override { return TaskOption::Date; }
    std::string OptionLabel() const override { return "Received before:"; }
    std::vector<std::pair<std::string, int>> Columns() const override { return {{"Voucher", 64}, {"Customer and invoice", 220}, {"Received", 80}, {"Amount", 100}}; }
    std::string Noun(int n) const override { return n == 1 ? "settled invoice" : "settled invoices"; }
    std::vector<TaskRow> Load(cb4sqlite::Code4& code) override
    {
        std::vector<TaskRow> rows;
        for (const ArOpenItem& i : ArPurgeCandidates(code, option))
            rows.push_back({i.batchno, {std::to_string(i.batchno), TrimText(i.customer) + ", " + TrimText(i.invoiceNo), dateText(i.datePaid), money(i.amount)}});
        return rows;
    }
    TaskStep RunItem(cb4sqlite::Code4& code, const TaskRow& row) override
    {
        const ArPurgeOutcome o = PurgeReceived(code, option, row.key);
        if (o.items == 0) return {false, "Not purged: it's no longer settled before " + dateText(option) + "."};
        return {true, "Purged, with " + std::to_string(o.receipts) + (o.receipts == 1 ? " receipt." : " receipts.")};
    }
};

class CreateChequesTask : public TaskModel {
public:
    explicit CreateChequesTask(std::string payDate) { payment.payDate = std::move(payDate); }
    std::string Title() const override { return "Create cheques"; }
    std::string Action() const override { return "Create cheques"; }
    bool PerItem() const override { return false; }
    bool Checks() const override { return true; }
    unsigned Groups() const override { return data_group::kAp | data_group::kReports; }
    TaskOption Option() const override { return TaskOption::Date; }
    std::string OptionLabel() const override { return "Due by:"; }
    bool HasPaymentOptions() const override { return true; }
    PaymentOptions DefaultPaymentOptions(cb4sqlite::Code4& code) const override
    {
        PaymentOptions o;
        o.payDate = payment.payDate;
        for (const ApBank& b : LoadApAccounts(code).banks)
            o.banks.push_back({TrimText(b.shortName), TrimText(b.shortName) + "  " + TrimText(b.name) + "  (" + TrimText(b.account.account) +
                                                          (TrimText(b.account.dept).empty() ? "" : "-" + TrimText(b.account.dept)) + ")"});
        if (!o.banks.empty()) o.bank = o.banks.front().first;
        o.firstNumber = NextChequeNumber(code);
        return o;
    }
    std::vector<std::pair<std::string, int>> Columns() const override { return {{"Voucher", 64}, {"Vendor and invoice", 220}, {"Due", 80}, {"Owed", 100}}; }
    std::string Noun(int n) const override { return n == 1 ? "invoice" : "invoices"; }
    std::vector<TaskRow> Load(cb4sqlite::Code4& code) override
    {
        std::vector<TaskRow> rows;
        listed_.clear();
        for (const PaymentItem& p : PaymentItems(code, option, true)) {
            const std::string ctl = TrimText(p.item.paymentCtl);
            if (ctl != ap_payment::kHeld && ctl != ap_payment::kOpen && ctl != ap_payment::kForced) continue;   // paid
            const bool due = TrimText(p.item.dueDate) <= TrimText(option);   // as PaymentItems: a blank date is due
            TaskRow r{p.item.batchno, {std::to_string(p.item.batchno), p.vendorName + ", " + TrimText(p.item.invoiceNo), dateText(p.item.dueDate), money(p.owed)}};
            r.checked = ctl == ap_payment::kForced || (ctl == ap_payment::kOpen && due);
            if (ctl == ap_payment::kHeld) r.note = "Held: check it to pay it.";
            else if (ctl == ap_payment::kForced && !due) r.note = "Forced: paid before it's due.";
            else if (!due) r.note = "Not due: check it to pay it now.";
            listed_.push_back({p.item.batchno, ctl, due});
            rows.push_back(std::move(r));
        }
        return rows;
    }
    std::string Summary(const std::vector<TaskRow>& rows) const override
    {
        if (rows.empty()) return "Nothing to do: no invoices are open.";
        int checked = 0;
        std::int64_t owed = 0;
        for (const TaskRow& r : rows)
            if (r.checked) {
                ++checked;
                owed += m::ParseAmount(r.cells[3]).value_or(0);
            }
        return std::to_string(rows.size()) + " open " + Noun(static_cast<int>(rows.size())) + ", " + std::to_string(checked) +
               " to pay (" + money(owed) + "). An unchecked invoice is held.";
    }
    TaskStep RunAll(cb4sqlite::Code4& code, const std::vector<TaskRow>& checked) override
    {
        const ApAccounts setat = LoadApAccounts(code);
        if (setat.banks.empty()) return {false, "There is no bank (a GL_SETAT \"CA\" account) to draw the cheques on."};
        // Blank options are as the classic screen fills them: the first bank, the next number.
        const ApBank* bank = payment.bank.empty() ? &setat.banks.front() : nullptr;
        for (const ApBank& b : setat.banks)
            if (!bank && TrimText(b.shortName) == payment.bank) bank = &b;
        if (!bank) return {false, "The bank " + payment.bank + " is no longer a GL_SETAT \"CA\" account."};
        const long first = payment.firstNumber > 0 ? payment.firstNumber : NextChequeNumber(code);
        if (OpenPeriodOf(code, payment.payDate) == 0) return {false, "The payment date isn't in an open period."};

        // The boxes become the payment controls (TAPHoldForce): what's checked is paid, the rest held.
        std::vector<std::pair<long, std::string>> undo;
        int held = 0, released = 0, forced = 0;
        for (const Listed& l : listed_) {
            const bool pay = std::any_of(checked.begin(), checked.end(), [&](const TaskRow& r) { return r.key == l.batchno; });
            std::string want = l.control;
            if (pay && l.control == ap_payment::kHeld) want = l.due ? ap_payment::kOpen : ap_payment::kForced;
            else if (pay && l.control == ap_payment::kOpen && !l.due) want = ap_payment::kForced;
            else if (!pay && (l.control == ap_payment::kForced || (l.control == ap_payment::kOpen && l.due))) want = ap_payment::kHeld;
            if (want == l.control) continue;
            if (!SetPaymentControl(code, l.batchno, want)) continue;   // paid meanwhile: CreateCheques decides
            undo.push_back({l.batchno, l.control});
            ++(want == ap_payment::kHeld ? held : want == ap_payment::kForced ? forced : released);
        }
        auto putBack = [&] {
            for (const auto& [batchno, ctl] : undo) SetPaymentControl(code, batchno, ctl);
        };
        const std::string holds = (held ? ", held " + std::to_string(held) : std::string()) +
                                  (released ? ", released " + std::to_string(released) : std::string()) +
                                  (forced ? ", forced " + std::to_string(forced) : std::string());

        const CreateChequesResult r = CreateCheques(code, option, payment.payDate, bank->account, first);
        switch (r.status) {
        case CreateChequesStatus::Created: {
            std::string made;
            for (const ChequeMade& c : r.cheques)
                made += std::string(made.empty() ? "" : ", ") + "cheque " + std::to_string(c.number) + " to " + c.vendorName + " (" + money(c.amount) + ")";
            return {true, "Made " + made + ", drawn on " + TrimText(bank->shortName) + holds + ". Print them from Print Cheques."};
        }
        case CreateChequesStatus::NothingDue: return {true, "Nothing to pay by " + dateText(option) + holds + "."};
        case CreateChequesStatus::BadBank: putBack(); return {false, "The bank account isn't in the chart."};
        case CreateChequesStatus::BadNumber: putBack(); return {false, "Cheque " + std::to_string(r.clash) + " is already on file."};
        case CreateChequesStatus::ClosedPeriod: putBack(); return {false, "The payment date isn't in an open period."};
        }
        putBack();
        return {false, ""};
    }

private:
    struct Listed {
        long batchno;
        std::string control;   // as listed
        bool due;
    };
    std::vector<Listed> listed_;
};

class BackupTask : public TaskModel {
public:
    BackupTask(std::string database, std::string today) : database_(std::move(database)), today_(std::move(today)) {}
    std::string Title() const override { return "Backup"; }
    std::string Action() const override { return "Back up"; }
    bool PerItem() const override { return false; }
    bool Writes() const override { return false; }   // it reads: the pages and other jobs carry on
    unsigned Groups() const override { return 0; }
    std::vector<std::pair<std::string, int>> Columns() const override { return {{"Company file", 360}, {"The copy", 360}}; }
    std::string Noun(int n) const override { return n == 1 ? "file" : "files"; }
    std::vector<TaskRow> Load(cb4sqlite::Code4&) override
    {
        const int y = std::atoi(today_.substr(0, 4).c_str()), mo = std::atoi(today_.substr(4, 2).c_str()), d = std::atoi(today_.substr(6, 2).c_str());
        // Beside the company file, as Backup's file dialog starts (BackupFileName is the name alone).
        const std::filesystem::path db(std::u8string(database_.begin(), database_.end()));
        copy_ = (db.parent_path() / BackupFileName(db, y, mo, d)).u8string();
        return {{1, {database_, std::string(copy_.begin(), copy_.end())}, true, false, ""}};
    }
    TaskStep RunAll(cb4sqlite::Code4& code, const std::vector<TaskRow>&) override
    {
        code.backupTo(std::string(copy_.begin(), copy_.end()).c_str());
        return {true, "Backed up to " + std::string(copy_.begin(), copy_.end()) + "."};
    }

private:
    std::string database_, today_;
    std::u8string copy_;
};

// ---- the cheque cycle (spec #9 D5, D6; S9-M3) --------------------------------------
//
// The classic screens are ApPayments.cpp's ConfirmDialog (8074) and
// BankRecDialog with ClearDialog (6085); the messages are theirs.

std::string bankText(const ApAccount& a)
{
    return a.dept.empty() ? a.account : a.account + "-" + a.dept;
}

class ChequesTask : public TaskModel {
public:
    std::string Title() const override { return "Cheques"; }
    std::string Action() const override { return "Confirm printed"; }
    std::vector<std::pair<std::string, int>> Columns() const override
    {
        return {{"Cheque", 64}, {"Vendor", 220}, {"Date", 80}, {"Amount", 100}};
    }
    unsigned Groups() const override { return data_group::kAp | data_group::kReports; }
    std::string Noun(int n) const override { return n == 1 ? "cheque" : "cheques"; }
    // ConfirmDialog asked "Have cheques %ld to %ld printed correctly? They can't be renumbered after this."
    std::string QuestionNote() const override
    {
        return "Check only the cheques that printed correctly: they can't be renumbered after this. "
               "Renumber a spoiled one first.";
    }
    std::vector<TaskRow> Load(cb4sqlite::Code4& code) override
    {
        std::vector<TaskRow> rows;
        for (const ChequeToPrint& c : LoadChequesIn(code, ap_payment::kPaid)) {
            const std::string who = TrimText(c.cheque.vendor) + (c.payee.name.empty() ? "" : "  " + TrimText(c.payee.name));
            rows.push_back({c.cheque.number, {std::to_string(c.cheque.number), who, dateText(c.cheque.date), money(c.cheque.amount)}});
        }
        return rows;
    }
    TaskStep RunItem(cb4sqlite::Code4& code, const TaskRow& row) override
    {
        if (ConfirmPrinted(code, row.key, row.key) == 1) return {true, "Confirmed printed."};
        return {false, "Not confirmed: it isn't waiting to be printed."};
    }

    std::string RowAction() const override { return "Ren&umber"; }
    std::string RowInputLabel() const override { return "Ne&w number:"; }
    long RowInputDefault(cb4sqlite::Code4& code) const override { return NextChequeNumber(code); }
    TaskStep RunRowAction(cb4sqlite::Code4& code, const TaskRow& row) override
    {
        const long from = row.key, to = std::atol(m::Trim(rowInput).c_str());
        if (to <= 0) return {false, "Enter the new cheque number."};
        switch (RenumberCheque(code, from, to)) {
        case RenumberResult::Renumbered:
            return {true, "Cheque " + std::to_string(from) + " is now cheque " + std::to_string(to) + ". Print it again."};
        case RenumberResult::NumberUsed: return {false, "Cheque number " + std::to_string(to) + " has already been used."};
        default: return {false, "Cheque " + std::to_string(from) + " can't be renumbered."};
        }
    }
    unsigned short PageCommand() const override { return 4091; }   // Print Cheques, its preview (Q3)
    std::string PageCommandLabel() const override { return "&Print cheques"; }
};

class BankReconciliationTask : public TaskModel {
public:
    std::string Title() const override { return "Bank reconciliation"; }
    std::string Action() const override { return "Clear"; }
    std::vector<std::pair<std::string, int>> Columns() const override
    {
        return {{"Cheque", 64}, {"Vendor", 120}, {"State", 70}, {"Bank", 90}, {"Amount", 100}};
    }
    unsigned Groups() const override { return data_group::kAp | data_group::kGlBalances | data_group::kReports; }
    std::string Noun(int n) const override { return n == 1 ? "cheque" : "cheques"; }
    TaskOption Option() const override { return TaskOption::Date; }
    bool OptionFiltersList() const override { return false; }   // the date is the clear's, not a filter
    std::string OptionLabel() const override { return "&Date:"; }   // the clear's date, and a void's (1051 had one Date for both)
    std::vector<TaskRow> Load(cb4sqlite::Code4& code) override
    {
        std::vector<TaskRow> rows;
        for (const ChequeToPrint& c : LoadChequesIn(code, "")) {
            const bool printed = c.state == ap_payment::kPrinted;
            TaskRow r{c.cheque.number,
                      {std::to_string(c.cheque.number), TrimText(c.cheque.vendor),
                       printed ? "Printed" : c.state == ap_payment::kPaid ? "Made" : "", bankText(c.cheque.bank),
                       money(c.cheque.amount)}};
            r.checked = false;   // a statement's cheques are checked one by one
            if (!printed) {      // ClearCheque refuses it
                r.selectable = false;
                r.note = "Not confirmed printed: confirm it on the Cheques page.";
            }
            rows.push_back(std::move(r));
        }
        return rows;
    }
    TaskStep RunItem(cb4sqlite::Code4& code, const TaskRow& row) override
    {
        switch (ClearCheque(code, row.key, option)) {
        case ClearResult::Cleared: return {true, "Cleared " + dateText(option) + "."};
        case ClearResult::NotPrinted: return {false, "Not confirmed printed (Cheques page)."};
        case ClearResult::AlreadyCleared: return {false, "Already cleared."};
        default: return {false, "Can't be cleared."};
        }
    }

    std::string RowAction() const override { return "&Void"; }
    std::string RowQuestion(const TaskRow& row) const override
    {
        return "Void cheque " + std::to_string(row.key) + " (" + (row.cells.size() > 1 ? row.cells[1] : std::string()) +
               ", " + (row.cells.size() > 4 ? row.cells[4] : std::string()) +
               ")? Its invoices become open again and its payment is reversed, dated " + dateText(option) + ".";
    }
    TaskStep RunRowAction(cb4sqlite::Code4& code, const TaskRow& row) override
    {
        const VoidOutcome v = VoidCheque(code, row.key, option);
        switch (v.result) {
        case VoidResult::Voided:
            return {true, "Cheque " + std::to_string(row.key) + " voided: " + std::to_string(v.itemsReopened) +
                              " invoice(s) open again; " + std::to_string(v.rowsRemoved) + " journal row(s) removed, " +
                              std::to_string(v.rowsReversed) + " reversed."};
        case VoidResult::ClosedPeriod:
            return {false, "The date isn't in an open period, and the payment has gone to the General Ledger: it can't be "
                           "reversed then."};
        default: return {false, "Cheque " + std::to_string(row.key) + " can't be voided."};
        }
    }
};

// ---- Restore Databases (spec #9 D7, S9-M4) ------------------------------------------
//
// The classic screen is Backup.cpp's RunRestore (a file dialog, then message
// boxes); the checks, the question and the messages are its.

std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }
std::string utf8Of(const std::filesystem::path& p)
{
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

class RestoreTask : public TaskModel {
public:
    explicit RestoreTask(std::string current) : current_(pathOf(current)) {}
    std::string Title() const override { return "Restore"; }
    std::string Action() const override { return "Restore"; }
    bool PerItem() const override { return false; }
    bool Checks() const override { return false; }
    unsigned Groups() const override { return data_group::kAll; }
    std::vector<std::pair<std::string, int>> Columns() const override
    {
        return {{"Backup", 220}, {"Saved", 120}, {"Size", 80}};
    }
    std::string Noun(int n) const override { return n == 1 ? "backup" : "backups"; }
    std::string Summary(const std::vector<TaskRow>& rows) const override
    {
        if (rows.empty()) return "No backups beside " + utf8Of(current_.filename()) + ": Browse for one.";
        return std::to_string(rows.size()) + " " + Noun(static_cast<int>(rows.size())) +
               ". Select the one to restore: everything entered since it was made will be lost.";
    }
    bool ActionOnRow() const override { return true; }
    bool ClosesOtherPages() const override { return true; }
    std::string RowAction() const override { return "&Restore"; }
    std::string BrowseLabel() const override { return "B&rowse..."; }

    std::vector<TaskRow> Load(cb4sqlite::Code4&) override
    {
        std::vector<std::filesystem::path> found;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(current_.parent_path(), ec)) {
            if (!e.is_regular_file(ec)) continue;
            const std::filesystem::path p = e.path();
            const std::wstring ext = p.extension().wstring();
            if (differExt(ext, L".db") && differExt(ext, L".bak")) continue;
            if (std::filesystem::equivalent(p, current_, ec)) continue;
            found.push_back(p);
        }
        for (const std::filesystem::path& p : added_)
            if (std::find(found.begin(), found.end(), p) == found.end()) found.push_back(p);
        std::sort(found.begin(), found.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
            std::error_code e1, e2;
            return std::filesystem::last_write_time(a, e1) > std::filesystem::last_write_time(b, e2);
        });
        files_ = found;
        std::vector<TaskRow> rows;
        for (size_t i = 0; i < files_.size(); ++i) {
            const std::filesystem::path& p = files_[i];
            // The name beside the company file; a file from elsewhere (Browse) by its whole path.
            std::error_code e;
            const bool beside = std::filesystem::equivalent(p.parent_path(), current_.parent_path(), e);
            TaskRow r{static_cast<long>(i), {utf8Of(beside ? p.filename() : p), savedOf(p), sizeOf(p)}};
            std::string why;
            if (!CheckRestoreFile(current_, p, &why)) {   // RunRestore's check
                r.selectable = false;
                r.note = "Can't be restored: " + why + ".";
            }
            rows.push_back(std::move(r));
        }
        return rows;
    }
    int AddFile(cb4sqlite::Code4& code, const std::string& utf8Path) override
    {
        const std::filesystem::path p = pathOf(utf8Path);
        if (std::find(added_.begin(), added_.end(), p) == added_.end()) added_.push_back(p);
        Load(code);
        const auto it = std::find(files_.begin(), files_.end(), p);
        return it == files_.end() ? -1 : static_cast<int>(it - files_.begin());
    }
    std::string RowQuestion(const TaskRow& row) const override
    {
        return "Replace the company database with " + fileOf(row) + "? Everything entered since that backup will be lost. "
               "The current database is kept as " + utf8Of(BackupOfCurrent(current_).filename()) + ".";
    }
    TaskStep RunRowAction(cb4sqlite::Code4& code, const TaskRow& row) override
    {
        if (!row.selectable) return {false, row.note};
        std::string why;
        if (!CheckRestoreFile(current_, pathOf(fileOf(row)), &why))   // checked again: the file may have changed
            return {false, "This file can't be restored: " + why + "."};
        const std::string bak = utf8Of(BackupOfCurrent(current_).filename());
        const RestoreResult r = RestoreDatabase(code, current_, pathOf(fileOf(row)));
        if (r.restored) return {true, "The company database was restored. The previous one is " + bak + "."};
        std::string text = "The restore failed: " + r.error + ".";
        text += r.reopened ? " The company database is unchanged."
                           : " The company database could not be reopened. Close the program and check " + utf8Of(current_) +
                                 " and " + bak + ".";
        return {false, text};
    }

private:
    static bool differExt(const std::wstring& a, const wchar_t* b)   // true if they differ
    {
        if (a.size() != std::wcslen(b)) return true;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::towlower(a[i]) != std::towlower(b[i])) return true;
        return false;
    }
    std::string fileOf(const TaskRow& row) const
    {
        return row.key >= 0 && static_cast<size_t>(row.key) < files_.size() ? utf8Of(files_[static_cast<size_t>(row.key)])
                                                                            : std::string();
    }
    static std::string savedOf(const std::filesystem::path& p)
    {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(p, ec);
        if (ec) return {};
        const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
        const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof buf, "%m/%d/%Y %H:%M", &tm);
        return buf;
    }
    static std::string sizeOf(const std::filesystem::path& p)
    {
        std::error_code ec;
        const auto n = std::filesystem::file_size(p, ec);
        if (ec) return {};
        return std::to_string((n + 1023) / 1024) + " KB";
    }

    std::filesystem::path current_;
    std::vector<std::filesystem::path> files_, added_;
};

} // namespace

std::string TaskModel::Summary(const std::vector<TaskRow>& rows) const
{
    int listed = 0, checked = 0;
    for (const TaskRow& r : rows) {
        ++listed;
        if (r.checked && r.selectable) ++checked;
    }
    if (listed == 0) return "Nothing to do: no " + Noun(2) + ".";
    if (!Checks()) return std::to_string(listed) + " " + Noun(listed) + ".";
    return std::to_string(listed) + " " + Noun(listed) + " listed, " + std::to_string(checked) + " checked.";
}

std::string TaskModel::Question(const std::vector<TaskRow>& rows) const
{
    int n = 0;
    for (const TaskRow& r : rows)
        if (r.checked && r.selectable) ++n;
    if (PerItem()) return Action() + " " + std::to_string(n) + " " + Noun(n) + "?";
    return Checks() ? Action() + " for " + std::to_string(n) + " " + Noun(n) + "?" : Title() + "?";
}

TaskRun RunTask(TaskModel& task, cb4sqlite::Code4& code, std::vector<TaskRow>& rows,
                const std::function<bool(int done, int of)>& progress)
{
    TaskRun run;
    if (!task.PerItem()) {
        std::vector<TaskRow> checked;
        for (const TaskRow& r : rows)
            if (r.checked) checked.push_back(r);
        run.of = 1;
        const TaskStep s = task.RunAll(code, checked);
        run.done = 1;
        run.ok = s.ok;
        run.message = s.note;
        if (progress) progress(1, 1);
        return run;
    }
    std::vector<size_t> todo;
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].checked && rows[i].selectable) todo.push_back(i);
    run.of = static_cast<int>(todo.size());
    int okCount = 0, refused = 0;
    for (size_t i : todo) {
        if (progress && !progress(run.done, run.of)) {
            run.stoppedEarly = true;
            break;
        }
        const TaskStep s = task.RunItem(code, rows[i]);
        rows[i].note = s.note;
        if (s.ok) {
            ++okCount;
            rows[i].checked = false;
        } else {
            ++refused;
        }
        ++run.done;
    }
    if (progress && !run.stoppedEarly) progress(run.done, run.of);
    run.message = std::to_string(okCount) + " " + task.Noun(okCount) + " done" +
                  (refused ? ", " + std::to_string(refused) + " refused (see the list)" : "") +
                  (run.stoppedEarly ? "; stopped before the rest" : "") + ".";
    return run;
}

std::unique_ptr<TaskModel> MakePostTask(VoucherKind kind) { return std::make_unique<PostTask>(kind); }
std::unique_ptr<TaskModel> MakeCancelTask(VoucherKind kind) { return std::make_unique<CancelTask>(kind); }
std::unique_ptr<TaskModel> MakeRecoverTask(VoucherKind kind) { return std::make_unique<RecoverTask>(kind); }
std::unique_ptr<TaskModel> MakeTransferTask(long ini) { return std::make_unique<TransferTask>(ini); }
std::unique_ptr<TaskModel> MakeDepreciationTask(long ini) { return std::make_unique<DepreciationTask>(ini); }
std::unique_ptr<TaskModel> MakeYearEndTask() { return std::make_unique<YearEndTask>(); }
std::unique_ptr<TaskModel> MakePurgePayablesTask() { return std::make_unique<PurgePayablesTask>(); }
std::unique_ptr<TaskModel> MakePurgeReceivablesTask() { return std::make_unique<PurgeReceivablesTask>(); }
std::unique_ptr<TaskModel> MakeCreateChequesTask(const std::string& payDate) { return std::make_unique<CreateChequesTask>(payDate); }
std::unique_ptr<TaskModel> MakeBackupTask(const std::string& db, const std::string& today) { return std::make_unique<BackupTask>(db, today); }
std::unique_ptr<TaskModel> MakeChequesTask() { return std::make_unique<ChequesTask>(); }
std::unique_ptr<TaskModel> MakeBankReconciliationTask() { return std::make_unique<BankReconciliationTask>(); }
std::unique_ptr<TaskModel> MakeRestoreTask(const std::string& current) { return std::make_unique<RestoreTask>(current); }
