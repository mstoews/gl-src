//
// HomeModels.cpp - see HomeModels.h.
//
#include "HomeModels.h"

#include "../Accounts.h"   // TrimText
#include "../Ap.h"
#include "../Ar.h"
#include "../Assets.h"
#include "../Commands.h"
#include "../ReportWriter.h"
#include "../Vouchers.h"
#include "../PortIds.h"
#include "model/Validate.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <sstream>

namespace m = fx::model;

namespace {

std::string lower(std::string s)
{
    for (char& c : s)
        if (static_cast<unsigned char>(c) < 0x80) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> words(const std::string& s)
{
    std::vector<std::string> out;
    std::string w;
    for (char c : s) {
        const bool sep = c == ' ' || c == '>' || c == ',' || c == '-' || c == '/' || c == '(' || c == ')' || c == '.' || c == ':';
        if (sep) {
            if (!w.empty()) out.push_back(w);
            w.clear();
        } else {
            w += c;
        }
    }
    if (!w.empty()) out.push_back(w);
    return out;
}

std::string narrow(const std::wstring& w)
{
    std::string s;
    for (wchar_t c : w) s += c < 0x80 ? static_cast<char>(c) : '?';   // menu text is ASCII
    return s;
}

int countOf(cb4sqlite::Code4& code, const char* sql)
{
    try {
        cb4sqlite::Query q(code, sql);
        return q.next() ? std::atoi(q.text(0).c_str()) : 0;
    } catch (const std::exception&) {
        return 0;
    }
}

std::vector<std::string> split(const std::string& s)
{
    std::vector<std::string> out;
    std::stringstream in(s);
    std::string item;
    while (std::getline(in, item, ','))
        if (!m::Trim(item).empty()) out.push_back(m::Trim(item));
    return out;
}

std::string join(const std::vector<std::string>& v)
{
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : ",") + x;
    return s;
}

} // namespace

// ---- home ---------------------------------------------------------------------------------

HomeFigures LoadHomeFigures(cb4sqlite::Code4& code, const std::string& today)
{
    HomeFigures f;
    const std::vector<OpenPeriod> periods = OpenPeriods(code);
    f.openPeriods = static_cast<int>(periods.size());
    if (!periods.empty()) {
        f.period = periods.back().number;
        f.periodName = periods.back().description;
    }
    f.glWaiting = countOf(code, "SELECT count(*) FROM gl_btch WHERE _deleted = 0 AND trim(mode) IN ('1', '3')");
    for (ApKind k : {ApKind::Invoice, ApKind::Cash})
        for (const ApVoucher& v : LoadApVouchers(code, k)) f.apWaiting += v.mode == ap_mode::kOpen;
    for (const ArVoucher& v : LoadArVouchers(code)) f.arWaiting += v.mode == ar_mode::kOpen;
    f.journalRows = countOf(code, "SELECT count(*) FROM ap_jsum WHERE _deleted = 0 AND trim(coalesce(mode, '')) IN ('', '1')") +
                    countOf(code, "SELECT count(*) FROM ar_jsum WHERE _deleted = 0 AND trim(coalesce(mode, '')) IN ('', '1')");
    f.chequesToPrint = static_cast<int>(LoadChequesIn(code, ap_payment::kPaid).size());
    if (f.period) {
        EnsureAssetTables(code);
        const RunPlan plan = PlanRun(code, f.period);
        f.depreciationRun = plan.alreadyRun;
        f.depreciationVoucher = plan.existingVoucher;
        f.assetsDue = static_cast<int>(plan.due.size());
    }
    for (const ArOpenItem& i : LoadArOpenItems(code)) {   // as Open Receivables Detail lists them
        if (i.paymentCtl == ar_receipt::kSettled || i.remainder == 0) continue;
        f.arOwed += i.remainder;
        if (!i.dueDate.empty() && i.dueDate < today) f.arOverdue += i.remainder;
    }
    for (const OpenItem& i : LoadOpenItems(code)) {   // as Open Payables Detail lists them
        if (i.paymentCtl != ap_payment::kHeld && i.paymentCtl != ap_payment::kOpen && i.paymentCtl != ap_payment::kForced) continue;
        const std::int64_t owed = AmountOwed(i);
        f.apOwed += owed;
        if (!i.dueDate.empty() && i.dueDate < today) f.apOverdue += owed;
    }
    return f;
}

