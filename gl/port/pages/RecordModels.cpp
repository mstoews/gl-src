//
// RecordModels.cpp - see RecordModels.h.
//
// Translation notes (decisions are spec #8 13):
//   - D9 (all pages): Save doesn't ask "already added, modify?"; a new
//     record with a taken key is refused (RecordModel::Save).
//   - The required fields and refusals are the dialogs': a department needs
//     its code and description (6049, 6050); a rep, doctor or product its
//     code; a vendor CheckVendor's; an account its number and description, a
//     new one a type and never a second retained earnings account (16011,
//     4071), and one with activity isn't deleted (1027); an asset type its
//     code, description and three accounts in the chart; an asset
//     CheckAsset's, and a disposed one isn't changed.
//   - Deleting a department, rep, doctor or product doesn't check whether it
//     is in use, as the dialogs didn't (spec #8 is the screens, not the rules).
//   - An asset type's account descriptions (ASSET_DESC, ...) are the chart's
//     names of the accounts: the page shows the name beside the account
//     rather than a description typed again.
//
#include "RecordModels.h"

#include "../Accounts.h"
#include "../Ap.h"
#include "../Assets.h"
#include "model/Validate.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace m = fx::model;
using Values = std::map<std::string, std::string>;

namespace {

std::string get(const Values& v, const std::string& k)
{
    const auto it = v.find(k);
    return it == v.end() ? std::string() : it->second;
}

// Cents -> "1250.50", as the cores' number fields take it.
std::string plain(std::int64_t cents)
{
    const bool negative = cents < 0;
    const std::int64_t a = negative ? -cents : cents;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s%lld.%02lld", negative ? "-" : "", static_cast<long long>(a / 100),
                  static_cast<long long>(a % 100));
    return buf;
}

std::int64_t centsOf(double value)
{
    return static_cast<std::int64_t>(value < 0 ? value * 100 - 0.5 : value * 100 + 0.5);
}

bool isAmount(const std::string& text)
{
    const std::string t = m::Trim(text);
    return t.empty() || m::ParseAmount(t).has_value();
}

// "1,234.567": digits, separators, and up to three decimals (gl_itemm's N11.3, N14.3).
bool isDecimal(const std::string& text)
{
    const std::string t = m::Trim(text);
    size_t i = 0, decimals = 0;
    bool point = false, digit = false;
    if (i < t.size() && t[i] == '-') ++i;
    for (; i < t.size(); ++i) {
        const char c = t[i];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digit = true;
            if (point && ++decimals > 3) return false;
        } else if (c == ',' && !point) {
        } else if (c == '.' && !point) {
            point = true;
        } else {
            return false;
        }
    }
    return t.empty() || digit;
}

std::string formatDate(const std::string& yyyymmdd)
{
    if (yyyymmdd.size() != 8) return yyyymmdd;
    return yyyymmdd.substr(4, 2) + "/" + yyyymmdd.substr(6, 2) + "/" + yyyymmdd.substr(0, 4);
}

std::vector<RecordRow> rowsOf(cb4sqlite::Code4& code, const std::string& sql, int cells)
{
    std::vector<RecordRow> rows;
    cb4sqlite::Query q(code, sql);
    while (q.next()) {
        RecordRow r;
        r.key = q.text(0);
        for (int i = 1; i <= cells; ++i) r.cells.push_back(q.text(i));
        rows.push_back(std::move(r));
    }
    return rows;
}

const char* const kRequired = "Required.";

// ---- Departments -----------------------------------------------------------

class DepartmentsModel : public RecordModel {
public:
    using RecordModel::RecordModel;
    std::string Noun() const override { return "department"; }

protected:
    std::vector<RecordRow> LoadRows() override
    {
        return rowsOf(code_, "SELECT trim(depart), trim(depart), trim(descript) FROM gldept WHERE _deleted = 0 ORDER BY 1",
                      2);
    }
    bool Read(const std::string& key, Values& out) override
    {
        DepartmentTable t(code_);
        if (!SeekCode(t, t.depart, key)) return false;
        out = {{"depart", TrimText(t.depart.str())}, {"descript", TrimText(t.descript.str())}};
        return true;
    }
    std::string KeyOf(const Values& v) const override { return get(v, "depart"); }
    std::string KeyField() const override { return "depart"; }
    Result Write(Values& v, bool isNew) override
    {
        if (get(v, "depart").empty()) return Refuse("depart", "Enter the department's code.");   // 6049
        if (get(v, "descript").empty()) return Refuse("descript", "Enter the department's description.");   // 6050
        DepartmentTable t(code_);
        if (!t.isValid()) return Refuse("", "The company database has no GLDEPT table.");
        auto assign = [&] {
            t.depart.assign(get(v, "depart").c_str());
            t.descript.assign(get(v, "descript").c_str());
        };
        if (isNew) {
            t.appendStart();
            assign();
            t.append();
        } else {
            if (!SeekCode(t, t.depart, key())) return Refuse("", "The department is no longer on file.");
            assign();
            t.flush();
        }
        return {true, "", ""};
    }
    Result Remove(const std::string& key) override
    {
        DepartmentTable t(code_);
        if (!DeleteRecord(t, t.depart, key)) return Refuse("", "Department " + key + " is no longer on file.");
        return {true, "", ""};
    }
};

