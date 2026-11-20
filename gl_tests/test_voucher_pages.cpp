//
// gl_tests: the voucher pages' models (spec #8 5.2; plan S8-M3 task 7),
// through the Spec #2, #5 and #6 sheets' entry criteria, each compared with
// the rows the classic screens' calls write.
//
#include "GlTestDb.h"

#include "Accounts.h"
#include "Vouchers.h"
#include "pages/GlVoucherModel.h"
#include "pages/SubledgerModels.h"

#include <algorithm>
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

// Every live row of `table` as text, without the update / user / time stamps, sorted.
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

// Types a cell from empty and presses Tab, as the keyboard would.
void typeCell(m::EntryGridModel& g, const std::string& text)
{
    if (!text.empty()) {
        REQUIRE(g.BeginEdit(false));
        g.SetEditText(text);
    }
    const m::GridAction a = g.Key(m::GridKey::Tab);
    INFO(text << ": " << g.message());
    REQUIRE(a != m::GridAction::Refused);
}

struct TypedLine {
    std::string account, dept, doc, debit, credit;
};

// One line, keyboard only: account, dept, document, (the name filled), debit,
// credit, and Tab past the codes to the next line.
void typeLine(m::EntryGridModel& g, const TypedLine& l)
{
    typeCell(g, l.account);
    typeCell(g, l.dept);
    typeCell(g, l.doc);
    typeCell(g, "");   // the account's name, filled in
    typeCell(g, l.debit);
    typeCell(g, l.credit);
    typeCell(g, "");
    typeCell(g, "");
    typeCell(g, "");
}

const std::vector<TypedLine> kFiveLines = {
    {"101000", "", "DOC-1", "100.00", ""},  {"102101", "", "DOC-1", "200.50", ""}, {"111101", "", "DOC-1", "49.50", ""},
    {"410001", "10", "DOC-1", "", "300.00"}, {"410001", "20", "DOC-1", "", "50.00"},
};

} // namespace

TEST_CASE("GL voucher page: five lines typed, saved and posted as through Voucher Entry (S8-M3)", "[voucherpages]")
{
    FixtureDb page, classic;
    GlVoucherModel model(page.code, false, 0);
    model.New("20261020");
    REQUIRE(model.period != 0);
    const int period = model.period;
    const long expected = model.nextNumber();
    model.description = "Five lines";
    for (const TypedLine& l : kFiveLines) typeLine(model.grid(), l);
    CHECK(model.grid().Lines() == 5);
    CHECK(model.grid().Cell(0, "name") == model.AccountName("101000", ""));   // filled from the chart
    CHECK(model.grid().Total("debit") == 35000);
    CHECK(model.grid().Balances("debit", "credit"));
    CHECK_FALSE(model.CanPost());   // not saved yet

    GlVoucherModel::Result r = model.Save();
    INFO(r.message);
    REQUIRE(r.ok);
    CHECK(model.number == expected);
    CHECK(model.CanPost());

    // The classic screen's calls on the other copy: SaveLine per line, as Add/Mod does.
    {
        VoucherHeaders h(classic.code);
        VoucherLines l(classic.code);
        VoucherHeader header;
        header.mode = voucher_mode::kOpen;
        header.description = "Five lines";
        header.date = "20261020";
        header.period = period;
        long number = 0;
        int i = 0;
        for (const TypedLine& t : kFiveLines) {
            VoucherLine v;
            v.line = ++i;
            v.period = period;
            v.date = "20261020";
            v.reference = t.doc;
            v.descript = model.AccountName(t.account, t.dept);
            v.account = t.account;
            v.dept = t.dept;
            v.debit = t.debit.empty() ? 0 : std::stod(t.debit);
            v.credit = t.credit.empty() ? 0 : std::stod(t.credit);
            number = SaveLine(h, l, number, header, v, 0);
        }
        CHECK(number == expected);
        VoucherHeader shown;
        std::vector<VoucherLine> lines;
        LoadVoucher(h, l, number, shown, lines);   // Voucher Entry shows it after each line
    }
    CHECK(snapshot(page.code, "gl_btch") == snapshot(classic.code, "gl_btch"));
    CHECK(snapshot(page.code, "gl_btchd") == snapshot(classic.code, "gl_btchd"));

    r = model.Post();
    INFO(r.message);
    REQUIRE(r.ok);
    CHECK(model.isNew());
    const PostResult classicPost = PostVoucher(classic.code, expected);
    REQUIRE(classicPost.posted == std::vector<long>{expected});
    CHECK(snapshot(page.code, "gl_dist") == snapshot(classic.code, "gl_dist"));
    CHECK(snapshot(page.code, "glaccts") == snapshot(classic.code, "glaccts"));
    CHECK(snapshot(page.code, "gl_btchd") == snapshot(classic.code, "gl_btchd"));
}

