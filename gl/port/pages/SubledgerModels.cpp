//
// SubledgerModels.cpp - see SubledgerModels.h.
//
#include "SubledgerModels.h"

#include "../Accounts.h"   // TrimText
#include "../Assets.h"     // ValidateAccount
#include "model/Validate.h"

#include <algorithm>

namespace m = fx::model;

namespace {

std::vector<m::LookupItem> itemsOf(cb4sqlite::Code4& code, const char* sql)
{
    std::vector<m::LookupItem> items;
    try {
        cb4sqlite::Query q(code, sql);
        while (q.next()) items.push_back({q.text(0), q.columns() > 2 ? q.text(1) : std::string(), q.text(q.columns() - 1)});
    } catch (const std::exception&) {
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

std::string cents(std::int64_t v) { return v ? m::FormatAmount(v) : std::string(); }
std::int64_t centsOf(const std::string& text)
{
    const std::string t = m::Trim(text);
    return t.empty() ? 0 : m::ParseAmount(t).value_or(0);
}

bool periodOpen(const std::vector<OpenPeriod>& periods, int p)
{
    return std::any_of(periods.begin(), periods.end(), [p](const OpenPeriod& o) { return o.number == p; });
}

EntryResult refuse(const char* field, std::string message) { return {false, field, -1, "", std::move(message)}; }
EntryResult refuseCell(int row, const char* column, std::string message) { return {false, "", row, column, std::move(message)}; }

std::string refusalText(const ApPostResult& r, long number, const char* what)
{
    for (const ApRefusal& f : r.refused)
        if (f.number == number) switch (f.reason) {
            case ApPostRefusal::OutOfBalance: return std::string(what) + " " + std::to_string(number) + " doesn't balance.";
            case ApPostRefusal::NoLines: return std::string(what) + " " + std::to_string(number) + " has no lines.";
            case ApPostRefusal::UnknownAccount:
                return "Account " + f.account.account + (f.account.dept.empty() ? "" : "-" + f.account.dept) +
                       " isn't in the chart.";
            }
    return std::string(what) + " " + std::to_string(number) + " wasn't posted.";
}

} // namespace

// ---- lookups --------------------------------------------------------------------

void EntryLookups::Load(cb4sqlite::Code4& code)
{
    accounts.SetItems(itemsOf(code, "SELECT trim(account), coalesce(trim(dept), ''), trim(descript) FROM glaccts "
                                    "WHERE _deleted = 0 ORDER BY 1, 2"));
    reps.SetItems(itemsOf(code, "SELECT trim(repcode), trim(rpname) FROM gl_medrp WHERE _deleted = 0 ORDER BY 1"));
    products.SetItems(itemsOf(code, "SELECT trim(fpcode), trim(ffulln) FROM gl_itemm WHERE _deleted = 0 ORDER BY 1"));
    doctors.SetItems(itemsOf(code, "SELECT trim(drcode), trim(rpname) FROM gl_drcod WHERE _deleted = 0 ORDER BY 1"));
    vendors.SetItems(itemsOf(code, "SELECT trim(vendorno), trim(vdname) FROM ap_vend WHERE _deleted = 0 ORDER BY 1"));
    customers.SetItems(itemsOf(code, "SELECT trim(vendorno), trim(vdname) FROM ar_cust WHERE _deleted = 0 ORDER BY 1"));
}

bool EntryLookups::HasAccount(const ApAccount& a) const
{
    return accounts.Exact(m::LookupItem{m::Trim(a.account), m::Trim(a.dept), ""}.key()) >= 0;
}

std::string EntryLookups::AccountName(const ApAccount& a) const
{
    const int i = accounts.Exact(m::LookupItem{m::Trim(a.account), m::Trim(a.dept), ""}.key());
    return i >= 0 ? accounts.items()[static_cast<size_t>(i)].name : std::string();
}

// ---- AP ---------------------------------------------------------------------------

ApVoucherModel::ApVoucherModel(cb4sqlite::Code4& code, ApKind kind, TaxMode mode) : code_(code), kind_(kind), mode_(mode)
{
    lookups_.Load(code_);
    setat_ = LoadApAccounts(code_);
    std::vector<m::GridColumn> cols = {
        {"account", "Account", m::CellKind::Lookup, 7}, {"dept", "Dept", m::CellKind::Upper, 4},
        {"descript", "Description", m::CellKind::Text, 24}, {"amount", "Amount", m::CellKind::Amount, 12, false, true},
        {"medrep", "Med rep", m::CellKind::Lookup, 5},  {"prod", "Product", m::CellKind::Lookup, 7},
        {"doctor", "Doctor", m::CellKind::Lookup, 5},
    };
    cols[0].validator.Add(inTable(&lookups_.accounts, "Not an account in the chart."));
    cols[2].validator.Add(m::MaxLength(40));
    cols[4].validator.Add(inTable(&lookups_.reps, "Not a medical rep's code."));
    cols[5].validator.Add(inTable(&lookups_.products, "Not a product code."));
    cols[6].validator.Add(inTable(&lookups_.doctors, "Not a doctor's code."));
    grid_.SetColumns(std::move(cols));
}

void ApVoucherModel::New(const std::string& today)
{
    periods_ = OpenPeriods(code_);
    number = 0;
    vendor.clear();
    vendorName.clear();
    invoiceNo.clear();
    orderNo.clear();
    reference.clear();
    cheque.clear();
    invoiceDate = today;
    dueDate.clear();
    period = periods_.empty() ? 0 : periods_.back().number;
    terms = vatRate = whRate = 0;
    individual = -1;
    credit = kind_ == ApKind::Invoice ? setat_.ap : (setat_.banks.empty() ? ApAccount{} : setat_.banks.front().account);
    vat = setat_.vatInput;
    savedLines_ = 0;
    grid_.Clear();
}

bool ApVoucherModel::SelectVendor(const std::string& no)
{
    const std::optional<Vendor> v = FindVendor(code_, m::Trim(no));
    if (!v) return false;
    vendor = TrimText(v->number);
    vendorName = TrimText(v->name);
    if (kind_ == ApKind::Invoice) credit = v->ap;
    vat = v->vat;
    vatRate = v->vatRate;
    whRate = v->whRate;
    terms = v->terms;
    individual = v->individual() ? 1 : 0;
    const std::string due = AddDays(invoiceDate, terms);
    if (!due.empty() && kind_ == ApKind::Invoice) dueDate = due;
    return true;
}

ApVoucherModel::Open ApVoucherModel::Load(long voucher)
{
    periods_ = OpenPeriods(code_);
    const std::optional<ApVoucher> v = FindApVoucher(code_, kind_, voucher);
    if (!v) return Open::NotFound;
    if (v->mode != ap_mode::kOpen) return Open::NotOpen;
    number = v->number;
    vendor = TrimText(v->vendor);
    vendorName = TrimText(v->vendorName);
    invoiceNo = TrimText(v->invoiceNo);
    orderNo = TrimText(v->orderNo);
    reference = TrimText(v->reference);
    invoiceDate = v->invoiceDate;
    dueDate = v->dueDate;
    cheque = TrimText(v->cheque);
    period = v->period;
    terms = v->terms;
    vatRate = v->vatRate;
    whRate = v->whRate;
    credit = v->account;
    if (const std::optional<Vendor> ven = FindVendor(code_, vendor)) {
        individual = ven->individual() ? 1 : 0;
        vat = ven->vat;
    }
    std::vector<std::vector<std::string>> rows;
    for (const ApLine& l : LoadApLines(code_, kind_, voucher)) {
        if (l.role == ApLineRole::Vat) vat = l.account;   // the voucher's own VAT account
        if (l.role != ApLineRole::Expense) continue;
        rows.push_back({TrimText(l.account.account), TrimText(l.account.dept), TrimText(l.descript), cents(l.gross),
                        TrimText(l.medrep), TrimText(l.product), TrimText(l.drcode)});
    }
    grid_.SetRows(rows);
    savedLines_ = static_cast<int>(rows.size());
    return Open::Opened;
}

VoucherAccounts ApVoucherModel::Accounts() const
{
    VoucherAccounts a;
    a.vat = vat;
    a.wh = individual == 1 ? setat_.whIndividual : setat_.whCompany;   // GL_SETAT WTXI / WTXC
    a.credit = credit;
    a.vatText = lookups_.AccountName(a.vat);
    a.whText = lookups_.AccountName(a.wh);
    a.creditText = lookups_.AccountName(a.credit);
    return a;
}

std::vector<ApLine> ApVoucherModel::ExpenseLines(EntryResult* refusal) const
{
    std::vector<ApLine> out;
    for (int r = 0; r < grid_.Rows(); ++r) {
        if (grid_.IsBlank(r)) continue;
        ApLine l;
        l.account = {grid_.Cell(r, "account"), grid_.Cell(r, "dept")};
        l.descript = grid_.Cell(r, "descript");
        l.medrep = grid_.Cell(r, "medrep");
        l.product = grid_.Cell(r, "prod");
        l.drcode = grid_.Cell(r, "doctor");
        l.gross = centsOf(grid_.Cell(r, "amount"));
        if (refusal) {
            if (!lookups_.HasAccount(l.account)) {   // ValidateGL, corrected
                *refusal = refuseCell(r, l.account.dept.empty() ? "account" : "dept", "Not an account and dept in the chart.");
                return {};
            }
            if (l.gross <= 0) {
                *refusal = refuseCell(r, "amount", "Enter the line's amount, VAT included.");
                return {};
            }
        }
        out.push_back(std::move(l));
    }
    return out;
}

ComputedVoucher ApVoucherModel::Split() const
{
    std::vector<ApLine> lines = ExpenseLines(nullptr);
    lines.erase(std::remove_if(lines.begin(), lines.end(), [](const ApLine& l) { return l.gross <= 0; }), lines.end());
    return ComputeVoucher(lines, vatRate, whRate, mode_, Accounts());
}

EntryResult ApVoucherModel::Save()
{
    if (m::Trim(vendor).empty()) return refuse("vendor", "Choose the vendor.");
    if (!FindVendor(code_, vendor)) return refuse("vendor", "There is no vendor " + vendor + ": add them in Purchase Vendors.");
    if (m::Trim(invoiceNo).empty()) return refuse("invoiceNo", "Type the invoice number.");
    if (invoiceDate.size() != 8) return refuse("invoiceDate", "Choose the invoice date.");
    if (kind_ == ApKind::Invoice && dueDate.empty()) dueDate = AddDays(invoiceDate, terms);
    if (!periodOpen(periods_, period)) return refuse("period", "Choose an open period.");
    if (m::Trim(reference).empty()) return refuse("reference", "Type the document.");
    if (individual < 0) return refuse("type", "Choose Company or Individual: the withholding tax account depends on it.");
    const VoucherAccounts accounts = Accounts();
    if (kind_ == ApKind::Cash && accounts.credit.empty()) return refuse("credit", "Choose the bank the invoice was paid from.");
    if (!ValidateAccount(code_, accounts.credit.account, accounts.credit.dept))
        return refuse("credit", kind_ == ApKind::Cash ? "The bank account isn't in the chart of accounts."
                                                      : "The AP account isn't in the chart of accounts.");
    if (vatRate < 0 || vatRate > 99) return refuse("vatRate", "A rate must be a whole percentage from 0 to 99.");
    if (whRate < 0 || whRate > 99) return refuse("whRate", "A rate must be a whole percentage from 0 to 99.");
    if (vatRate > 0 && accounts.vat.empty()) return refuse("vat", "The VAT percentage is above zero but there is no VAT account.");
    if (vatRate > 0 && !ValidateAccount(code_, accounts.vat.account, accounts.vat.dept))
        return refuse("vat", "The VAT account isn't in the chart of accounts.");
    if (whRate > 0 && !ValidateAccount(code_, accounts.wh.account, accounts.wh.dept))
        return refuse("whRate", std::string("The ") + (individual == 1 ? "individual" : "company") +
                                    " withholding tax account (" + (individual == 1 ? "WTXI" : "WTXC") +
                                    " in Company Profile > Account Setup) isn't set, or isn't in the chart of accounts.");
    EntryResult refusal{true};
    const std::vector<ApLine> lines = ExpenseLines(&refusal);
    if (!refusal.ok) return refusal;
    if (lines.empty()) return refuseCell(0, "account", "Type the voucher's lines.");
    if (const auto dup = DuplicateInvoice(code_, vendor, invoiceNo,
                                          isNew() ? std::nullopt : std::optional<ApVoucherRef>(ApVoucherRef{kind_, number})))
        return refuse("invoiceNo", "Invoice number has been used previously: vendor " + vendor + "'s invoice " +
                                       invoiceNo + " is on " + (dup->kind == ApKind::Cash ? "cash" : "invoice") +
                                       " voucher " + std::to_string(dup->number) + ".");

    ApVoucher h;
    h.kind = kind_;
    h.number = number;
    h.vendor = vendor;
    h.vendorName = vendorName;
    h.invoiceNo = m::Trim(invoiceNo);
    h.orderNo = m::Trim(orderNo);
    h.reference = m::Trim(reference);
    h.date = invoiceDate;
    h.invoiceDate = invoiceDate;
    h.dueDate = kind_ == ApKind::Cash ? invoiceDate : dueDate;   // paid when entered
    if (kind_ == ApKind::Cash) {
        h.cheque = m::Trim(cheque);
        h.vat = accounts.vat;
    }
    h.period = period;
    h.terms = terms;
    h.vatRate = vatRate;
    h.whRate = whRate;
    h.account = accounts.credit;
    long voucher = number;
    int i = 0;
    for (const ApLine& l : lines) {   // as Add does, a line at a time
        ExpenseEntry e;
        e.line = ++i <= savedLines_ ? i : 0;
        e.descript = l.descript;
        e.medrep = l.medrep;
        e.drcode = l.drcode;
        e.product = l.product;
        e.account = l.account;
        e.gross = l.gross;
        h.number = voucher;
        voucher = SaveExpenseLine(code_, h, e, mode_, accounts);
        if (voucher == 0) return refuse("", "Voucher " + std::to_string(number) + " is no longer open: it can't be changed.");
    }
    for (int j = savedLines_; j > static_cast<int>(lines.size()); --j)
        DeleteExpenseLine(code_, kind_, voucher, j, mode_, accounts);
    Load(voucher);
    return {true, "", -1, "", "Voucher " + std::to_string(voucher) + " saved."};
}

EntryResult ApVoucherModel::Delete()
{
    if (isNew()) return refuse("", "The voucher isn't saved.");
    const long voucher = number;
    if (!DeleteApVoucher(code_, kind_, voucher)) return refuse("", "Voucher " + std::to_string(voucher) + " isn't open.");
    New(invoiceDate);
    return {true, "", -1, "", "Voucher " + std::to_string(voucher) + " deleted."};
}

bool ApVoucherModel::CanPost() const
{
    return !isNew() && grid_.Lines() > 0;
}

EntryResult ApVoucherModel::Post()
{
    if (isNew()) return refuse("", "Save the voucher first.");
    const long voucher = number;
    const ApPostResult r = PostApVouchers(code_, kind_, voucher);
    if (std::find(r.posted.begin(), r.posted.end(), voucher) != r.posted.end()) {
        New(invoiceDate);
        return {true, "", -1, "", "Voucher " + std::to_string(voucher) + " posted."};
    }
    return refuse("", refusalText(r, voucher, "Voucher"));
}

// ---- AR ---------------------------------------------------------------------------

ArInvoiceModel::ArInvoiceModel(cb4sqlite::Code4& code) : code_(code)
{
    lookups_.Load(code_);
    setat_ = LoadArAccounts(code_);
    std::vector<m::GridColumn> cols = {
        {"prod", "Product", m::CellKind::Lookup, 7},   {"account", "Account", m::CellKind::Lookup, 7},
        {"dept", "Dept", m::CellKind::Upper, 4},       {"descript", "Description", m::CellKind::Text, 22},
        {"qty", "Quantity", m::CellKind::Amount, 8},   {"price", "Unit price", m::CellKind::Amount, 10},
        {"amount", "Amount", m::CellKind::Amount, 12, false, true},
        {"medrep", "Med rep", m::CellKind::Lookup, 5}, {"doctor", "Doctor", m::CellKind::Lookup, 5},
    };
    cols[0].validator.Add(inTable(&lookups_.products, "Not a product code."));
    cols[1].validator.Add(inTable(&lookups_.accounts, "Not an account in the chart."));
    cols[3].validator.Add(m::MaxLength(40));
    cols[7].validator.Add(inTable(&lookups_.reps, "Not a medical rep's code."));
    cols[8].validator.Add(inTable(&lookups_.doctors, "Not a doctor's code."));
    grid_.SetColumns(std::move(cols));
    grid_.OnCommit = [this](int row, int col) {
        const std::string& key = grid_.columns()[static_cast<size_t>(col)].key;
        if (key == "prod" || key == "qty" || key == "price") {
            if (grid_.Cell(row, "account").empty()) {   // a new line: on the customer's sales account
                grid_.SetCell(row, "account", sales.account);
                grid_.SetCell(row, "dept", sales.dept);
            }
        }
        if (key == "prod" && grid_.Cell(row, "descript").empty()) {   // GetSalesDescription: the product's name
            const int i = lookups_.products.Exact(grid_.Cell(row, "prod"));
            if (i >= 0) grid_.SetCell(row, "descript", lookups_.products.items()[static_cast<size_t>(i)].name);
        }
        if ((key == "qty" || key == "price") && centsOf(grid_.Cell(row, "price")) > 0)
            grid_.SetCell(row, "amount",
                          cents(LineAmount(centsOf(grid_.Cell(row, "qty")), centsOf(grid_.Cell(row, "price")), 0)));
    };
}

void ArInvoiceModel::New(const std::string& today)
{
    periods_ = OpenPeriods(code_);
    number = 0;
    customer.clear();
    invoiceNo.clear();
    orderNo.clear();
    orderDate.clear();
    vin.clear();
    units.clear();
    reference.clear();
    memo.clear();
    savedMemo_.clear();
    invoiceDate = today;
    dueDate.clear();
    period = periods_.empty() ? 0 : periods_.back().number;
    terms = vatRate = 0;
    ar = setat_.ar;   // GL_SETAT, until a customer is chosen (spec #6 D12)
    sales = setat_.sales;
    vat = setat_.vatPayable;
    savedLines_ = 0;
    grid_.Clear();
}

bool ArInvoiceModel::SelectCustomer(const std::string& no)
{
    const std::optional<Customer> c = FindCustomer(code_, m::Trim(no));
    if (!c) return false;
    customer = TrimText(c->number);
    ar = c->ar;
    sales = c->sales;
    vat = c->vat;
    vin = TrimText(c->vin);
    vatRate = c->vatRate;
    terms = c->terms;
    const std::string due = AddDays(invoiceDate, terms);
    if (!due.empty()) dueDate = due;
    return true;
}

ArInvoiceModel::Open ArInvoiceModel::Load(long voucher)
{
    periods_ = OpenPeriods(code_);
    const std::optional<ArVoucher> v = FindArVoucher(code_, voucher);
    if (!v) return Open::NotFound;
    if (v->mode != ar_mode::kOpen) return Open::NotOpen;
    number = v->number;
    customer = TrimText(v->customer);
    invoiceNo = TrimText(v->invoiceNo);
    orderNo = TrimText(v->orderNo);
    orderDate = v->orderDate;
    vin = TrimText(v->vin);
    units = TrimText(v->units);
    reference = TrimText(v->reference);
    invoiceDate = v->invoiceDate;
    dueDate = v->dueDate;
    memo = savedMemo_ = v->memo;
    period = v->period;
    terms = v->terms;
    vatRate = v->vatRate;
    ar = v->account;
    if (const std::optional<Customer> c = FindCustomer(code_, customer)) {
        vat = c->vat;
        sales = c->sales;
    }
    std::vector<std::vector<std::string>> rows;
    for (const ArLine& l : LoadArLines(code_, voucher)) {
        if (l.role == ArLineRole::Vat) vat = l.account;
        if (l.role != ArLineRole::Sales) continue;
        rows.push_back({TrimText(l.product), TrimText(l.account.account), TrimText(l.account.dept), TrimText(l.descript),
                        cents(l.qty), cents(l.price), cents(LineAmount(l.qty, l.price, l.gross)), TrimText(l.medrep),
                        TrimText(l.drcode)});
    }
    grid_.SetRows(rows);
    savedLines_ = static_cast<int>(rows.size());
    return Open::Opened;
}

std::vector<ArLine> ArInvoiceModel::ProductLines(EntryResult* refusal) const
{
    std::vector<ArLine> out;
    for (int r = 0; r < grid_.Rows(); ++r) {
        if (grid_.IsBlank(r)) continue;
        ArLine l;
        l.product = grid_.Cell(r, "prod");
        l.account = {grid_.Cell(r, "account"), grid_.Cell(r, "dept")};
        l.descript = grid_.Cell(r, "descript");
        l.qty = centsOf(grid_.Cell(r, "qty"));
        l.price = centsOf(grid_.Cell(r, "price"));
        l.gross = l.price > 0 ? 0 : centsOf(grid_.Cell(r, "amount"));
        l.medrep = grid_.Cell(r, "medrep");
        l.drcode = grid_.Cell(r, "doctor");
        if (refusal) {
            if (!lookups_.HasAccount(l.account)) {   // ValidGLAccount, per line (6089)
                *refusal = refuseCell(r, l.account.dept.empty() ? "account" : "dept", "Not a sales account and dept in the chart.");
                return {};
            }
            if (l.qty < 0 || l.price < 0) {
                *refusal = refuseCell(r, l.qty < 0 ? "qty" : "price", "The quantity and price can't be negative.");
                return {};
            }
            if (l.price > 0 && l.qty == 0) {
                *refusal = refuseCell(r, "qty", "Enter the quantity sold at this price.");
                return {};
            }
            if (LineAmount(l.qty, l.price, l.gross) <= 0) {
                *refusal = refuseCell(r, l.price > 0 ? "qty" : "amount",
                                      "Enter a quantity and unit price, or the line's amount, VAT included.");
                return {};
            }
        }
        out.push_back(std::move(l));
    }
    return out;
}

ComputedSale ArInvoiceModel::Split() const
{
    std::vector<ArLine> lines = ProductLines(nullptr);
    lines.erase(std::remove_if(lines.begin(), lines.end(),
                               [](const ArLine& l) { return LineAmount(l.qty, l.price, l.gross) <= 0; }),
                lines.end());
    SaleAccounts a;
    a.vat = vat;
    a.ar = ar;
    a.vatText = lookups_.AccountName(vat);
    a.arText = lookups_.AccountName(ar);
    return ComputeSale(lines, vatRate, a);
}

EntryResult ArInvoiceModel::Save()
{
    if (m::Trim(customer).empty()) return refuse("customer", "Choose the customer.");
    if (!FindCustomer(code_, customer)) return refuse("customer", "There is no customer " + customer + ": add them in Sales Customers.");
    if (m::Trim(invoiceNo).empty()) return refuse("invoiceNo", "Type the invoice number.");
    if (invoiceDate.size() != 8) return refuse("invoiceDate", "Choose the invoice date.");
    if (dueDate.empty()) dueDate = AddDays(invoiceDate, terms);
    if (!periodOpen(periods_, period)) return refuse("period", "Choose an open period.");
    if (m::Trim(reference).empty()) return refuse("reference", "Type the document.");
    if (!ValidateAccount(code_, ar.account, ar.dept)) return refuse("ar", "The AR account isn't in the chart of accounts.");
    if (vatRate < 0 || vatRate > 99) return refuse("vatRate", "The VAT rate must be a whole percentage from 0 to 99.");
    if (vatRate > 0 && vat.empty())
        return refuse("vat", "The VAT rate is above zero but there is no VAT account: enter the output VAT account.");
    if (vatRate > 0 && !ValidateAccount(code_, vat.account, vat.dept))
        return refuse("vat", "The VAT account isn't in the chart of accounts.");
    EntryResult refusal{true};
    const std::vector<ArLine> lines = ProductLines(&refusal);
    if (!refusal.ok) return refusal;
    if (lines.empty()) return refuseCell(0, "prod", "Type the voucher's lines.");
    if (const long dup = DuplicateArInvoice(code_, customer, invoiceNo, number))
        return refuse("invoiceNo", "Invoice number has been used previously: customer " + customer + "'s invoice " +
                                       invoiceNo + " is on sales voucher " + std::to_string(dup) + ".");

    ArVoucher h;
    h.number = number;
    h.customer = customer;
    h.invoiceNo = m::Trim(invoiceNo);
    h.orderNo = m::Trim(orderNo);
    h.orderDate = orderDate;
    h.vin = m::Trim(vin);
    h.units = m::Trim(units);
    h.reference = m::Trim(reference);
    h.date = invoiceDate;
    h.invoiceDate = invoiceDate;
    h.dueDate = dueDate;
    h.period = period;
    h.terms = terms;
    h.vatRate = vatRate;
    h.account = ar;
    long voucher = number;
    int i = 0;
    for (const ArLine& l : lines) {
        SaleEntry e;
        e.line = ++i <= savedLines_ ? i : 0;
        e.descript = l.descript;
        e.medrep = l.medrep;
        e.drcode = l.drcode;
        e.product = l.product;
        e.account = l.account;
        e.qty = l.qty;
        e.price = l.price;
        e.gross = l.gross;
        h.number = voucher;
        voucher = SaveSaleLine(code_, h, e, vat);
        if (voucher == 0) return refuse("", "Voucher " + std::to_string(number) + " is no longer open: it can't be changed.");
    }
    for (int j = savedLines_; j > static_cast<int>(lines.size()); --j) DeleteSaleLine(code_, voucher, j, vat);
    if (memo != savedMemo_) SaveArMemo(code_, voucher, memo);
    Load(voucher);
    return {true, "", -1, "", "Sales voucher " + std::to_string(voucher) + " saved."};
}

EntryResult ArInvoiceModel::Delete()
{
    if (isNew()) return refuse("", "The voucher isn't saved.");
    const long voucher = number;
    if (!DeleteArVoucher(code_, voucher)) return refuse("", "Sales voucher " + std::to_string(voucher) + " isn't open.");
    New(invoiceDate);
    return {true, "", -1, "", "Sales voucher " + std::to_string(voucher) + " deleted."};
}

bool ArInvoiceModel::CanPost() const
{
    return !isNew() && grid_.Lines() > 0;
}

EntryResult ArInvoiceModel::Post()
{
    if (isNew()) return refuse("", "Save the voucher first.");
    const long voucher = number;
    const ApPostResult r = PostArVouchers(code_, voucher);
    if (std::find(r.posted.begin(), r.posted.end(), voucher) != r.posted.end()) {
        New(invoiceDate);
        return {true, "", -1, "", "Sales voucher " + std::to_string(voucher) + " posted."};
    }
    return refuse("", refusalText(r, voucher, "Sales voucher"));
}

// ---- receipts ---------------------------------------------------------------------

void ReceiptsModel::Load(bool all)
{
    all_ = all;
    items_ = ReceiptItems(code_, all);
    banks_ = LoadArAccounts(code_).banks;
    if (selected_) Select(selected_->item.batchno);
}

std::vector<size_t> ReceiptsModel::Filter(const std::string& text) const
{
    std::vector<m::LookupItem> items;
    for (const ReceiptItem& r : items_)
        items.push_back({TrimText(r.item.customer), "", r.customerName + " " + TrimText(r.item.invoiceNo)});
    return m::LookupModel(std::move(items)).Match(text);
}

bool ReceiptsModel::Select(long batchno)
{
    selected_ = FindReceiptItem(code_, batchno);
    return selected_.has_value();
}

EntryResult ReceiptsModel::Record()
{
    if (!selected_) return refuse("", "Choose the invoice the receipt is for.");
    if (date.size() != 8) return refuse("date", "Choose the receipt's date.");
    if (bank < 0 || static_cast<size_t>(bank) >= banks_.size()) return refuse("bank", "Choose the bank it was paid into.");
    const auto received = m::ParseAmount(m::Trim(amount));
    if (!received) return refuse("amount", "Not an amount.");
    const long batchno = selected_->item.batchno;
    const ReceiptOutcome o =
        RecordReceipt(code_, batchno, date, banks_[static_cast<size_t>(bank)].account, *received, discount, m::Trim(reference));
    switch (o.result) {
    case ReceiptResult::Recorded: {
        Load(all_);
        Select(batchno);
        std::string text = "Receipt " + std::to_string(o.number) + " recorded";
        if (o.discount) text += ", with a discount of " + m::FormatAmount(o.discount);
        text += o.settled ? ". The invoice is paid." : ".";
        amount.clear();
        discount = false;
        return {true, "", -1, "", text};
    }
    case ReceiptResult::NotFound: return refuse("", "There is no open item for that voucher.");
    case ReceiptResult::Settled: return refuse("", "Nothing is owed on that invoice.");
    case ReceiptResult::Zero: return refuse("amount", "Enter the amount received.");
    case ReceiptResult::TooMuch: return refuse("amount", "That's more than is owed.");
    case ReceiptResult::NoDiscount: return refuse("discount", "The whole amount was received: there's nothing to discount.");
    case ReceiptResult::ClosedPeriod: return refuse("date", "The date isn't in an open period.");
    case ReceiptResult::UnknownBank: return refuse("bank", "The bank account isn't in the chart of accounts.");
    }
    return refuse("", "The receipt wasn't recorded.");
}

EntryResult ReceiptsModel::Reverse()
{
    if (!selected_) return refuse("", "Choose the invoice whose receipt is to be reversed.");
    if (!HasReceipt()) return refuse("", "Nothing has been received on that invoice.");
    if (date.size() != 8) return refuse("date", "Choose the reversal's date.");
    const long batchno = selected_->item.batchno;
    const std::vector<Receipt> receipts = LoadReceipts(code_, batchno);
    const long number = receipts.empty() ? 0 : receipts.back().number;
    switch (ReverseReceipt(code_, batchno, date)) {
    case ReverseResult::Reversed:
        Load(all_);
        Select(batchno);
        return {true, "", -1, "",
                (number ? "Receipt " + std::to_string(number) : std::string("The receipt")) + " reversed: " +
                    m::FormatAmount(selected_ ? selected_->item.remainder : 0) + " is owed on the invoice."};
    case ReverseResult::NoReceipt: return refuse("", "That invoice has no receipt to reverse.");
    case ReverseResult::ClosedPeriod:
        return refuse("date", "The receipt has been transferred to the GL: choose a date in an open period for the reversal.");
    }
    return refuse("", "The receipt wasn't reversed.");
}