// ---- Medical reps and doctors ------------------------------------------------

class RepsModel : public RecordModel {
public:
    RepsModel(cb4sqlite::Code4& code, RepTable::Kind kind) : RecordModel(code), kind_(kind) {}
    std::string Noun() const override { return kind_ == RepTable::Kind::MedicalRep ? "medical rep" : "doctor"; }

protected:
    std::vector<RecordRow> LoadRows() override
    {
        return kind_ == RepTable::Kind::MedicalRep
                   ? rowsOf(code_, "SELECT trim(repcode), trim(repcode), trim(rpname) FROM gl_medrp WHERE _deleted = 0 ORDER BY 1", 2)
                   : rowsOf(code_, "SELECT trim(drcode), trim(drcode), trim(rpname) FROM gl_drcod WHERE _deleted = 0 ORDER BY 1", 2);
    }
    bool Read(const std::string& key, Values& out) override
    {
        RepTable t(code_, kind_);
        if (!SeekCode(t, t.code, key)) return false;
        const RepInput r = ReadRep(t);
        out = {{"code", r.code},         {"shortName", r.shortName}, {"name", r.name},       {"address1", r.address1},
               {"address2", r.address2}, {"address3", r.address3},   {"postal", r.postal},   {"phone", r.phone},
               {"fax", r.fax},           {"contact", r.contact},     {"territory", r.territory},
               {"terms", r.terms == "0" ? std::string() : r.terms},
               {"creditLimit", record::Cents(centsOf(std::atof(r.creditLimit.c_str())))}};
        return true;
    }
    std::string KeyOf(const Values& v) const override { return get(v, "code"); }
    std::string KeyField() const override { return "code"; }
    Result Write(Values& v, bool isNew) override
    {
        if (get(v, "code").empty()) return Refuse("code", kRequired);
        if (!record::IsWholeNumber(get(v, "terms"), 0, 999)) return Refuse("terms", "A whole number of days, 0 to 999.");
        if (!isAmount(get(v, "creditLimit"))) return Refuse("creditLimit", "Not an amount.");
        RepInput in;
        in.code = get(v, "code");
        in.shortName = get(v, "shortName");
        in.name = get(v, "name");
        in.address1 = get(v, "address1");
        in.address2 = get(v, "address2");
        in.address3 = get(v, "address3");
        in.postal = get(v, "postal");
        in.phone = get(v, "phone");
        in.fax = get(v, "fax");
        in.contact = get(v, "contact");
        in.territory = get(v, "territory");
        in.terms = get(v, "terms");
        in.creditLimit = plain(record::ToCents(get(v, "creditLimit")));
        RepTable t(code_, kind_);
        if (!t.isValid()) return Refuse("", "The company database has no table for them.");
        if (isNew) {
            t.appendStart();
            AssignRep(t, in);
            t.append();
        } else {
            if (!SeekCode(t, t.code, key())) return Refuse("", "The record is no longer on file.");
            AssignRep(t, in);
            t.flush();
        }
        return {true, "", ""};
    }
    Result Remove(const std::string& key) override
    {
        RepTable t(code_, kind_);
        if (!DeleteRecord(t, t.code, key)) return Refuse("", key + " is no longer on file.");
        return {true, "", ""};
    }

private:
    RepTable::Kind kind_;
};

// ---- Products ----------------------------------------------------------------