TEST_CASE("GL voucher page: Voucher Entry's refusals, editing a voucher, modes (S8-M3)", "[voucherpages]")
{
    FixtureDb db;
    GlVoucherModel model(db.code, false, 0);
    model.New("20261020");
    for (const TypedLine& l : kFiveLines) typeLine(model.grid(), l);

    GlVoucherModel::Result r = model.Save();
    CHECK(r.field == "description");
    model.description = "Refusals";
    model.date.clear();
    CHECK(model.Save().field == "date");
    model.date = "20261020";
    model.period = 99;
    CHECK(model.Save().field == "period");
    model.period = model.periods().back().number;

    model.grid().SetCell(1, "doc", "");
    r = model.Save();
    CHECK(r.row == 1);
    CHECK(r.column == "doc");
    model.grid().SetCell(1, "doc", "DOC-1");
    model.grid().SetCell(3, "dept", "");   // 410001 has only depts 10 and 20
    r = model.Save();
    CHECK(r.row == 3);
    CHECK(r.column == "account");
    model.grid().SetCell(3, "dept", "10");
    model.grid().SetCell(2, "credit", "1.00");   // both
    CHECK(model.Save().column == "credit");
    model.grid().SetCell(2, "credit", "");
    model.grid().SetCell(2, "debit", "");          // neither
    CHECK(model.Save().column == "debit");
    model.grid().SetCell(2, "debit", "49.50");
    REQUIRE(model.Save().ok);
    const long saved = model.number;

    // Editing it: a line removed (Ctrl+Delete) and saved; the voucher has four lines on file.
    REQUIRE(model.Load(saved) == GlVoucherModel::Open::Opened);
    model.grid().SetCurrent(0, 0);
    CHECK(model.grid().Key(m::GridKey::CtrlDelete) == m::GridAction::Removed);
    REQUIRE(model.Save().ok);
    {
        VoucherHeaders h(db.code);
        CHECK(SeekVoucher(h, saved));
        CHECK(ReadHeader(h).lines == 4);
    }
    CHECK_FALSE(model.CanPost());   // out of balance now

    // The fixture's vouchers: 100 posted, 101 and 102 open, 103 an adjustment.
    CHECK(model.Load(100) == GlVoucherModel::Open::Posted);
    CHECK(model.Load(103) == GlVoucherModel::Open::WrongMode);
    CHECK(model.Load(999) == GlVoucherModel::Open::NotFound);
    REQUIRE(model.Load(101) == GlVoucherModel::Open::Opened);
    CHECK(model.Neighbour(1) == 102);
    CHECK(model.Neighbour(-1) == 0);
    REQUIRE(model.Load(102) == GlVoucherModel::Open::Opened);
    CHECK_FALSE(model.CanPost());   // 100.00 against 90.00
    r = model.Post();
    CHECK_FALSE(r.ok);
    CHECK(r.message.find("doesn't balance") != std::string::npos);

    GlVoucherModel adjust(db.code, true, 0);
    CHECK(adjust.Load(101) == GlVoucherModel::Open::NotAdjustment);
    REQUIRE(adjust.Load(103) == GlVoucherModel::Open::Opened);
    adjust.New("20261020");
    adjust.description = "Adjustment";
    typeLine(adjust.grid(), {"101000", "", "ADJ", "5.00", ""});
    typeLine(adjust.grid(), {"102101", "", "ADJ", "", "5.00"});
    REQUIRE(adjust.Save().ok);
    VoucherHeaders h(db.code);
    REQUIRE(SeekVoucher(h, adjust.number));
    CHECK(ReadHeader(h).mode == voucher_mode::kAdjustment);

    REQUIRE(model.Load(saved) == GlVoucherModel::Open::Opened);
    REQUIRE(model.Delete().ok);
    CHECK_FALSE(SeekVoucher(h, saved));
}

