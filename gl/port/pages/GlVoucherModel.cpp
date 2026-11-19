//
// GlVoucherModel.cpp - see GlVoucherModel.h.
//
#include "GlVoucherModel.h"

#include "../Accounts.h"   // TrimText, SeekAccount
#include "model/Validate.h"

#include <algorithm>
#include <cmath>

namespace m = fx::model;

namespace {

std::vector<m::GridColumn> columns()
{
    return {
        {"account", "Account", m::CellKind::Lookup, 7},
        {"dept", "Dept", m::CellKind::Upper, 4},
        {"doc", "Document", m::CellKind::Text, 12},
        {"name", "Account name", m::CellKind::Text, 24},
        {"debit", "Debit", m::CellKind::Amount, 12, false, true},
        {"credit", "Credit", m::CellKind::Amount, 12, false, true},
        {"medrep", "Med rep", m::CellKind::Lookup, 5},
        {"prod", "Product", m::CellKind::Lookup, 7},
        {"doctor", "Doctor", m::CellKind::Lookup, 5},
    };
}

std::vector<m::LookupItem> itemsOf(cb4sqlite::Code4& code, const char* sql)
{
    std::vector<m::LookupItem> items;
    try {
        cb4sqlite::Query q(code, sql);
        while (q.next()) items.push_back({q.text(0), q.columns() > 2 ? q.text(1) : std::string(), q.text(q.columns() - 1)});
    } catch (const std::exception&) {   // a company without the table: no items
    }
    return items;
}

m::Check inTable(const m::LookupModel* table, std::string message)
{
    return m::Exists(
        [table](const std::string& s) {
            for (const m::LookupItem& it : table->items())
                if (it.code == s) return true;
            return false;
        },
        std::move(message));
}

std::string amountText(double value)
{
    const auto cents = static_cast<std::int64_t>(std::llround(value * 100));
    return cents ? m::FormatAmount(cents) : std::string();
}

double amountOf(const std::string& text)
{
    const std::string t = m::Trim(text);
    if (t.empty()) return 0;
    const auto cents = m::ParseAmount(t);
    return cents ? static_cast<double>(*cents) / 100.0 : 0;
}

} // namespace

GlVoucherModel::GlVoucherModel(cb4sqlite::Code4& code, bool adjustment, long iniBatchno)
    : code_(code), adjustment_(adjustment), iniBatchno_(iniBatchno)
{
    LoadLookups();
    std::vector<m::GridColumn> cols = columns();
    cols[0].validator.Add(inTable(&accounts_, "Not an account in the chart."));
    cols[2].validator.Add(m::MaxLength(40));
    cols[3].validator.Add(m::MaxLength(40));
    cols[6].validator.Add(inTable(&reps_, "Not a medical rep's code."));
    cols[7].validator.Add(inTable(&products_, "Not a product code."));
    cols[8].validator.Add(inTable(&doctors_, "Not a doctor's code."));
    grid_.SetColumns(std::move(cols));
    grid_.OnCommit = [this](int row, int col) {
        const std::string& key = grid_.columns()[static_cast<size_t>(col)].key;
        if (key == "account" || key == "dept") {
            const std::string name = AccountName(grid_.Cell(row, "account"), grid_.Cell(row, "dept"));
            if (!name.empty()) grid_.SetCell(row, "name", name);
        } else if (key == "debit" && !grid_.Cell(row, col).empty()) {
            grid_.SetCell(row, "credit", "");   // a debit or a credit, not both
        } else if (key == "credit" && !grid_.Cell(row, col).empty()) {
            grid_.SetCell(row, "debit", "");
        }
    };
    periods_ = OpenPeriods(code_);
}

void GlVoucherModel::LoadLookups()
{
    accounts_.SetItems(itemsOf(code_, "SELECT trim(account), coalesce(trim(dept), ''), trim(descript) FROM glaccts "
                                      "WHERE _deleted = 0 ORDER BY 1, 2"));
    reps_.SetItems(itemsOf(code_, "SELECT trim(repcode), trim(rpname) FROM gl_medrp WHERE _deleted = 0 ORDER BY 1"));
    products_.SetItems(itemsOf(code_, "SELECT trim(fpcode), trim(ffulln) FROM gl_itemm WHERE _deleted = 0 ORDER BY 1"));
    doctors_.SetItems(itemsOf(code_, "SELECT trim(drcode), trim(rpname) FROM gl_drcod WHERE _deleted = 0 ORDER BY 1"));
}

