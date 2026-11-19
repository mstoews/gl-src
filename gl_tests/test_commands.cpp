//
// gl_tests: the command table and the rights (spec #8 8, D6; plan S8-M0).
// The rights must grey exactly what ApplyMenuAccess (Login.cpp) and
// ManilaGL's WM_INITMENUPOPUP greyed, for every command and every level.
//
#include "GlTestDb.h"

#include "Commands.h"
#include "PortIds.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace gltest;
using cmd::Id;

namespace {

// The legacy walk, replayed: accessm in "menu" order beside the code, each
// row setting its command enabled ('1': greyed), stopping at the code's end
// or the table's; then WM_INITMENUPOPUP's follow-on rules, which copy
// another item's state after the walk. Commands it never touched stay enabled.
std::map<Id, bool> legacyEnabled(const std::vector<Id>& rows, const std::string& code)
{
    std::map<Id, bool> on;
    for (Id id : cmd::CommandIds()) on[id] = true;
    for (size_t i = 0; i < code.size() && i < rows.size(); ++i) on[rows[i]] = code[i] != '1';
    on[CM_STATEMENT_LAYOUT] = on[1004];        // ID_ACCTADD
    on[CM_AP_RECOVER] = on[10122];             // ID_GLREPBATCH
    on[CM_AR_RECOVER] = on[10122];
    on[CM_VOUCHER_LIST] = on[10076];           // ID_MODBATCH
    return on;
}

std::vector<Id> accessmRows(cb4sqlite::Code4& code)
{
    std::vector<Id> rows;
    Data4 m(&code, "accessm");
    REQUIRE(m.isValid());
    m.SetIndex("menu");
    Field4 id(m, 4);
    for (m.top(); !m.eof(); m.skip()) rows.push_back(static_cast<Id>(std::atol(id.str())));
    m.close();
    return rows;
}

} // namespace

TEST_CASE("The command table is the MANILA menu (S8-M0)", "[commands]")
{
    const auto& rows = cmd::MenuRows();
    REQUIRE_FALSE(rows.empty());
    std::vector<std::wstring> top;
    for (const cmd::MenuRow& r : rows)
        if (r.depth == 0) {
            CHECK(r.kind == cmd::Kind::Popup);
            top.push_back(cmd::PlainText(r.text));
        }
    CHECK(top == std::vector<std::wstring>{L"File", L"Accounts", L"Fixed Assets", L"Transactions", L"Receipts/Payments",
                                           L"Reporting", L"Maintenance", L"Window", L"Help"});
    // 116 menu items, as accessm has rows; 114 commands, two of them on the menu twice.
    CHECK(std::count_if(rows.begin(), rows.end(), [](const cmd::MenuRow& r) { return r.kind == cmd::Kind::Item; }) == 116);
    CHECK(cmd::CommandIds().size() == 114);
    CHECK(cmd::PathOf(1004) == std::vector<std::wstring>{L"Accounts"});          // Chart of Accounts
    CHECK(cmd::PathOf(10121) == std::vector<std::wstring>{L"Transactions", L"Post Voucher"});
    CHECK(cmd::PathOf(CM_AR_STATEMENTS).size() == 2);
    CHECK(cmd::PathOf(9999).empty());
    CHECK(cmd::PlainText(L"&Chart of Accounts...") == L"Chart of Accounts");
    CHECK(cmd::PlainText(L"E&xit\tAlt+F4") == L"Exit");
    // The depths nest: after a popup at depth d the next row is at d + 1 at
    // most, after anything else at its own depth at most.
    int open = 0;
    for (const cmd::MenuRow& r : rows) {
        CHECK(r.depth <= open);
        open = r.kind == cmd::Kind::Popup ? r.depth + 1 : r.depth;
    }
}

TEST_CASE("Rights by command grey what ApplyMenuAccess greyed (S8-M0, D6)", "[commands]")
{
    FixtureDb db;
    const std::vector<Id> rows = accessmRows(db.code);
    const cmd::Rights rights = cmd::Rights::Load(db.code);
    CHECK(rights.rows() == rows.size());

    std::vector<std::string> codes;
    {
        cb4sqlite::Query q(db.code, "SELECT trim(menucode) FROM accessl WHERE _deleted = 0 ORDER BY level");
        while (q.next()) codes.push_back(q.text(0));
    }
    REQUIRE(codes.size() == 2);   // A and B
    codes.push_back("");                                   // no login
    codes.push_back(std::string(rows.size(), '1'));        // everything off
    codes.push_back(std::string(rows.size() / 2, '1'));    // a code that stops halfway
    std::string alternate;
    for (size_t i = 0; i < rows.size() + 5; ++i) alternate += i % 2 ? '1' : '0';
    codes.push_back(alternate);                            // longer than the rows

    for (const std::string& code : codes) {
        INFO("code: " << code);
        const auto expected = legacyEnabled(rows, code);
        for (Id id : cmd::CommandIds()) {
            INFO("command " << id);
            CHECK(rights.MayRun(id, code) == expected.at(id));
        }
    }
    // The clerk (B) has no Maintenance menu; the administrator (A) has all of it.
    int maintenance = 0;
    for (Id id : cmd::CommandIds()) {
        const auto path = cmd::PathOf(id);
        if (path.empty() || path.front() != L"Maintenance") continue;
        ++maintenance;
        CHECK(rights.MayRun(id, codes[0]));
        CHECK_FALSE(rights.MayRun(id, codes[1]));
    }
    CHECK(maintenance > 0);
}

TEST_CASE("Rights without accessm, and a command with two rows (S8-M0)", "[commands]")
{
    const cmd::Rights none = cmd::Rights::FromIds({});
    for (Id id : cmd::CommandIds()) CHECK(none.MayRun(id, "1111111111"));
    // 1004 twice: the last row the code reaches decides, as the walk's later EnableMenuItem.
    const cmd::Rights twice = cmd::Rights::FromIds({1004, 1, 2, 1004});
    CHECK_FALSE(twice.MayRun(1004, "0001"));   // row 4 decides
    CHECK(twice.MayRun(1004, "1110"));         // row 4 again, enabled over row 1
    CHECK(twice.MayRun(1004, "0000"));
    CHECK_FALSE(twice.MayRun(1004, "100"));    // the code doesn't reach row 4: row 1 decides
    CHECK(twice.MayRun(CM_STATEMENT_LAYOUT, "0000"));
    CHECK_FALSE(twice.MayRun(CM_STATEMENT_LAYOUT, "1"));   // follows the Chart of Accounts
}
