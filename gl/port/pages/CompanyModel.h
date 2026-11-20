//
// CompanyModel.h - the Company Profile page's logic, without a window (spec
// #9 D3, D4; plan S9-M2). Portable: RecordPage shows it, and gl_tests drives
// each tab through the tables the classic dialogs write.
//
// Company Profile was four nested modal dialogs (CompanyProfile.cpp): 1021
// (GL_PROF, the company), 1028 (GL_PDS, the 12 periods), 1033 (the eight
// GL_SETAT TYPE="GL" accounts) and 1034 with its list 1011 (the GL_SETAT
// TYPE="CA" banks). The page is one record, "company", with four tabs:
//
//   Company    the GL_PROF fields; the fiscal year's first and last days and
//              the company database (read-only); the GL.INI [WITHHOLDING] TAX
//              setting (a Yes/No choice, through the page's callbacks)
//   Periods    a fixed 12-row grid: start, end (MM/DD/YYYY), description,
//              closed ("Y" or blank)
//   Accounts   a fixed 8-row grid in the gl_accts tag's order (SHORT): the
//              row's name (read-only), account and dept
//   Banks      a grid of the banks (cash tag order: account + dept): account,
//              dept and name; a row is added in the blank row at the end and
//              removed with Ctrl+Delete
//
// Save writes only the tabs that changed, in one transaction, each as its
// dialog wrote it:
//   - Company: 1021's Ok (append the profile if there is none; UpdateUserTime);
//   - Periods: 1028's Ok, with no checks (Q1);
//   - Accounts: 1033's Ok, refusing an account not in the chart (8121);
//   - Banks: 1034's Ok, Modify and Delete (deleteRec, pack) for the rows that
//     changed, were added or were removed; an account not in the chart is
//     refused (8121), and so is a bank listed twice (the grid can hold two
//     rows with one key; the dialog's "already added, modify?" couldn't).
// No closeAll (spec #8 D7): the tables are opened and closed here.
//
#pragma once

#include "RecordModel.h"

#include <functional>
#include <string>
#include <vector>

class CompanyModel : public RecordModel {
public:
    explicit CompanyModel(cb4sqlite::Code4& code);

    std::string Noun() const override { return "company profile"; }
    std::vector<std::pair<std::string, std::string>> Choices(const std::string& field) const override;
    fx::model::EntryGridModel* Grid(const std::string& key) override;

    // The GL.INI [WITHHOLDING] TAX value ("T" or "F"): read when the record is,
    // written on Save when it changed. The page sets them; unset, it stays "T".
    std::function<std::string()> readWithholding;
    std::function<void(const std::string&)> writeWithholding;
    // The company database, shown read-only (CurDir).
    std::string dataPath;

    // 1028's "Reset the depreciation journal flag" (6025, asked by the page):
    // gl_fa.mode blanked on every row. The number of rows.
    int ResetDepreciationFlag();

    // "19940401" <-> "04/01/1994"; FromDisplayDate takes "4/1/1994", "04/01/94" or
    // "19940401", and gives "" for blank or not a date.
    static std::string ToDisplayDate(const std::string& ymd);
    static std::string FromDisplayDate(const std::string& text);

protected:
    std::vector<RecordRow> LoadRows() override;
    bool Read(const std::string& key, Values& out) override;
    std::string KeyOf(const Values&) const override { return "company"; }
    std::string KeyField() const override { return "company"; }
    Result Write(Values& v, bool isNew) override;
    Result Remove(const std::string&) override { return Refuse("", "The company profile can't be deleted."); }
    void LoadGrids(const std::string& key) override;

private:
    bool InChart(const std::string& account, const std::string& dept) const;

    fx::model::EntryGridModel periods_, accounts_, banks_;
    Values loaded_;                                    // the Company tab as read
    std::vector<std::vector<std::string>> loadedPeriods_, loadedAccounts_, loadedBanks_;
};
