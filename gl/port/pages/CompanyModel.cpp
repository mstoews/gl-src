//
// CompanyModel.cpp - see CompanyModel.h.
//
// Translation notes (the dialogs are CompanyProfile.cpp; decisions spec #9 D3, D4):
//   - The fiscal year's days show as MM/DD/YYYY, as the page's dates do (1021
//     showed MM/DD/YY, string 4024).
//   - A bank added through the grid starts from a blank record buffer (blank()).
//     The dialog's appendStart kept whatever record was in the buffer, so a new
//     bank could take another row's SHORT code.
//   - 1033 refused the first account not in the chart with 8121 and wrote
//     nothing; so does Save, naming the tab and row. 1034 did the same for a
//     bank.
//   - Everything Save writes is in one transaction (cb4sqlite::Transaction);
//     the dialogs flushed record by record. GL.INI's withholding setting is
//     written after the commit.
//
#include "CompanyModel.h"

#include "../Accounts.h"   // AccountsTable, SeekAccount, AccountKey, TrimText
#include "../Company.h"    // UpdateUserTime
#include "../Globals.h"    // szUser
#include "../Tables.h"
#include "model/Validate.h"

#include <cctype>
#include <cstdio>
#include <set>

namespace m = fx::model;

namespace {

std::string trimmed(const Field4& f)
{
    return TrimText(f.str());
}

// A grid's rows that have any text, as they are.
std::vector<std::vector<std::string>> linesOf(const m::EntryGridModel& g)
{
    std::vector<std::vector<std::string>> v;
    for (int r = 0; r < g.Rows(); ++r) {
        if (g.fixedRows() == 0 && g.IsBlank(r)) continue;
        std::vector<std::string> cells;
        for (int c = 0; c < static_cast<int>(g.columns().size()); ++c) cells.push_back(m::Trim(g.Cell(r, c)));
        v.push_back(std::move(cells));
    }
    return v;
}

// String 8121, "Unable to locate the account number: %s  %s".
std::string notInChart(const std::string& account, const std::string& dept, const std::string& where)
{
    std::string text = "Unable to locate the account number: " + account;
    if (!dept.empty()) text += "  " + dept;
    return text + " (" + where + ").";
}

const char* const kCompanyFields[] = {"company", "address1", "address2", "address3", "postal", "phone", "fax", "contact"};

} // namespace

CompanyModel::CompanyModel(cb4sqlite::Code4& code) : RecordModel(code)
{
    const m::Check date = [](const std::string& text) -> std::optional<std::string> {
        if (m::Trim(text).empty() || !FromDisplayDate(text).empty()) return std::nullopt;
        return "A date, as MM/DD/YYYY.";
    };
    const m::Check yesOrBlank = [](const std::string& text) -> std::optional<std::string> {
        const std::string t = m::Trim(text);
        if (t.empty() || t == "Y" || t == "y") return std::nullopt;
        return "Y for a closed period, or blank for an open one.";
    };
    std::vector<m::GridColumn> periods = {{"period", "Period", m::CellKind::Number, 6, true},
                                          {"start", "Start", m::CellKind::Text, 10},
                                          {"end", "End", m::CellKind::Text, 10},
                                          {"desc", "Description", m::CellKind::Text, 10},
                                          {"closed", "Closed", m::CellKind::Upper, 6}};
    periods[1].validator.Add(date);
    periods[2].validator.Add(date);
    periods[3].validator.Add(m::MaxLength(10));   // pd_desc
    periods[4].validator.Add(yesOrBlank);
    periods_.SetColumns(std::move(periods));
    periods_.SetFixedRows(12);

    std::vector<m::GridColumn> accounts = {{"name", "Account for", m::CellKind::Text, 30, true},
                                           {"account", "Account", m::CellKind::Lookup, 7},
                                           {"dept", "Dept", m::CellKind::Upper, 4}};
    accounts[2].validator.Add(m::MaxLength(4));
    accounts_.SetColumns(std::move(accounts));
    accounts_.SetFixedRows(8);   // 1033's eight rows

    std::vector<m::GridColumn> banks = {{"account", "Account", m::CellKind::Lookup, 7},
                                        {"dept", "Dept", m::CellKind::Upper, 4},
                                        {"name", "Name", m::CellKind::Text, 30}};
    banks[1].validator.Add(m::MaxLength(4));
    banks[2].validator.Add(m::MaxLength(30));
    banks_.SetColumns(std::move(banks));
}

std::vector<std::pair<std::string, std::string>> CompanyModel::Choices(const std::string& field) const
{
    if (field == "withholding") return {{"T", "Yes"}, {"F", "No"}};
    return {};
}

m::EntryGridModel* CompanyModel::Grid(const std::string& key)
{
    if (key == "periods") return &periods_;
    if (key == "accounts") return &accounts_;
    if (key == "banks") return &banks_;
    return nullptr;
}

std::string CompanyModel::ToDisplayDate(const std::string& ymd)
{
    const std::string s = m::Trim(ymd);
    if (s.size() != 8 || s.find_first_not_of("0123456789") != std::string::npos) return {};
    return s.substr(4, 2) + "/" + s.substr(6, 2) + "/" + s.substr(0, 4);
}

