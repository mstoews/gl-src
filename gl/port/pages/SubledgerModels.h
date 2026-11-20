//
// SubledgerModels.h - the AP and AR voucher pages' logic and the receipts
// page's, without a window (spec #8 5.2; plan S8-M3 task 5). Portable:
// the pages show them, and gl_tests drives them through the Spec #5 and #6
// sheets' entry criteria.
//
//   ApVoucherModel   Purchase Invoice Vouchers and Cash Invoice Vouchers
//                    (ApInvoiceEntry.cpp): expense lines typed in a grid, the
//                    VAT, W/H and AP (or bank) lines shown as they are typed
//                    (Ap.h ComputeVoucher), saved through SaveExpenseLine.
//   ArInvoiceModel   Sales Invoice Vouchers (ArInvoiceEntry.cpp): product lines
//                    with a quantity and price or an amount, the VAT and AR
//                    lines shown (Ar.h ComputeSale), saved through SaveSaleLine;
//                    the memo beside them.
//   ReceiptsModel    Clear Receipts (ArReceipts.cpp): the open items, and a
//                    receipt for the one picked (Ar.h RecordReceipt).
//
// The checks are the classic screens' Add, in their order. What changes
// (spec #8 D4, D9): the voucher is typed whole and saved at once, line by line
// through the same core calls (so the rows are the same), and there is no
// "save the current transaction?".
//
#pragma once

#include "../Ap.h"
#include "../Ar.h"
#include "../Vouchers.h"   // OpenPeriod
#include "GlVoucherModel.h"   // GlVoucherModel::Result
#include "cb4sqlite.h"
#include "model/EntryGridModel.h"
#include "model/LookupModel.h"

#include <optional>
#include <string>
#include <vector>

using EntryResult = GlVoucherModel::Result;

// The tables every voucher page looks codes up in.
struct EntryLookups {
    fx::model::LookupModel accounts, reps, products, doctors, vendors, customers;
    void Load(cb4sqlite::Code4& code);
    bool HasAccount(const ApAccount& a) const;
    std::string AccountName(const ApAccount& a) const;
};

class ApVoucherModel {
public:
    ApVoucherModel(cb4sqlite::Code4& code, ApKind kind, TaxMode mode);

    ApKind kind() const noexcept { return kind_; }
    fx::model::EntryGridModel& grid() noexcept { return grid_; }
    const fx::model::EntryGridModel& grid() const noexcept { return grid_; }
    const EntryLookups& lookups() const noexcept { return lookups_; }
    const std::vector<OpenPeriod>& periods() const noexcept { return periods_; }
    const std::vector<ApBank>& banks() const noexcept { return setat_.banks; }

    // ---- the header ----
    long number = 0;
    std::string vendor, vendorName, invoiceNo, orderNo, reference, invoiceDate, dueDate, cheque;
    int period = 0, terms = 0, vatRate = 0, whRate = 0;
    int individual = -1;   // -1 not chosen, 0 company, 1 individual
    ApAccount credit;      // the AP account (invoices) or the bank (cash)
    ApAccount vat;
    bool isNew() const noexcept { return number == 0; }
    long nextNumber() const { return NextApVoucherNumber(code_, kind_); }

    void New(const std::string& today);
    // The vendor's accounts, rates, terms and type (and the due date from the terms).
    bool SelectVendor(const std::string& number);
    enum class Open { Opened, NotFound, NotOpen };
    Open Load(long voucher);

    ComputedVoucher Split() const;   // the voucher's lines as typed so far
    EntryResult Save();
    EntryResult Delete();
    bool CanPost() const;
    EntryResult Post();

private:
    VoucherAccounts Accounts() const;
    std::vector<ApLine> ExpenseLines(EntryResult* refusal) const;

    cb4sqlite::Code4& code_;
    ApKind kind_;
    TaxMode mode_;
    ApAccounts setat_;
    EntryLookups lookups_;
    std::vector<OpenPeriod> periods_;
    fx::model::EntryGridModel grid_;
    int savedLines_ = 0;   // expense lines on file
};

class ArInvoiceModel {
public:
    explicit ArInvoiceModel(cb4sqlite::Code4& code);

    fx::model::EntryGridModel& grid() noexcept { return grid_; }
    const fx::model::EntryGridModel& grid() const noexcept { return grid_; }
    const EntryLookups& lookups() const noexcept { return lookups_; }
    const std::vector<OpenPeriod>& periods() const noexcept { return periods_; }

    long number = 0;
    std::string customer, invoiceNo, orderNo, orderDate, vin, units, reference, invoiceDate, dueDate, memo;
    int period = 0, terms = 0, vatRate = 0;
    ApAccount ar, vat, sales;   // sales: a new line's account
    bool isNew() const noexcept { return number == 0; }
    long nextNumber() const { return NextArVoucherNumber(code_); }

    void New(const std::string& today);
    bool SelectCustomer(const std::string& number);
    enum class Open { Opened, NotFound, NotOpen };
    Open Load(long voucher);

    ComputedSale Split() const;
    EntryResult Save();
    EntryResult Delete();
    bool CanPost() const;
    EntryResult Post();

private:
    std::vector<ArLine> ProductLines(EntryResult* refusal) const;

    cb4sqlite::Code4& code_;
    ArAccounts setat_;
    EntryLookups lookups_;
    std::vector<OpenPeriod> periods_;
    fx::model::EntryGridModel grid_;
    int savedLines_ = 0;
    std::string savedMemo_;
};

class ReceiptsModel {
public:
    explicit ReceiptsModel(cb4sqlite::Code4& code) : code_(code) {}

    void Load(bool all = false);
    const std::vector<ReceiptItem>& items() const noexcept { return items_; }
    std::vector<size_t> Filter(const std::string& text) const;   // customer, then name or invoice
    const std::vector<ApBank>& banks() const noexcept { return banks_; }

    bool Select(long batchno);
    const ReceiptItem* selected() const noexcept { return selected_ ? &*selected_ : nullptr; }

    // The receipt: amount (as typed), a discount for the rest, the bank (an index into banks()).
    std::string date, amount, reference;
    bool discount = false;
    int bank = 0;

    EntryResult Record();

private:
    cb4sqlite::Code4& code_;
    std::vector<ReceiptItem> items_;
    std::vector<ApBank> banks_;
    std::optional<ReceiptItem> selected_;
};