std::string GlVoucherModel::AccountName(const std::string& account, const std::string& dept) const
{
    const int i = accounts_.Exact(m::LookupItem{m::Trim(account), m::Trim(dept), ""}.key());
    return i >= 0 ? accounts_.items()[static_cast<size_t>(i)].name : std::string();
}

long GlVoucherModel::nextNumber() const
{
    VoucherHeaders h(code_);
    return NextVoucherNumber(h, iniBatchno_);
}

bool GlVoucherModel::PeriodOpen(int p) const
{
    return std::any_of(periods_.begin(), periods_.end(), [p](const OpenPeriod& o) { return o.number == p; });
}

void GlVoucherModel::New(const std::string& today)
{
    periods_ = OpenPeriods(code_);
    number = 0;
    description.clear();
    cheque.clear();
    date = today;
    period = periods_.empty() ? 0 : periods_.back().number;   // the lowest open period, as Voucher Entry selects
    savedLines_ = 0;
    grid_.Clear();
}

GlVoucherModel::Open GlVoucherModel::Load(long voucher)
{
    periods_ = OpenPeriods(code_);
    VoucherHeaders h(code_);
    VoucherLines l(code_);
    if (!SeekVoucher(h, voucher)) return Open::NotFound;
    const std::string mode = ReadHeader(h).mode;
    if (adjustment_ && mode != voucher_mode::kAdjustment && !IsPostedMode(mode)) return Open::NotAdjustment;
    if (!adjustment_ && mode == voucher_mode::kAdjustment) return Open::WrongMode;
    VoucherHeader header;
    std::vector<VoucherLine> lines;
    switch (LoadVoucher(h, l, voucher, header, lines)) {
    case LoadResult::NotFound: return Open::NotFound;
    case LoadResult::Posted: return Open::Posted;
    case LoadResult::NoLines: return Open::NoLines;
    case LoadResult::Loaded: break;
    }
    number = header.number;
    description = TrimText(header.description);
    date = header.date;
    cheque = TrimText(header.cheque);
    period = header.period;
    std::vector<std::vector<std::string>> rows;
    for (const VoucherLine& v : lines)
        rows.push_back({TrimText(v.account), TrimText(v.dept), TrimText(v.reference), TrimText(v.descript),
                        amountText(v.debit), amountText(v.credit), TrimText(v.medrep), TrimText(v.prod),
                        TrimText(v.drcode)});
    grid_.SetRows(rows);
    savedLines_ = static_cast<int>(lines.size());
    return Open::Opened;
}

long GlVoucherModel::Neighbour(int step) const
{
    VoucherHeaders h(code_);
    std::vector<long> numbers;
    for (const VoucherHeader& v : ListVouchers(h, {adjustment_ ? voucher_mode::kAdjustment : voucher_mode::kOpen}))
        numbers.push_back(v.number);
    std::sort(numbers.begin(), numbers.end());
    if (step < 0) {
        for (auto it = numbers.rbegin(); it != numbers.rend(); ++it)
            if (isNew() || *it < number) return *it;
    } else {
        for (long n : numbers)
            if (!isNew() && n > number) return n;
    }
    return 0;
}