std::string CompanyModel::FromDisplayDate(const std::string& text)
{
    const std::string t = m::Trim(text);
    std::string ymd;
    if (t.size() == 8 && t.find_first_not_of("0123456789") == std::string::npos) {
        ymd = t;
    } else {
        // "M/D/YYYY" or "MM/DD/YY": three numbers between two slashes.
        int parts[3] = {0, 0, 0};
        int part = 0, digits = 0;
        for (char c : t) {
            if (c >= '0' && c <= '9') {
                if (++digits > 4) return {};
                parts[part] = parts[part] * 10 + (c - '0');
            } else if (c == '/' && digits > 0 && part < 2) {
                ++part;
                digits = 0;
            } else {
                return {};
            }
        }
        if (part != 2 || digits == 0) return {};
        const int mo = parts[0], d = parts[1];
        int y = parts[2];
        if (y < 100) y += y < 50 ? 2000 : 1900;   // "04/01/94"
        char buf[16] = {};
        std::snprintf(buf, sizeof buf, "%04d%02d%02d", y, mo, d);
        ymd = buf;
    }
    return m::Date()(ymd) ? std::string() : ymd;   // a real date
}

std::vector<RecordRow> CompanyModel::LoadRows()
{
    GL_PROF p(code_);
    std::string name = "company";
    if (p.isValid() && p.top() == r4success && !p.eof()) name = trimmed(p.company);
    return {{"company", {name}}};
}

bool CompanyModel::Read(const std::string& key, Values& out)
{
    if (key != "company") return false;
    GL_PROF p(code_);
    if (!p.isValid()) return false;
    out.clear();
    if (p.top() == r4success && !p.eof()) {
        out["company"] = trimmed(p.company);
        out["address1"] = trimmed(p.address1);
        out["address2"] = trimmed(p.address2);
        out["address3"] = trimmed(p.address3);
        out["postal"] = trimmed(p.postal);
        out["phone"] = trimmed(p.telephone);
        out["fax"] = trimmed(p.fax);
        out["contact"] = trimmed(p.contact);
    }
    GL_PDS d(code_);
    if (d.isValid()) {   // the fiscal year's first and last days, as 1021 showed them
        d.SetIndex("period");
        if (d.top() == r4success && !d.eof()) out["firstDay"] = ToDisplayDate(d.periods.str());
        if (d.bottom() == r4success && !d.eof()) out["lastDay"] = ToDisplayDate(d.periode.str());
    }
    out["dataPath"] = dataPath;
    out["withholding"] = readWithholding ? readWithholding() : "T";
    loaded_ = out;
    return true;
}

void CompanyModel::LoadGrids(const std::string& key)
{
    std::vector<std::vector<std::string>> periods, accounts, banks;
    if (key == "company") {
        GL_PDS d(code_);
        if (d.isValid()) {
            d.SetIndex("period");
            for (d.top(); !d.eof() && periods.size() < 12; d.skip())
                periods.push_back({trimmed(d.period), ToDisplayDate(d.periods.str()), ToDisplayDate(d.periode.str()),
                                   trimmed(d.pd_desc), trimmed(d.status) == "1" ? "Y" : ""});
        }
        GL_SETAT s(code_);
        if (s.isValid()) {
            s.SetIndex("gl_accts");
            for (s.top(); !s.eof() && accounts.size() < 8; s.skip())
                accounts.push_back({trimmed(s.name), trimmed(s.account), trimmed(s.dept)});
            s.SetIndex("cash");
            for (s.top(); !s.eof(); s.skip()) banks.push_back({trimmed(s.account), trimmed(s.dept), trimmed(s.name)});
        }
    }
    loadedAccounts_ = accounts;   // the rows on file (a fixed grid may show more, blank)
    periods_.SetRows(periods);
    accounts_.SetRows(accounts);
    banks_.SetRows(banks);
    loadedPeriods_ = linesOf(periods_);
    loadedBanks_ = linesOf(banks_);
}

bool CompanyModel::InChart(const std::string& account, const std::string& dept) const
{
    AccountsTable t(code_);
    return t.isValid() && SeekAccount(t, account, dept);
}

