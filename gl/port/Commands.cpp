//
// Commands.cpp - see Commands.h.
//
#include "Commands.h"

#include "PortIds.h"   // the CM_* commands the menu names

#include <algorithm>
#include <cstdlib>

namespace cmd {

namespace {

// The legacy IDs the follow-on rules name (gl/header: ID_ACCTADD, ID_MODBATCH,
// ID_GLREPBATCH). Their headers need <Windows.h>, so the values are here.
constexpr Id kChartOfAccounts = 1004;    // ID_ACCTADD
constexpr Id kGlVoucher = 10076;         // ID_MODBATCH
constexpr Id kRecoverGlVoucher = 10122;  // ID_GLREPBATCH

const std::vector<MenuRow> kRows = {
#include "CommandsTable.inc"
};

} // namespace

const std::vector<MenuRow>& MenuRows() { return kRows; }

std::vector<Id> CommandIds()
{
    std::vector<Id> out;
    for (const MenuRow& r : kRows)
        if (r.kind == Kind::Item && std::find(out.begin(), out.end(), r.id) == out.end()) out.push_back(r.id);
    return out;
}

std::wstring PlainText(const wchar_t* menuText)
{
    std::wstring s;
    for (const wchar_t* p = menuText; *p && *p != L'\t'; ++p)
        if (*p != L'&') s += *p;
    while (!s.empty() && (s.back() == L'.' || s.back() == L' ')) s.pop_back();
    while (!s.empty() && s.front() == L' ') s.erase(s.begin());
    return s;
}

std::vector<std::wstring> PathOf(Id id)
{
    std::vector<std::wstring> path;   // the open popups, by depth
    for (const MenuRow& r : kRows) {
        if (path.size() > static_cast<size_t>(r.depth)) path.resize(static_cast<size_t>(r.depth));
        if (r.kind == Kind::Popup) path.push_back(PlainText(r.text));
        else if (r.kind == Kind::Item && r.id == id) return path;
    }
    return {};
}

Id RightOf(Id id)
{
    switch (id) {
    case CM_STATEMENT_LAYOUT: return kChartOfAccounts;
    case CM_AP_RECOVER:
    case CM_AR_RECOVER: return kRecoverGlVoucher;
    case CM_VOUCHER_LIST: return kGlVoucher;
    case CM_AR_CUSTOMERS_CLASSIC: return 4059;   // Sales Customers (spec #8 S8-M1)
    // The classic dialogs of the S8-M2 pages: each follows its page's right.
    case CM_DEPARTMENTS_CLASSIC: return 10061;
    case CM_MEDREPS_CLASSIC: return 6042;
    case CM_DOCTORS_CLASSIC: return 6040;
    case CM_PRODUCTS_CLASSIC: return 6005;
    case CM_VENDORS_CLASSIC: return 4045;
    case CM_ACCOUNTS_CLASSIC: return 1004;
    case CM_ASSETTYPES_CLASSIC: return 6036;
    case CM_ASSETS_CLASSIC: return 6000;
    case CM_USERS_CLASSIC: return 4078;
    case CM_BUDGET_CLASSIC: return 10074;   // spec #9 S9-M1
    case CM_PROFILE_CLASSIC: return 6015;   // spec #9 S9-M2
    // The classic dialogs of the S8-M3 voucher pages.
    case CM_GLVOUCHER_CLASSIC: return kGlVoucher;
    case CM_PRIOR_CLASSIC: return 8081;
    case CM_APINVOICE_CLASSIC: return 4044;
    case CM_APCASH_CLASSIC: return 1079;
    case CM_ARINVOICE_CLASSIC: return 5045;
    case CM_RECEIPTS_CLASSIC: return 4054;
    // The classic dialogs of the S8-M4 task pages.
    case CM_TK_POSTGL_CLASSIC: return 10121;
    case CM_TK_POSTAP_CLASSIC: return 1048;
    case CM_TK_POSTCASH_CLASSIC: return 1088;
    case CM_TK_POSTAR_CLASSIC: return 4049;
    case CM_TK_CANCELGL_CLASSIC: return 3054;
    case CM_TK_CANCELAP_CLASSIC: return 3064;
    case CM_TK_CANCELCASH_CLASSIC: return 1085;
    case CM_TK_CANCELAR_CLASSIC: return 4053;
    case CM_TK_RECOVERGL_CLASSIC: return 10122;
    case CM_TK_RECOVERAP_CLASSIC: return CM_AP_RECOVER;
    case CM_TK_RECOVERAR_CLASSIC: return CM_AR_RECOVER;
    case CM_TK_TRANSFER_CLASSIC: return 1028;
    case CM_TK_DEPRECIATION_CLASSIC: return 6003;
    case CM_TK_YEAREND_CLASSIC: return 1068;
    case CM_TK_PURGEAP_CLASSIC: return 1066;
    case CM_TK_PURGEAR_CLASSIC: return 4062;
    case CM_TK_CHEQUES_CLASSIC: return 1057;
    case CM_TK_BACKUP_CLASSIC: return 1072;
    case CM_TK_CONFIRM_CLASSIC: return 8074;   // spec #9 S9-M3
    case CM_TK_BANKREC_CLASSIC: return 6085;
    case CM_TK_RESTORE_CLASSIC: return 1074;   // spec #9 S9-M4
    case CM_REPORT_LIST_CLASSIC: return CM_REPORT_LIST;   // S8-M5
    default: return id;
    }
}

Rights Rights::Load(cb4sqlite::Code4& code)
{
    std::vector<Id> ids;
    Data4 accessm(&code, "accessm");
    if (accessm.isValid()) {
        accessm.SetIndex("menu");
        Field4 id(accessm, 4);   // ID, as ApplyMenuAccess reads it
        for (accessm.top(); !accessm.eof(); accessm.skip())
            ids.push_back(static_cast<Id>(std::atol(id.str())));
        accessm.close();
    }
    return FromIds(std::move(ids));
}

Rights Rights::FromIds(std::vector<Id> rowIds)
{
    Rights r;
    r.rows_ = rowIds.size();
    for (size_t i = 0; i < rowIds.size(); ++i) r.index_[rowIds[i]].push_back(i);
    return r;
}

bool Rights::MayRun(Id id, const std::string& menuCode) const
{
    // The walk stopped at the code's end, and a later row's EnableMenuItem
    // won: the last of the command's rows the code reaches decides.
    const auto it = index_.find(RightOf(id));
    if (it == index_.end()) return true;
    for (auto row = it->second.rbegin(); row != it->second.rend(); ++row)
        if (*row < menuCode.size()) return menuCode[*row] != '1';
    return true;
}

} // namespace cmd