TEST_CASE("Purchase invoice page: Spec #5's invoice with VAT and W/H, as through Invoice Entry (S8-M3)", "[voucherpages]")
{
    FixtureDb page, classic;
    ApVoucherModel model(page.code, ApKind::Invoice, TaxMode::F);
    model.New("20261020");
    REQUIRE(model.SelectVendor("V001"));   // a company, VAT 12, W/H 1
    CHECK(model.individual == 0);
    model.invoiceNo = "INV-1001";          // V001's own: refused as a duplicate
    model.reference = "PO-77";
    typeCell(model.grid(), "501000");
    typeCell(model.grid(), "");
    typeCell(model.grid(), "Supplies");
    typeCell(model.grid(), "1,120.00");
    for (int i = 0; i < 3; ++i) typeCell(model.grid(), "");
    typeCell(model.grid(), "501000");
    typeCell(model.grid(), "");
    typeCell(model.grid(), "");
    typeCell(model.grid(), "560.00");

    // The split as the lines are typed (D4, F mode): VAT in the amounts, W/H on the net.
    const ComputedVoucher split = model.Split();
    CHECK(split.payable == 168000);
    CHECK(split.vat == 18000);
    CHECK(split.wh == 1500);
    CHECK(split.credit == 166500);
    CHECK(split.debits == split.credits);

    EntryResult r = model.Save();
    CHECK_FALSE(r.ok);
    CHECK(r.field == "invoiceNo");
    CHECK(r.message.find("invoice voucher 1") != std::string::npos);
    model.invoiceNo = "INV-1010";
    r = model.Save();
    INFO(r.message);
    REQUIRE(r.ok);
    const long number = model.number;

    // Invoice Entry's calls on the other copy: SaveExpenseLine per line.
    {
        ApVoucherModel shape(classic.code, ApKind::Invoice, TaxMode::F);   // only for the vendor's defaults
        shape.New("20261020");
        REQUIRE(shape.SelectVendor("V001"));
        ApVoucher h;
        h.kind = ApKind::Invoice;
        h.vendor = "V001";
        h.vendorName = shape.vendorName;
        h.invoiceNo = "INV-1010";
        h.reference = "PO-77";
        h.date = h.invoiceDate = "20261020";
        h.dueDate = shape.dueDate;
        h.period = shape.period;
        h.terms = shape.terms;
        h.vatRate = 12;
        h.whRate = 1;
        h.account = shape.credit;
        const ApAccounts setat = LoadApAccounts(classic.code);
        VoucherAccounts a;
        a.vat = shape.vat;
        a.wh = setat.whCompany;
        a.credit = shape.credit;
        a.vatText = shape.lookups().AccountName(a.vat);
        a.whText = shape.lookups().AccountName(a.wh);
        a.creditText = shape.lookups().AccountName(a.credit);
        ExpenseEntry e1;
        e1.account = {"501000", ""};
        e1.descript = "Supplies";
        e1.gross = 112000;
        h.number = SaveExpenseLine(classic.code, h, e1, TaxMode::F, a);
        CHECK(h.number == number);
        ExpenseEntry e2;
        e2.account = {"501000", ""};
        e2.gross = 56000;
        SaveExpenseLine(classic.code, h, e2, TaxMode::F, a);
    }
    for (const char* t : {"ap_btch", "ap_btchd", "ap_linex"}) {
        INFO(t);
        CHECK(snapshot(page.code, t) == snapshot(classic.code, t));
    }
    REQUIRE(model.Post().ok);
    REQUIRE(PostApVouchers(classic.code, ApKind::Invoice, number).posted == std::vector<long>{number});
    for (const char* t : {"ap_btch", "ap_dist", "ap_jsum"}) {
        INFO(t);
        CHECK(snapshot(page.code, t) == snapshot(classic.code, t));
    }
}