class ProductsModel : public RecordModel {
public:
    using RecordModel::RecordModel;
    std::string Noun() const override { return "product"; }

protected:
    std::vector<RecordRow> LoadRows() override
    {
        return rowsOf(code_, "SELECT trim(fpcode), trim(fpcode), trim(ffulln), trim(fpname) FROM gl_itemm "
                             "WHERE _deleted = 0 ORDER BY 1",
                      3);   // the short name: filtered, not shown
    }
    bool Read(const std::string& key, Values& out) override
    {
        ProductTable t(code_);
        if (!SeekCode(t, t.fpcode, key)) return false;
        const ProductInput p = ReadProduct(t);
        out = {{"code", p.code}, {"name", p.name},     {"fullName", p.fullName}, {"type", p.type},
               {"unit", p.unit}, {"onHand", p.onHand}, {"avPrice", p.avPrice}};
        return true;
    }
    std::string KeyOf(const Values& v) const override { return get(v, "code"); }
    std::string KeyField() const override { return "code"; }
    Result Write(Values& v, bool isNew) override
    {
        if (get(v, "code").empty()) return Refuse("code", kRequired);
        for (const char* f : {"onHand", "avPrice"})
            if (!isDecimal(get(v, f))) return Refuse(f, "A number, with up to three decimals.");
        ProductInput in;
        in.code = get(v, "code");
        in.name = get(v, "name");
        in.fullName = get(v, "fullName");
        in.type = get(v, "type");
        in.unit = get(v, "unit");
        in.onHand = get(v, "onHand");
        in.avPrice = get(v, "avPrice");
        ProductTable t(code_);
        if (!t.isValid()) return Refuse("", "The company database has no GL_ITEMM table.");
        if (isNew) {
            t.appendStart();
            AssignProduct(t, in);
            t.append();
        } else {
            if (!SeekCode(t, t.fpcode, key())) return Refuse("", "The product is no longer on file.");
            AssignProduct(t, in);
            t.flush();
        }
        return {true, "", ""};
    }
    Result Remove(const std::string& key) override
    {
        ProductTable t(code_);
        if (!DeleteRecord(t, t.fpcode, key)) return Refuse("", "Product " + key + " is no longer on file.");
        return {true, "", ""};
    }
};

// ---- Vendors -------------------------------------------------------------------

class VendorsModel : public RecordModel {
public:
    using RecordModel::RecordModel;
    std::string Noun() const override { return "vendor"; }
    std::vector<std::pair<std::string, std::string>> Choices(const std::string& field) const override
    {
        if (field == "type") return {{"C", "Company"}, {"I", "Individual"}};
        return {};
    }

protected:
    std::vector<RecordRow> LoadRows() override
    {
        std::vector<RecordRow> rows;
        for (const Vendor& v : LoadVendors(code_)) {
            const std::string n = TrimText(v.number);
            rows.push_back({n, {n, TrimText(v.name), TrimText(v.shortName)}});   // the short name: filtered, not shown
        }
        return rows;
    }
    Values Defaults() override
    {
        base_ = Vendor{};
        const ApAccounts d = LoadApAccounts(code_);   // the default accounts (spec #5 D12)
        return {{"ap", d.ap.account}, {"ap_dept", d.ap.dept}, {"vat", d.vatInput.account}, {"vat_dept", d.vatInput.dept}};
    }
    bool Read(const std::string& key, Values& out) override
    {
        const std::optional<Vendor> found = FindVendor(code_, key);
        if (!found) return false;
        const Vendor& v = *found;
        auto num = [](int n) { return n ? std::to_string(n) : std::string(); };
        out = {{"number", TrimText(v.number)},   {"shortName", TrimText(v.shortName)}, {"name", TrimText(v.name)},
               {"type", TrimText(v.type)},       {"vin", TrimText(v.vin)},             {"medrep", TrimText(v.medrep)},
               {"address1", TrimText(v.address1)}, {"address2", TrimText(v.address2)}, {"address3", TrimText(v.address3)},
               {"postal", TrimText(v.postal)},   {"phone", TrimText(v.phone)},         {"fax", TrimText(v.fax)},
               {"contact", TrimText(v.contact)}, {"terms", num(v.terms)},              {"vatRate", num(v.vatRate)},
               {"whRate", num(v.whRate)},        {"discountDays", num(v.discountDays)},
               {"discountPercent", num(v.discountPercent)},
               {"ap", TrimText(v.ap.account)},   {"ap_dept", TrimText(v.ap.dept)},
               {"vat", TrimText(v.vat.account)}, {"vat_dept", TrimText(v.vat.dept)}};
        base_ = v;
        return true;
    }
    std::string KeyOf(const Values& v) const override { return get(v, "number"); }
    std::string KeyField() const override { return "number"; }
    Result Write(Values& in, bool isNew) override
    {
        static const std::pair<const char*, int> ranges[] = {
            {"terms", 999}, {"vatRate", 99}, {"whRate", 99}, {"discountDays", 99}, {"discountPercent", 99}};
        for (const auto& [f, hi] : ranges)
            if (!record::IsWholeNumber(get(in, f), 0, hi))
                return Refuse(f, hi == 999 ? "A whole number of days, 0 to 999." : "A whole number from 0 to 99.");
        Vendor v = isNew ? Vendor{} : base_;
        v.number = get(in, "number");
        v.shortName = get(in, "shortName");
        v.name = get(in, "name");
        v.type = get(in, "type");
        v.vin = get(in, "vin");
        v.medrep = get(in, "medrep");
        v.address1 = get(in, "address1");
        v.address2 = get(in, "address2");
        v.address3 = get(in, "address3");
        v.postal = get(in, "postal");
        v.phone = get(in, "phone");
        v.fax = get(in, "fax");
        v.contact = get(in, "contact");
        v.terms = record::ToInt(get(in, "terms"));
        v.vatRate = record::ToInt(get(in, "vatRate"));
        v.whRate = record::ToInt(get(in, "whRate"));
        v.discountDays = record::ToInt(get(in, "discountDays"));
        v.discountPercent = record::ToInt(get(in, "discountPercent"));
        v.ap = {get(in, "ap"), get(in, "ap_dept")};
        v.vat = {get(in, "vat"), get(in, "vat_dept")};
        if (const std::optional<VendorProblem> p = CheckVendor(code_, v)) {
            static const std::pair<VendorField, const char*> keys[] = {
                {VendorField::Number, "number"}, {VendorField::ShortName, "shortName"}, {VendorField::Name, "name"},
                {VendorField::Type, "type"},     {VendorField::VatRate, "vatRate"},     {VendorField::WhRate, "whRate"},
                {VendorField::ApAccount, "ap"},  {VendorField::VatAccount, "vat"}};
            std::string field = "number";
            for (const auto& [f, k] : keys)
                if (f == p->field) field = k;
            switch (p->kind) {
            case VendorProblem::Kind::Required: return Refuse(field, kRequired);
            case VendorProblem::Kind::NoType:
                return Refuse(field, "Choose Company or Individual: the withholding tax account depends on it.");
            case VendorProblem::Kind::BadRate: return Refuse(field, "A whole percentage from 0 to 99.");
            case VendorProblem::Kind::BadAccount: return Refuse(field, "Not an account in the chart.");
            case VendorProblem::Kind::VatAccountNeeded:
                return Refuse(field, "Needed when the VAT percentage isn't 0: invoices book the VAT to it.");
            }
        }
        SaveVendor(code_, v);
        return {true, "", ""};
    }
    Result Remove(const std::string& key) override
    {
        std::string usedBy;
        switch (DeleteVendor(code_, key, &usedBy)) {
        case DeleteVendorResult::Deleted: return {true, "", ""};
        case DeleteVendorResult::InUse:
            return Refuse("", "Vendor " + key + " can't be deleted: " + usedBy +
                                  " is theirs. A vendor stays while their vouchers, open items or cheques are on file.");
        case DeleteVendorResult::NotFound: break;
        }
        return Refuse("", "Vendor " + key + " is no longer on file.");
    }

private:
    Vendor base_;   // the record as read: the fields the page doesn't show stay
};