std::vector<VoucherLine> GlVoucherModel::Lines(Result& refusal) const
{
    std::vector<VoucherLine> out;
    for (int r = 0; r < grid_.Rows(); ++r) {
        if (grid_.IsBlank(r)) continue;
        auto refuse = [&](const char* column, std::string message) {
            refusal = {false, "", r, column, std::move(message)};
            return std::vector<VoucherLine>{};
        };
        VoucherLine v;
        v.line = static_cast<int>(out.size()) + 1;
        v.period = period;
        v.date = date;
        v.reference = grid_.Cell(r, "doc");
        v.descript = grid_.Cell(r, "name");
        v.cheque = cheque;
        v.account = grid_.Cell(r, "account");
        v.dept = grid_.Cell(r, "dept");
        v.medrep = grid_.Cell(r, "medrep");
        v.prod = grid_.Cell(r, "prod");
        v.drcode = grid_.Cell(r, "doctor");
        v.debit = amountOf(grid_.Cell(r, "debit"));
        v.credit = amountOf(grid_.Cell(r, "credit"));
        if (m::Trim(v.reference).empty()) return refuse("doc", "Type the line's document.");   // EnterError(kReference)
        if (accounts_.Exact(m::LookupItem{v.account, v.dept, ""}.key()) < 0)                    // CheckGLAccount
            return refuse(v.dept.empty() ? "account" : "dept", "Not an account and dept in the chart.");
        if (v.debit == 0 && v.credit == 0) return refuse("debit", "Enter a debit or credit amount.");   // 8092
        if (v.debit != 0 && v.credit != 0) return refuse("credit", "A debit or a credit, but not both.");  // 8093
        if (v.debit < 0 || v.credit < 0) return refuse(v.debit < 0 ? "debit" : "credit", "Not below zero.");
        out.push_back(std::move(v));
    }
    return out;
}

GlVoucherModel::Result GlVoucherModel::Save()
{
    if (m::Trim(description).empty()) return {false, "description", -1, "", "Type the voucher's description."};
    if (date.size() != 8) return {false, "date", -1, "", "Choose the voucher's date."};   // 8090
    if (!PeriodOpen(period)) return {false, "period", -1, "", "Choose an open period."};
    Result refusal{true};
    const std::vector<VoucherLine> lines = Lines(refusal);
    if (!refusal.ok) return refusal;
    if (lines.empty()) return {false, "", 0, "account", "Type the voucher's lines."};

    VoucherHeader header;
    header.mode = adjustment_ ? voucher_mode::kAdjustment : voucher_mode::kOpen;
    header.description = description;
    header.date = date;
    header.period = period;
    header.cheque = cheque;
    VoucherHeaders h(code_);
    VoucherLines l(code_);
    if (!isNew()) {
        VoucherHeader onFile;
        std::vector<VoucherLine> old;
        if (LoadVoucher(h, l, number, onFile, old) == LoadResult::Posted)
            return {false, "", -1, "", "Voucher " + std::to_string(number) + " has been posted: it can't be changed."};
    }
    long voucher = number;
    for (const VoucherLine& v : lines) voucher = SaveLine(h, l, voucher, header, v, iniBatchno_);   // as Add/Mod does
    for (int j = savedLines_; j > static_cast<int>(lines.size()); --j) DeleteLine(h, l, voucher, j);   // lines removed
    const bool created = isNew();
    h.close();
    l.close();
    Load(voucher);
    return {true, "", -1, "",
            "Voucher " + std::to_string(voucher) + (created ? " saved." : " saved.") +
                (grid_.Balances("debit", "credit") ? "" : " It doesn't balance yet, so it can't be posted.")};
}

GlVoucherModel::Result GlVoucherModel::Delete()
{
    if (isNew()) return {false, "", -1, "", "The voucher isn't saved."};
    const long voucher = number;
    VoucherHeaders h(code_);
    VoucherLines l(code_);
    DeleteVoucher(h, l, voucher);
    h.close();
    l.close();
    New(date);
    return {true, "", -1, "", "Voucher " + std::to_string(voucher) + " deleted."};
}

bool GlVoucherModel::CanPost() const
{
    return !isNew() && grid_.Balances("debit", "credit") && PeriodOpen(period);
}

GlVoucherModel::Result GlVoucherModel::Post()
{
    if (isNew()) return {false, "", -1, "", "Save the voucher first."};
    const long voucher = number;
    const PostResult r = PostVoucher(code_, voucher);
    if (std::find(r.posted.begin(), r.posted.end(), voucher) != r.posted.end()) {
        New(date);
        return {true, "", -1, "", "Voucher " + std::to_string(voucher) + " posted."};
    }
    for (const VoucherRefusal& f : r.refused)
        if (f.number == voucher)
            return {false, "", -1, "",
                    f.reason == PostRefusal::OutOfBalance
                        ? "Voucher " + std::to_string(voucher) + " doesn't balance."
                        : "Account " + f.account + (f.dept.empty() ? "" : "-" + f.dept) + " isn't in the chart."};
    return {false, "", -1, "", "Voucher " + std::to_string(voucher) + " wasn't posted."};
}
