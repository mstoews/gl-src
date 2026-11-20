//
// CustomersModel.cpp - see CustomersModel.h.
//
#include "CustomersModel.h"

#include "../Accounts.h"   // TrimText
#include "model/LookupModel.h"
#include "model/Validate.h"

#include <cstdlib>
#include <map>

namespace {

CustomerInput inputOf(CustomerField f)
{
    switch (f) {
    case CustomerField::Number: return CustomerInput::Number;
    case CustomerField::ShortName: return CustomerInput::ShortName;
    case CustomerField::Name: return CustomerInput::Name;
    case CustomerField::VatRate: return CustomerInput::VatRate;
    case CustomerField::ArAccount: return CustomerInput::ArAccount;
    case CustomerField::SalesAccount: return CustomerInput::SalesAccount;
    case CustomerField::VatAccount: return CustomerInput::VatAccount;
    default: return CustomerInput::Name;
    }
}

std::string messageOf(const CustomerProblem& p)
{
    switch (p.kind) {
    case CustomerProblem::Kind::Required: return "Required.";
    case CustomerProblem::Kind::BadRate: return "A whole number from 0 to 99.";
    case CustomerProblem::Kind::BadAccount: return "Not an account in the chart.";
    case CustomerProblem::Kind::VatAccountNeeded:
        return "Needed when the VAT percentage isn't 0: sales invoices book the VAT to it.";
    }
    return {};
}

} // namespace

void CustomersModel::Load()
{
    EnsureArTables(code_);
    std::map<std::string, std::int64_t> owed;
    for (const ArOpenItem& i : LoadArOpenItems(code_)) owed[TrimText(i.customer)] += i.remainder;
    rows_.clear();
    for (const Customer& c : LoadCustomers(code_)) {
        const std::string n = TrimText(c.number);
        rows_.push_back({n, TrimText(c.shortName), TrimText(c.name), owed[n]});
    }
}

std::vector<size_t> CustomersModel::Filter(const std::string& text) const
{
    std::vector<fx::model::LookupItem> items;
    items.reserve(rows_.size());
    for (const CustomerRow& r : rows_) items.push_back({r.number, "", r.name + " " + r.shortName});
    return fx::model::LookupModel(std::move(items)).Match(text);
}

int CustomersModel::RowOf(const std::string& number) const
{
    const std::string n = TrimText(number);
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].number == n) return static_cast<int>(i);
    return -1;
}

void CustomersModel::New()
{
    const ArAccounts defaults = LoadArAccounts(code_);
    current_ = Customer{};
    current_.ar = defaults.ar;
    current_.sales = defaults.sales;
    current_.vat = defaults.vatPayable;
    isNew_ = true;
}

bool CustomersModel::Select(const std::string& number)
{
    const std::optional<Customer> found = FindCustomer(code_, TrimText(number));
    if (!found) return false;
    current_ = *found;
    isNew_ = false;
    return true;
}

CustomersModel::Result CustomersModel::Save(const std::string& termsText, const std::string& vatText)
{
    Customer c = current_;
    c.number = TrimText(c.number);
    // The numbers as typed: whole numbers in their fields' range (N3, VAT N3 but 0-99).
    if (auto why = fx::model::WholeNumber(0, 999)(termsText)) return {false, CustomerInput::Terms, *why};
    if (auto why = fx::model::WholeNumber(0, 99)(vatText)) return {false, CustomerInput::VatRate, *why};
    c.terms = static_cast<int>(std::atol(fx::model::Trim(termsText).c_str()));
    c.vatRate = static_cast<int>(std::atol(fx::model::Trim(vatText).c_str()));
    if (const std::optional<CustomerProblem> p = CheckCustomer(code_, c)) return {false, inputOf(p->field), messageOf(*p)};
    if (isNew_ && FindCustomer(code_, c.number))
        return {false, CustomerInput::Number, "Customer " + c.number + " is already on file: pick it in the list to change it."};
    SaveCustomer(code_, c);
    current_ = c;
    isNew_ = false;
    Load();
    return {true, std::nullopt, "Customer " + c.number + " saved."};
}

CustomersModel::Result CustomersModel::Delete()
{
    if (isNew_) return {false, std::nullopt, "This customer isn't on file yet: there is nothing to delete."};
    std::string usedBy;
    switch (DeleteCustomer(code_, TrimText(current_.number), &usedBy)) {
    case DeleteCustomerResult::Deleted: {
        const std::string n = TrimText(current_.number);
        Load();
        New();
        return {true, std::nullopt, "Customer " + n + " deleted."};
    }
    case DeleteCustomerResult::InUse:
        return {false, std::nullopt, "Customer " + TrimText(current_.number) + " can't be deleted: " + usedBy + " is theirs."};
    case DeleteCustomerResult::NotFound: break;
    }
    Load();
    return {false, std::nullopt, "Customer " + TrimText(current_.number) + " is no longer on file."};
}

std::vector<ArOpenItem> CustomersModel::OpenItems() const
{
    std::vector<ArOpenItem> out;
    if (isNew_) return out;
    const std::string n = TrimText(current_.number);
    for (const ArOpenItem& i : LoadArOpenItems(code_))
        if (TrimText(i.customer) == n) out.push_back(i);
    return out;
}

bool CustomersModel::AccountExists(const std::string& account, const std::string& dept) const
{
    cb4sqlite::Query q(code_, "SELECT count(*) FROM glaccts WHERE _deleted = 0 AND trim(account) = ?1 AND "
                              "coalesce(trim(dept), '') = ?2");
    q.bindText(1, TrimText(account)).bindText(2, TrimText(dept));
    return q.next() && q.integer(0) > 0;
}