TEST_CASE("Sales invoice page: Spec #6's voucher 7, 3 x 1,100.00, and its receipts (S8-M3)", "[voucherpages]")
{
    FixtureDb page, classic;
    ArInvoiceModel model(page.code);
    model.New("20261020");
    REQUIRE(model.SelectCustomer("C001"));   // VAT 10
    CHECK(model.nextNumber() == 7);
    model.invoiceNo = "S-1007";
    model.reference = "DR-7";
    model.memo = "Deliver to the pharmacy wing.";
    for (int i = 0; i < 3; ++i) {
        typeCell(model.grid(), "");           // no product
        typeCell(model.grid(), model.sales.account);
        typeCell(model.grid(), model.sales.dept);
        typeCell(model.grid(), "");
        typeCell(model.grid(), "");           // no quantity
        typeCell(model.grid(), "");           // no price
        typeCell(model.grid(), "1,100.00");   // the amount, VAT included
        typeCell(model.grid(), "");
        typeCell(model.grid(), "");
    }
    const ComputedSale split = model.Split();
    CHECK(split.total == 330000);                 // Dr 3,300.00
    CHECK(split.vat == 30000);                    // Cr 300.00
    CHECK(split.credits - split.vat == 300000);   // Cr 3,000.00 of sales
    EntryResult r = model.Save();
    INFO(r.message);
    REQUIRE(r.ok);
    CHECK(model.number == 7);
    CHECK(TrimText(FindArVoucher(page.code, 7)->memo) == "Deliver to the pharmacy wing.");

    {
        ArInvoiceModel shape(classic.code);
        shape.New("20261020");
        REQUIRE(shape.SelectCustomer("C001"));
        ArVoucher h;
        h.customer = "C001";
        h.invoiceNo = "S-1007";
        h.vin = shape.vin;
        h.reference = "DR-7";
        h.date = h.invoiceDate = "20261020";
        h.dueDate = shape.dueDate;
        h.period = shape.period;
        h.terms = shape.terms;
        h.vatRate = 10;
        h.account = shape.ar;
        for (int i = 0; i < 3; ++i) {
            SaleEntry e;
            e.account = shape.sales;
            e.gross = 110000;
            h.number = SaveSaleLine(classic.code, h, e, shape.vat);
        }
        CHECK(h.number == 7);
        SaveArMemo(classic.code, 7, "Deliver to the pharmacy wing.");
    }
    for (const char* t : {"ar_btch", "ar_btchd", "ar_linex"}) {
        INFO(t);
        CHECK(snapshot(page.code, t) == snapshot(classic.code, t));
    }
    REQUIRE(model.Post().ok);
    REQUIRE(PostArVouchers(classic.code, 7).posted == std::vector<long>{7});

    // Its receipts: 1,000.00, then the rest less a discount of 100.00.
    ReceiptsModel receipts(page.code);
    receipts.Load();
    REQUIRE(receipts.Select(7));
    CHECK(receipts.selected()->item.remainder == 330000);
    receipts.date = "20261025";
    receipts.bank = 0;
    receipts.amount = "5,000.00";
    CHECK(receipts.Record().field == "amount");   // more than is owed
    receipts.amount = "1,000.00";
    r = receipts.Record();
    INFO(r.message);
    REQUIRE(r.ok);
    CHECK(receipts.selected()->item.remainder == 230000);
    receipts.amount = "2,200.00";
    receipts.discount = true;
    r = receipts.Record();
    REQUIRE(r.ok);
    CHECK(r.message.find("discount of 100.00") != std::string::npos);
    CHECK(receipts.selected()->item.remainder == 0);

    const std::vector<ApBank> banks = LoadArAccounts(classic.code).banks;
    REQUIRE_FALSE(banks.empty());
    CHECK(RecordReceipt(classic.code, 7, "20261025", banks[0].account, 100000, false).result == ReceiptResult::Recorded);
    CHECK(RecordReceipt(classic.code, 7, "20261025", banks[0].account, 220000, true).result == ReceiptResult::Recorded);
    for (const char* t : {"ar_btch", "ar_dist", "ar_jsum", "ar_rcpt"}) {
        INFO(t);
        CHECK(snapshot(page.code, t) == snapshot(classic.code, t));
    }
}