RecordModel::Result CompanyModel::Write(Values& v, bool)
{
    const std::vector<std::vector<std::string>> periods = linesOf(periods_), banks = linesOf(banks_);
    std::vector<std::vector<std::string>> accounts = linesOf(accounts_);
    accounts.resize(loadedAccounts_.size());   // 1033 wrote the rows on file only

    bool companyChanged = false;
    for (const char* f : kCompanyFields) companyChanged = companyChanged || value(v, f) != value(loaded_, f);
    const bool withholdingChanged = value(v, "withholding") != value(loaded_, "withholding");
    const bool periodsChanged = periods != loadedPeriods_;
    const bool accountsChanged = accounts != loadedAccounts_;
    const bool banksChanged = banks != loadedBanks_;

    // ---- the checks, before anything is written ----
    for (size_t i = 0; i < periods.size(); ++i)
        for (int c : {1, 2})
            if (!periods[i][static_cast<size_t>(c)].empty() && FromDisplayDate(periods[i][static_cast<size_t>(c)]).empty())
                return Refuse("", "Period " + std::to_string(i + 1) + "'s " + (c == 1 ? "start" : "end") +
                                      " isn't a date (Periods tab): enter it as MM/DD/YYYY.");
    if (accountsChanged)
        for (size_t i = 0; i < accounts.size(); ++i)
            if (!InChart(accounts[i][1], accounts[i][2]))   // 1033's Ok, 8121
                return Refuse("", notInChart(accounts[i][1], accounts[i][2], "Accounts tab, " + accounts[i][0]));
    if (banksChanged) {
        std::set<std::string> keys;
        for (const auto& b : banks) {
            if (!InChart(b[0], b[1])) return Refuse("", notInChart(b[0], b[1], "Banks tab"));   // 1034's Ok, 8121
            if (!keys.insert(b[0] + "|" + b[1]).second)
                return Refuse("", "Bank account " + b[0] + (b[1].empty() ? "" : " " + b[1]) +
                                      " is listed twice (Banks tab). Keep one row for it.");
        }
    }

    cb4sqlite::Transaction tx(code_);
    if (companyChanged) {   // 1021's Ok
        GL_PROF p(code_);
        if (!p.isValid()) return Refuse("", "The company database has no GL_PROF table.");
        p.top();
        const bool adding = p.eof();   // no profile record yet
        if (adding) p.appendStart();
        p.company.assign(value(v, "company").c_str());
        p.address1.assign(value(v, "address1").c_str());
        p.address2.assign(value(v, "address2").c_str());
        p.address3.assign(value(v, "address3").c_str());
        p.postal.assign(value(v, "postal").c_str());
        p.telephone.assign(value(v, "phone").c_str());
        p.fax.assign(value(v, "fax").c_str());
        p.contact.assign(value(v, "contact").c_str());
        UpdateUserTime(p, szUser);
        if (adding) p.append();
        else p.flush();
    }
    if (periodsChanged) {   // 1028's Ok: no checks (Q1)
        GL_PDS d(code_);
        if (!d.isValid()) return Refuse("", "The company database has no GL_PDS table.");
        d.SetIndex("period");
        size_t i = 0;
        for (d.top(); !d.eof() && i < periods.size(); d.skip(), ++i) {
            d.periods.assign(FromDisplayDate(periods[i][1]).c_str());
            d.periode.assign(FromDisplayDate(periods[i][2]).c_str());
            d.pd_desc.assign(periods[i][3].c_str());
            d.status.assign(periods[i][4] == "Y" ? "1" : "0");
            d.flush();
        }
    }
    if (accountsChanged) {   // 1033's Ok
        GL_SETAT s(code_);
        s.SetIndex("gl_accts");
        size_t i = 0;
        for (s.top(); !s.eof() && i < accounts.size(); s.skip(), ++i) {
            s.account.assign(accounts[i][1].c_str());
            s.dept.assign(accounts[i][2].c_str());
            s.flush();
        }
    }
    if (banksChanged) {   // 1034's Ok, Modify and Delete
        GL_SETAT s(code_);
        s.SetIndex("cash");
        // The cash tag's seek is a prefix match ("102101" finds "102101 10"): the row found must be the bank.
        const auto seekBank = [&s](const std::vector<std::string>& b) {
            return s.seek(AccountKey(b[0], b[1], static_cast<unsigned>(s.account.len())).c_str()) == r4success &&
                   trimmed(s.account) == b[0] && trimmed(s.dept) == b[1];
        };
        std::set<std::string> kept;
        for (const auto& b : banks) kept.insert(b[0] + "|" + b[1]);
        bool removed = false;
        for (const auto& old : loadedBanks_)
            if (!kept.count(old[0] + "|" + old[1]) && seekBank(old)) {
                s.deleteRec();
                removed = true;
            }
        if (removed) s.pack();
        for (const auto& b : banks) {
            if (seekBank(b)) {
                if (trimmed(s.name) != b[2]) {
                    s.name.assign(b[2].c_str());
                    s.flush();
                }
                continue;
            }
            s.appendStart();
            s.blank();
            s.name.assign(b[2].c_str());
            s.account.assign(b[0].c_str());
            s.dept.assign(b[1].c_str());
            s.type.assign("CA");
            s.append();
        }
    }
    tx.commit();
    if (withholdingChanged && writeWithholding) writeWithholding(value(v, "withholding"));
    return {true, "", "Company profile saved."};
}

int CompanyModel::ResetDepreciationFlag()
{
    Data4 glfa(code_, "gl_fa");
    Field4 mode(glfa, "mode");
    if (!glfa.isValid() || !mode.isValid()) return 0;
    int n = 0;
    for (glfa.top(); !glfa.eof(); glfa.skip(), ++n) {   // 6025: "Reset the depreciation journal flag."
        mode.assign(" ");
        glfa.flush();
    }
    return n;
}