// ---- Chart of Accounts ---------------------------------------------------------

std::string accountKey(const std::string& account, const std::string& dept)
{
    return dept.empty() ? account : account + "-" + dept;
}

std::pair<std::string, std::string> splitKey(const std::string& key)
{
    const size_t dash = key.find('-');
    return dash == std::string::npos ? std::make_pair(key, std::string())
                                     : std::make_pair(key.substr(0, dash), key.substr(dash + 1));
}

class AccountsModel : public RecordModel {
public:
    using RecordModel::RecordModel;
    std::string Noun() const override { return "account"; }
    std::vector<std::pair<std::string, std::string>> Choices(const std::string& field) const override
    {
        if (field == "type")
            return {{"B", "Balance sheet"}, {"I", "Income statement"}, {"R", "Retained earnings"}, {"N", "Not set"}};
        return {};
    }

protected:
    std::vector<RecordRow> LoadRows() override
    {
        std::vector<RecordRow> rows;
        cb4sqlite::Query q(code_, "SELECT trim(account), coalesce(trim(dept), ''), trim(descript) FROM glaccts "
                                  "WHERE _deleted = 0 ORDER BY 1, 2");
        while (q.next()) rows.push_back({accountKey(q.text(0), q.text(1)), {q.text(0), q.text(1), q.text(2)}});
        return rows;
    }
    Values Defaults() override { return {{"type", "N"}}; }
    bool Read(const std::string& key, Values& out) override
    {
        const auto [account, dept] = splitKey(key);
        AccountsTable t(code_);
        if (!t.isValid() || !SeekAccount(t, account, dept)) return false;
        const std::string type = TrimText(t.type->str());
        out = {{"account", TrimText(t.accounts->str())},
               {"dept", TrimText(t.dept->str())},
               {"descript", TrimText(t.descript->str())},
               {"fdescript", TrimText(t.fdescript->str())},
               {"type", type.empty() ? "N" : type},
               {"currBal", record::Cents(centsOf(t.curr_bal->num()))},
               {"openBal", record::Cents(centsOf(t.previous12->num()))}};   // as the dialog showed it
        return true;
    }
    std::string KeyOf(const Values& v) const override { return accountKey(get(v, "account"), get(v, "dept")); }
    std::string KeyField() const override { return "account"; }
    Result Write(Values& v, bool isNew) override
    {
        if (get(v, "account").empty()) return Refuse("account", kRequired);    // 4072, "GL Account"
        if (get(v, "descript").empty()) return Refuse("descript", kRequired);  // 4072, "Description"
        for (const char* f : {"currBal", "openBal"})
            if (!isAmount(get(v, f))) return Refuse(f, "Not an amount.");
        AccountInput in;
        in.account = get(v, "account");
        in.dept = get(v, "dept");
        in.descript = get(v, "descript");
        in.fdescript = get(v, "fdescript");
        in.type = get(v, "type").empty() ? "N" : get(v, "type");
        in.currBal = plain(record::ToCents(get(v, "currBal")));
        in.openBal = plain(record::ToCents(get(v, "openBal")));
        AccountsTable t(code_);
        if (!t.isValid()) return Refuse("", "The company database has no GLACCTS table.");
        if (isNew) {
            if (in.type == "R")   // 16011: the chart has one retained earnings account
                return Refuse("type", "There can only be one retained earnings account.");
            if (in.type == "N") return Refuse("type", "Choose the account's type.");   // 4071
            AppendAccount(t, in);
        } else {
            const auto [account, dept] = splitKey(key());
            if (!SeekAccount(t, account, dept)) return Refuse("", "The account is no longer on file.");
            ModifyAccount(t, in);
        }
        return {true, "", ""};
    }
    Result Remove(const std::string& key) override
    {
        const auto [account, dept] = splitKey(key);
        AccountsTable t(code_);
        if (!t.isValid() || !SeekAccount(t, account, dept)) return Refuse("", "Account " + key + " is no longer on file.");
        if (HasActivity(t))   // 1027
            return Refuse("", "Account " + key + " has balances in a period or the previous year. "
                                                 "An account must be empty to be deleted.");
        DeleteAccount(t);
        return {true, "", ""};
    }
};