// ---- gallery ------------------------------------------------------------------------------

std::string GalleryEntry::key() const
{
    return command ? "c" + std::to_string(command) : "u" + std::to_string(definition);
}

std::vector<size_t> GalleryModel::Filter(const std::string& text) const
{
    const std::string t = lower(m::Trim(text));
    std::vector<size_t> first, then;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (t.empty()) {
            first.push_back(i);
            continue;
        }
        const std::string name = lower(entries_[i].name);
        bool wordHit = false;
        for (const std::string& w : words(name))
            if (w.rfind(t, 0) == 0) wordHit = true;
        if (wordHit) first.push_back(i);
        else if (name.find(t) != std::string::npos || lower(entries_[i].group).find(t) != std::string::npos ||
                 lower(entries_[i].detail).find(t) != std::string::npos)
            then.push_back(i);
    }
    first.insert(first.end(), then.begin(), then.end());
    return first;
}

void GalleryModel::LoadState(const std::string& favourites, const std::string& recent)
{
    favourites_ = split(favourites);
    recent_ = split(recent);
    if (recent_.size() > kRecent) recent_.resize(kRecent);
}

std::string GalleryModel::favouritesText() const { return join(favourites_); }
std::string GalleryModel::recentText() const { return join(recent_); }

bool GalleryModel::IsFavourite(const GalleryEntry& e) const
{
    return std::find(favourites_.begin(), favourites_.end(), e.key()) != favourites_.end();
}

void GalleryModel::ToggleFavourite(const GalleryEntry& e)
{
    const auto it = std::find(favourites_.begin(), favourites_.end(), e.key());
    if (it != favourites_.end()) favourites_.erase(it);
    else favourites_.push_back(e.key());
}

void GalleryModel::Opened(const GalleryEntry& e)
{
    recent_.erase(std::remove(recent_.begin(), recent_.end(), e.key()), recent_.end());
    recent_.insert(recent_.begin(), e.key());
    if (recent_.size() > kRecent) recent_.resize(kRecent);
}

int GalleryModel::IndexOf(const std::string& key) const
{
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].key() == key) return static_cast<int>(i);
    return -1;
}

std::vector<size_t> GalleryModel::Favourites() const
{
    std::vector<size_t> out;
    for (const std::string& k : favourites_)
        if (const int i = IndexOf(k); i >= 0) out.push_back(static_cast<size_t>(i));
    return out;
}

std::vector<size_t> GalleryModel::Recent() const
{
    std::vector<size_t> out;
    for (const std::string& k : recent_)
        if (const int i = IndexOf(k); i >= 0) out.push_back(static_cast<size_t>(i));
    return out;
}

std::vector<GalleryEntry> LoadGalleryEntries(cb4sqlite::Code4& code)
{
    std::vector<GalleryEntry> out;
    std::set<cmd::Id> seen;
    // The report commands outside the Reporting menu.
    const std::set<cmd::Id> extra = {10071, 10073, 1019, 1020, 4092, 4093, 4091};
    std::vector<std::wstring> path;   // the popups above the row (PathOf gives a command's first place only)
    for (const cmd::MenuRow& r : cmd::MenuRows()) {
        if (path.size() > static_cast<size_t>(r.depth)) path.resize(static_cast<size_t>(r.depth));
        if (r.kind == cmd::Kind::Popup) {
            path.push_back(cmd::PlainText(r.text));
            continue;
        }
        if (r.kind != cmd::Kind::Item || r.id == 0 || r.id == CM_REPORT_LIST || seen.count(r.id)) continue;
        const bool reporting = !path.empty() && path[0] == L"Reporting";
        if (!reporting && !extra.count(r.id)) continue;
        seen.insert(r.id);
        GalleryEntry e;
        e.command = r.id;
        std::string name = narrow(cmd::PlainText(r.text));
        while (!name.empty() && name.back() == '.') name.pop_back();
        e.name = name;
        e.group = reporting ? (path.size() > 1 ? narrow(path[1]) : "Reporting") : narrow(path.empty() ? L"" : path[0]);
        out.push_back(std::move(e));
    }
    std::stable_sort(out.begin(), out.end(), [](const GalleryEntry& a, const GalleryEntry& b) { return a.group < b.group; });
    try {
        for (const rw::StoredDefinition& d : rw::LoadDefinitions(code)) {
            GalleryEntry e;
            e.definition = d.number;
            e.name = d.name;
            e.group = "User reports";
            e.detail = TrimText(d.description) + (d.madeBy.empty() ? "" : " (" + TrimText(d.madeBy) + ")");
            out.push_back(std::move(e));
        }
    } catch (const std::exception&) {   // no gl_rptdef yet
    }
    return out;
}

