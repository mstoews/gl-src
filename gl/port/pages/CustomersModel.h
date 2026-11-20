//
// CustomersModel.h - the Customers page's logic, without a window (spec #8
// 5.1; plan S8-M1 task 6). Portable: CustomersPage shows it, and gl_tests
// drives it through the Spec #6 sheet's criterion 1.
//
// It does what the Sales Customers dialog (ArCustomers.cpp) does, through the
// same Ar.h functions: a new customer starts with GL_SETAT's AR, SALE and
// VATP accounts (spec #6 D12); Save checks with CheckCustomer and saves with
// SaveCustomer; Delete refuses a customer in use (DeleteCustomer).
//
// What changes (spec #8 D9): the page knows whether the customer is new or
// on file, so Save doesn't ask "has already been added. Do you want to
// modify the entry?"; a new customer whose number is taken is refused
// instead. A customer's number can't be changed once saved.
//
#pragma once

#include "../Ar.h"
#include "cb4sqlite.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct CustomerRow {
    std::string number, shortName, name;
    std::int64_t owed = 0;   // the open items' remainders, in cents
};

// The fields a problem can be on (the page marks the control).
enum class CustomerInput { Number, ShortName, Name, ArAccount, SalesAccount, VatAccount, VatRate, Terms };

class CustomersModel {
public:
    explicit CustomersModel(cb4sqlite::Code4& code) : code_(code) {}

    // The customers, in number order, with what each owes.
    void Load();
    const std::vector<CustomerRow>& rows() const noexcept { return rows_; }
    // The rows matching a filter (code, then name; fx::model::LookupModel), best first.
    std::vector<size_t> Filter(const std::string& text) const;
    int RowOf(const std::string& number) const;   // -1 if none

    // A new customer, with the default accounts.
    void New();
    // The customer on file with this number; false if there is none.
    bool Select(const std::string& number);

    Customer& edit() noexcept { return current_; }
    const Customer& current() const noexcept { return current_; }
    bool isNew() const noexcept { return isNew_; }

    struct Result {
        bool ok = false;
        std::optional<CustomerInput> input;   // the field to mark
        std::string message;
    };
    // Terms and the VAT percentage as typed. On success the customer is on
    // file, the list reloaded, and the customer stays selected.
    Result Save(const std::string& termsText, const std::string& vatText);
    // After asking (the page's task dialog): deleted, or why not.
    Result Delete();

    // The selected customer's open items (Ar.h LoadArOpenItems).
    std::vector<ArOpenItem> OpenItems() const;

    // Accounts and depts for the lookups: the chart, "account" or "account-dept".
    bool AccountExists(const std::string& account, const std::string& dept) const;

private:
    cb4sqlite::Code4& code_;
    std::vector<CustomerRow> rows_;
    Customer current_;
    bool isNew_ = true;
};