// ---- Asset types -------------------------------------------------------------

std::string chartName(cb4sqlite::Code4& code, const std::string& account, const std::string& dept)
{
    AccountsTable t(code);
    return t.isValid() && SeekAccount(t, account, dept) ? TrimText(t.descript->str()) : std::string();
}

class AssetTypesModel : public RecordModel {
public:
    using RecordModel::RecordModel;
    std::string Noun() const override { return "asset type"; }

protected:
    std::vector<RecordRow> LoadRows() override
    {
        std::vector<RecordRow> rows;
        for (const AssetType& t : LoadAssetTypes(code_)) rows.push_back({t.code, {t.code, t.description}});
        return rows;
    }
    bool Read(const std::string& key, Values& out) override
    {
        const std::optional<AssetType> t = FindAssetType(code_, key);
        if (!t) return false;
        out = {{"code", t->code},           {"description", t->description}, {"asset", t->asset.account},
               {"asset_dept", t->asset.dept}, {"expense", t->expense.account}, {"expense_dept", t->expense.dept},
               {"accum", t->accum.account}, {"accum_dept", t->accum.dept}};
        return true;
    }
    std::string KeyOf(const Values& v) const override { return get(v, "code"); }
    std::string KeyField() const override { return "code"; }
    Result Write(Values& v, bool) override
    {
        if (get(v, "code").empty()) return Refuse("code", kRequired);             // 8101, "Fixed Asset Type"
        if (get(v, "description").empty()) return Refuse("description", kRequired);
        AssetType t;
        t.code = get(v, "code");
        t.description = get(v, "description");
        const std::pair<const char*, AssetAccount*> accounts[] = {{"asset", &t.asset}, {"expense", &t.expense},
                                                                  {"accum", &t.accum}};
        for (const auto& [f, a] : accounts) {
            a->account = get(v, f);
            a->dept = get(v, std::string(f) + "_dept");
            if (!ValidateAccount(code_, a->account, a->dept)) return Refuse(f, "Not an account in the chart.");
            a->descript = chartName(code_, a->account, a->dept);
        }
        SaveAssetType(code_, t);
        return {true, "", ""};
    }
    Result Remove(const std::string& key) override
    {
        std::string usedBy;
        switch (DeleteAssetType(code_, key, &usedBy)) {
        case DeleteTypeResult::Deleted: return {true, "", ""};
        case DeleteTypeResult::InUse:
            return Refuse("", "Asset type " + key + " can't be deleted: asset " + usedBy +
                                  " uses it. Give the assets another type first.");
        case DeleteTypeResult::NotFound: break;
        }
        return Refuse("", "Asset type " + key + " is no longer on file.");
    }
};

