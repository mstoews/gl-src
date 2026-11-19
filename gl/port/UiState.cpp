//
// UiState.cpp - see UiState.h.
//
#include "UiState.h"

#include "Schema.h"   // cb4sqlite::createTable

namespace {

bool hasTable(cb4sqlite::Code4& code)
{
    cb4sqlite::Query q(code, "SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name = 'gl_uistate'");
    return q.next() && q.integer(0) > 0;
}

std::string trim(const std::string& s)
{
    const auto first = s.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(' ') - first + 1);
}

std::string clip(const std::string& s, size_t n) { return s.size() > n ? s.substr(0, n) : s; }

} // namespace

void EnsureUiStateTable(cb4sqlite::Code4& code)
{
    if (hasTable(code)) return;
    cb4sqlite::Transaction tx(code);
    cb4sqlite::createTable(code, "gl_uistate", {{"USERNAME", 'C', 10}, {"UIKEY", 'C', 40}, {"UIVALUE", 'M', 10}});
    cb4sqlite::createTag(code, "gl_uistate", "userkey", "\"USERNAME\" || \"UIKEY\"");
    tx.commit();
}

std::optional<std::string> GetUiState(cb4sqlite::Code4& code, const std::string& user, const std::string& key)
{
    if (!hasTable(code)) return std::nullopt;
    cb4sqlite::Query q(code, "SELECT uivalue FROM gl_uistate WHERE _deleted = 0 AND trim(username) = ?1 AND "
                             "trim(uikey) = ?2 ORDER BY _rowid DESC LIMIT 1");
    q.bindText(1, clip(trim(user), 10)).bindText(2, clip(trim(key), 40));
    if (!q.next()) return std::nullopt;
    return q.text(0);
}

void SetUiState(cb4sqlite::Code4& code, const std::string& user, const std::string& key, const std::string& value)
{
    EnsureUiStateTable(code);
    const std::string u = clip(trim(user), 10), k = clip(trim(key), 40);
    cb4sqlite::Transaction tx(code);
    Data4 d(&code, "gl_uistate");
    Field4 username(d, "username"), uikey(d, "uikey"), uivalue(d, "uivalue");
    bool found = false;
    for (d.top(); !d.eof(); d.skip()) {
        if (d.deleted() || trim(username.str()) != u || trim(uikey.str()) != k) continue;
        if (found || value.empty()) {
            d.deleteRec();   // a duplicate, or the value removed
        } else {
            uivalue.assign(value.c_str());
            d.flush();
        }
        found = true;
    }
    if (!found && !value.empty()) {
        d.appendStart();
        d.blank();
        username.assign(u.c_str());
        uikey.assign(k.c_str());
        uivalue.assign(value.c_str());
        d.append();
    }
    d.pack();
    d.close();
    tx.commit();
}

std::string UiStateUser(const std::string& szUser)
{
    const std::string u = trim(szUser);
    return u == "NOLOGIN" ? std::string() : u;
}