// ---- search -------------------------------------------------------------------------------

std::vector<size_t> SearchModel::Match(const std::string& text, size_t limit) const
{
    const std::string t = lower(m::Trim(text));
    if (t.empty()) return {};
    const std::vector<std::string> tw = words(t);
    struct Hit {
        int rank;
        int whole;   // query words that are whole words of the label: more first
        int kind;
        size_t index;
    };
    std::vector<Hit> hits;
    for (size_t i = 0; i < items_.size(); ++i) {
        const SearchItem& it = items_[i];
        const std::string code = lower(it.code), label = lower(it.label);
        int rank = -1, whole = 0;
        if (!code.empty() && code == t) rank = 0;
        else if (!code.empty() && code.rfind(t, 0) == 0) rank = 1;
        else {
            const std::vector<std::string> lw = words(label);
            bool all = !tw.empty();
            for (const std::string& q : tw) {
                bool found = false, exact = false;
                for (const std::string& w : lw) {
                    if (w.rfind(q, 0) == 0) found = true;
                    if (w == q) exact = true;
                }
                all = all && found;
                whole += exact ? 1 : 0;
            }
            if (all) rank = 2;
            else if (label.find(t) != std::string::npos) rank = 3;
        }
        if (rank >= 0) hits.push_back({rank, whole, it.kind == SearchItem::Kind::Command ? 0 : 1, i});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        if (a.whole != b.whole) return a.whole > b.whole;
        return a.kind < b.kind;
    });
    std::vector<size_t> out;
    for (const Hit& h : hits) {
        if (out.size() >= limit) break;
        out.push_back(h.index);
    }
    return out;
}

std::vector<SearchItem> LoadSearchItems(cb4sqlite::Code4& code, const std::function<bool(std::uint16_t)>& enabled)
{
    std::vector<SearchItem> items;
    std::set<cmd::Id> seen;
    std::vector<std::wstring> path;
    for (const cmd::MenuRow& r : cmd::MenuRows()) {
        if (path.size() > static_cast<size_t>(r.depth)) path.resize(static_cast<size_t>(r.depth));
        if (r.kind == cmd::Kind::Popup) {
            path.push_back(cmd::PlainText(r.text));
            continue;
        }
        if (r.kind != cmd::Kind::Item || r.id == 0 || seen.count(r.id)) continue;
        if (!path.empty() && path[0] == L"Window") continue;
        if (enabled && !enabled(r.id)) continue;   // greyed: left out
        seen.insert(r.id);
        std::string label;
        for (const std::wstring& p : path) label += narrow(p) + " > ";
        std::string text = narrow(cmd::PlainText(r.text));
        while (!text.empty() && text.back() == '.') text.pop_back();
        items.push_back({SearchItem::Kind::Command, "", label + text, r.id});
    }
    auto add = [&](SearchItem::Kind kind, const char* sql, const char* what) {
        try {
            cb4sqlite::Query q(code, sql);
            while (q.next()) items.push_back({kind, q.text(0), std::string(what) + " " + q.text(0) + "  " + q.text(1), 0});
        } catch (const std::exception&) {
        }
    };
    add(SearchItem::Kind::Account,
        "SELECT trim(account) || CASE WHEN trim(coalesce(dept, '')) = '' THEN '' ELSE '-' || trim(dept) END, trim(descript) "
        "FROM glaccts WHERE _deleted = 0 ORDER BY 1",
        "Account");
    add(SearchItem::Kind::Customer, "SELECT trim(vendorno), trim(vdname) FROM ar_cust WHERE _deleted = 0 ORDER BY 1", "Customer");
    add(SearchItem::Kind::Vendor, "SELECT trim(vendorno), trim(vdname) FROM ap_vend WHERE _deleted = 0 ORDER BY 1", "Vendor");
    add(SearchItem::Kind::Voucher,
        "SELECT batchno, trim(reference) FROM gl_btch WHERE _deleted = 0 AND trim(mode) IN ('1', '3') ORDER BY batchno",
        "GL voucher");
    return items;
}