// ---- Fixed assets ------------------------------------------------------------

class AssetsModel : public RecordModel {
public:
    explicit AssetsModel(cb4sqlite::Code4& code) : RecordModel(code) { EnsureAssetTables(code); }
    std::string Noun() const override { return "asset"; }
    std::vector<std::pair<std::string, std::string>> Choices(const std::string& field) const override
    {
        if (field == "method") return {{"S", "Straight line"}, {"D", "Declining balance"}};
        if (field == "type") {
            std::vector<std::pair<std::string, std::string>> items;
            for (const AssetType& t : LoadAssetTypes(code_)) items.push_back({t.code, t.code + "  " + t.description});
            return items;
        }
        return {};
    }
    std::string Get(const std::string& field) const override
    {
        if (field == "monthly" || field == "bookValue") {
            const Asset a = typed(values_);
            if (field == "bookValue") return m::FormatAmount(BookValue(a));
            const std::int64_t start = history_ && !yearEnd_.empty() ? StartOfYearBookValue(code_, a, yearEnd_)
                                                                     : a.cost - a.accumulated;
            return m::FormatAmount(MonthlyAmount(a, start));
        }
        if (field == "dateSold") return formatDate(RecordModel::Get(field));
        return RecordModel::Get(field);
    }

protected:
    std::vector<RecordRow> LoadRows() override
    {
        yearEnd_ = FiscalYearEnd(code_);
        std::vector<RecordRow> rows;
        for (const Asset& a : LoadAssets(code_))
            rows.push_back({a.code, {a.code, a.description + (a.disposed() ? " (disposed)" : "")}});
        return rows;
    }
    Values Defaults() override
    {
        history_ = false;
        return {{"method", "S"}};
    }
    bool Read(const std::string& key, Values& out) override
    {
        const std::optional<Asset> found = FindAsset(code_, key);
        if (!found) return false;
        const Asset& a = *found;
        auto num = [](int n) { return n ? std::to_string(n) : std::string(); };
        out = {{"code", a.code},
               {"type", a.type},
               {"description", a.description},
               {"reference", a.reference},
               {"datePurchased", a.datePurchased},
               {"dateSold", a.dateSold},
               {"method", a.method == DepMethod::Declining ? "D" : "S"},
               {"cost", record::Cents(a.cost)},
               {"salvage", record::Cents(a.salvage)},
               {"life", num(a.life)},
               {"percent", num(a.percent)},
               {"monthsUsed", num(a.monthsUsed)},
               {"accumulated", record::Cents(a.accumulated)}};
        history_ = HasDepreciation(code_, key);
        return true;
    }
    std::string KeyOf(const Values& v) const override { return get(v, "code"); }
    std::string KeyField() const override { return "code"; }
    Result Write(Values& v, bool isNew) override
    {
        for (const char* f : {"cost", "salvage", "accumulated"})
            if (!isAmount(get(v, f))) return Refuse(f, "Not an amount.");
        for (const char* f : {"life", "monthsUsed"})
            if (!record::IsWholeNumber(get(v, f), 0, 9999)) return Refuse(f, "A whole number of months.");
        if (!record::IsWholeNumber(get(v, "percent"), 0, 99)) return Refuse("percent", "A whole percentage from 0 to 99.");
        Asset a = typed(v);
        const std::optional<Asset> existing = isNew ? std::nullopt : FindAsset(code_, key());
        if (existing && existing->disposed())
            return Refuse("", "Asset " + existing->code + " was disposed of on " + formatDate(existing->dateSold) +
                                  ". It can't be changed.");
        if (const std::optional<AssetProblem> p = CheckAsset(code_, a)) {
            static const std::pair<AssetField, const char*> keys[] = {
                {AssetField::Code, "code"},       {AssetField::Type, "type"},       {AssetField::Description, "description"},
                {AssetField::Date, "datePurchased"}, {AssetField::Cost, "cost"},    {AssetField::Salvage, "salvage"},
                {AssetField::Life, "life"},       {AssetField::MonthsUsed, "monthsUsed"}, {AssetField::Rate, "percent"},
                {AssetField::Accumulated, "accumulated"}};
            std::string field = "code";
            for (const auto& [f, k] : keys)
                if (f == p->field) field = k;
            switch (p->kind) {
            case AssetProblem::Kind::Required: return Refuse(field, kRequired);
            case AssetProblem::Kind::NoSuchType: return Refuse(field, "There is no fixed asset type " + a.type + ".");
            case AssetProblem::Kind::SalvageOverCost: return Refuse(field, "The salvage value can't be more than the cost.");
            case AssetProblem::Kind::UsedOverLife: return Refuse(field, "The months used can't be more than the life.");
            case AssetProblem::Kind::AccumOverDepreciable:
                return Refuse(field, "The accumulated depreciation can't be more than the cost less the salvage value.");
            }
        }
        const bool history = existing && HasDepreciation(code_, a.code);
        if (existing) {
            a.mode = existing->mode;
            a.dateSold = existing->dateSold;
            if (history) {   // spec #4 D6: the run's figures stand
                a.accumulated = existing->accumulated;
                a.monthsUsed = existing->monthsUsed;
            }
        }
        a.monthly = MonthlyAmount(a, history && !yearEnd_.empty() ? StartOfYearBookValue(code_, a, yearEnd_)
                                                                  : a.cost - a.accumulated);
        SaveAsset(code_, a);
        return {true, "", history ? "Asset " + a.code + " saved. Its depreciation runs' figures stand." : ""};
    }
    Result Remove(const std::string& key) override
    {
        switch (DeleteAsset(code_, key)) {
        case DeleteAssetResult::Deleted: return {true, "", ""};
        case DeleteAssetResult::HasHistory:
            return Refuse("", "Asset " + key + " has been depreciated, so it stays on file. Dispose of it instead.");
        case DeleteAssetResult::NotFound: break;
        }
        return Refuse("", "Asset " + key + " is no longer on file.");
    }

private:
    Asset typed(const Values& v) const
    {
        Asset a;
        a.code = get(v, "code");
        a.type = get(v, "type");
        a.description = get(v, "description");
        a.reference = get(v, "reference");
        a.datePurchased = get(v, "datePurchased");
        a.dateSold = get(v, "dateSold");
        a.method = get(v, "method") == "D" ? DepMethod::Declining : DepMethod::Straight;
        a.cost = record::ToCents(get(v, "cost"));
        a.salvage = record::ToCents(get(v, "salvage"));
        a.accumulated = record::ToCents(get(v, "accumulated"));
        a.life = record::ToInt(get(v, "life"));
        a.monthsUsed = record::ToInt(get(v, "monthsUsed"));
        a.percent = record::ToInt(get(v, "percent"));
        return a;
    }

    std::string yearEnd_;
    bool history_ = false;   // the asset shown has been depreciated by a run
};

} // namespace

std::unique_ptr<RecordModel> MakeDepartmentsModel(cb4sqlite::Code4& code)
{
    return std::make_unique<DepartmentsModel>(code);
}
std::unique_ptr<RecordModel> MakeRepsModel(cb4sqlite::Code4& code, RepTable::Kind kind)
{
    return std::make_unique<RepsModel>(code, kind);
}
std::unique_ptr<RecordModel> MakeProductsModel(cb4sqlite::Code4& code)
{
    return std::make_unique<ProductsModel>(code);
}
std::unique_ptr<RecordModel> MakeVendorsModel(cb4sqlite::Code4& code)
{
    return std::make_unique<VendorsModel>(code);
}
std::unique_ptr<RecordModel> MakeAccountsModel(cb4sqlite::Code4& code)
{
    return std::make_unique<AccountsModel>(code);
}
std::unique_ptr<RecordModel> MakeAssetTypesModel(cb4sqlite::Code4& code)
{
    return std::make_unique<AssetTypesModel>(code);
}
std::unique_ptr<RecordModel> MakeAssetsModel(cb4sqlite::Code4& code)
{
    return std::make_unique<AssetsModel>(code);
}

// ---- User Access ---------------------------------------------------------------

void AccessModel::Load()
{
    rows_.clear();
    Data4 accessm(&code_, "accessm");
    if (!accessm.isValid()) return;
    accessm.SetIndex("menu");
    Field4 popup(accessm, "popup"), text(accessm, "text"), id(accessm, "id");
    for (accessm.top(); !accessm.eof(); accessm.skip())
        rows_.push_back({TrimText(popup.str()), TrimText(text.str()), std::atol(id.str())});
    accessm.close();
}

bool AccessModel::HasLevel(const std::string& level) const
{
    Data4 accessl(&code_, "accessl");
    if (!accessl.isValid()) return false;
    accessl.SetIndex("l");
    Field4 lv(accessl, "level");
    const bool found = SeekCode(accessl, lv, level);
    accessl.close();
    return found;
}

std::string AccessModel::CodeOf(const std::string& level) const
{
    Data4 accessl(&code_, "accessl");
    if (!accessl.isValid()) return {};
    accessl.SetIndex("l");
    Field4 lv(accessl, "level"), menucode(accessl, "menucode");
    if (!SeekCode(accessl, lv, level)) accessl.top();   // LevelMenuCode: the first level's
    std::string code = accessl.eof() ? std::string() : std::string(menucode.str());
    accessl.close();
    return code;
}

void AccessModel::SaveLevel(const std::string& level, const std::string& menuCode)
{
    Data4 accessl(&code_, "accessl");
    if (!accessl.isValid()) return;
    accessl.SetIndex("l");
    Field4 lv(accessl, "level"), menucode(accessl, "menucode");
    if (SeekCode(accessl, lv, level)) {
        menucode.assign(menuCode.c_str());
        accessl.flush();
    } else {
        accessl.appendStart();
        lv.assign(level.c_str());
        menucode.assign(menuCode.c_str());
        accessl.append();
    }
    accessl.close();
}

bool AccessModel::Allowed(const std::string& code, size_t row)
{
    return row >= code.size() || code[row] != '1';
}

std::string AccessModel::WithAllowed(std::string code, size_t row, bool allowed)
{
    if (Allowed(code, row) == allowed) return code;   // unchanged: not a byte moves
    if (code.size() <= row) code.resize(row + 1, '0');
    code[row] = allowed ? '0' : '1';
    return code;
}

UsersModel::UsersModel(cb4sqlite::Code4& code) : RecordModel(code), access_(code)
{
    access_.Load();
}

void UsersModel::ShowLevel(const std::string& level)
{
    level_ = m::Trim(level);
    levelCode_ = access_.CodeOf(level_);
    levelChanged_ = false;
}

void UsersModel::SetAllowed(size_t row, bool allowed)
{
    const std::string code = AccessModel::WithAllowed(levelCode_, row, allowed);
    if (code != levelCode_) {
        levelCode_ = code;
        levelChanged_ = true;
    }
}

void UsersModel::Set(const std::string& field, const std::string& value)
{
    RecordModel::Set(field, value);
    if (field == "level" && m::Trim(value) != level_ && !levelChanged_) ShowLevel(value);
}

std::vector<RecordRow> UsersModel::LoadRows()
{
    return rowsOf(code_, "SELECT trim(username), trim(username), trim(descript) FROM \"access.psw\" "
                         "WHERE _deleted = 0 AND trim(username) <> '' ORDER BY 1",
                  2);
}

bool UsersModel::Read(const std::string& key, Values& out)
{
    Data4 t(&code_, "access.psw");
    if (!t.isValid()) return false;
    t.SetIndex("u");
    Field4 user(t, "username"), descript(t, "descript"), date(t, "date"), password(t, "password"), level(t, "level");
    const bool found = SeekCode(t, user, key);
    if (found)
        out = {{"username", TrimText(user.str())},
               {"descript", TrimText(descript.str())},
               {"date", TrimText(date.str())},
               {"password", TrimText(password.str())},
               {"level", TrimText(level.str())}};
    t.close();
    return found;
}

RecordModel::Result UsersModel::Write(Values& v, bool isNew)
{
    if (get(v, "username").empty()) return Refuse("username", "A user ID must not be blank.");
    if (get(v, "level").empty()) return Refuse("level", kRequired);
    Data4 t(&code_, "access.psw");
    if (!t.isValid()) return Refuse("", "The company database has no ACCESS.PSW table.");
    t.SetIndex("u");
    Field4 user(t, "username"), descript(t, "descript"), date(t, "date"), password(t, "password"), level(t, "level");
    auto assign = [&] {
        user.assign(get(v, "username").c_str());
        descript.assign(get(v, "descript").c_str());
        date.assign(get(v, "date").c_str());
        password.assign(get(v, "password").c_str());
        level.assign(get(v, "level").c_str());
    };
    if (isNew) {
        t.appendStart();
        assign();
        t.append();
    } else {
        if (!SeekCode(t, user, key())) {
            t.close();
            return Refuse("", "The user is no longer on file.");
        }
        assign();
        t.flush();
    }
    t.close();
    // The level's commands, if they were changed for the level the user has.
    std::string message;
    if (levelChanged_ && level_ == get(v, "level")) {
        access_.SaveLevel(level_, levelCode_);
        levelChanged_ = false;
        message = "User " + get(v, "username") + " saved, and level " + level_ + "'s commands.";
    }
    return {true, "", message};
}

RecordModel::Result UsersModel::Remove(const std::string& key)
{
    Data4 t(&code_, "access.psw");
    if (!t.isValid()) return Refuse("", "The company database has no ACCESS.PSW table.");
    t.SetIndex("u");
    Field4 user(t, "username");
    if (!SeekCode(t, user, key)) {
        t.close();
        return Refuse("", "User " + key + " is no longer on file.");
    }
    t.deleteRec();
    t.pack();
    t.close();
    return {true, "", ""};
}
