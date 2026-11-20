//
// RecordModel.cpp - see RecordModel.h.
//
#include "RecordModel.h"

#include "model/LookupModel.h"
#include "model/Validate.h"

#include <cctype>
#include <cstdlib>

namespace m = fx::model;

namespace {

std::string capitalized(std::string s)
{
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

} // namespace

std::vector<size_t> RecordModel::Filter(const std::string& text) const
{
    std::vector<m::LookupItem> items;
    items.reserve(rows_.size());
    for (const RecordRow& r : rows_) {
        std::string name;
        for (size_t i = 1; i < r.cells.size(); ++i) name += (i > 1 ? " " : "") + r.cells[i];
        items.push_back({r.cells.empty() ? r.key : r.cells[0], "", name});
    }
    return m::LookupModel(std::move(items)).Match(text);
}

int RecordModel::RowOf(const std::string& key) const
{
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].key == key) return static_cast<int>(i);
    return -1;
}

void RecordModel::New()
{
    values_ = Defaults();
    key_.clear();
    isNew_ = true;
    LoadGrids("");
}

bool RecordModel::Select(const std::string& key)
{
    Values v;
    if (key.empty() || !Read(key, v)) return false;
    values_ = std::move(v);
    key_ = key;
    isNew_ = false;
    LoadGrids(key);
    return true;
}

std::string RecordModel::value(const Values& v, const std::string& field) const
{
    const auto it = v.find(field);
    return it == v.end() ? std::string() : it->second;
}

std::string RecordModel::Get(const std::string& field) const
{
    return value(values_, field);
}

RecordModel::Result RecordModel::Save()
{
    for (auto& [field, text] : values_) text = m::Trim(text);
    if (isNew_) {
        const std::string key = KeyOf(values_);
        Values existing;
        if (!key.empty() && Read(key, existing))
            return Refuse(KeyField(), capitalized(Noun()) + " " + key + " is already on file. Choose it in the list to change it.");
    }
    Result r = Write(values_, isNew_);
    if (!r.ok) return r;
    const std::string key = isNew_ ? KeyOf(values_) : key_;
    if (r.message.empty()) r.message = capitalized(Noun()) + " " + key + " saved.";
    Load();
    Select(key);
    return r;
}

RecordModel::Result RecordModel::Delete()
{
    if (isNew_) return Refuse("", "Choose the " + Noun() + " to delete in the list.");
    const std::string key = key_;
    Result r = Remove(key);
    if (!r.ok) return r;
    if (r.message.empty()) r.message = capitalized(Noun()) + " " + key + " deleted.";
    Load();
    New();
    return r;
}

namespace record {

bool IsWholeNumber(const std::string& text, int lo, int hi)
{
    const std::string t = m::Trim(text);
    if (t.empty()) return lo <= 0 && 0 <= hi;
    if (t.size() > 9) return false;
    for (char c : t)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    const int n = std::atoi(t.c_str());
    return lo <= n && n <= hi;
}

int ToInt(const std::string& text)
{
    return std::atoi(m::Trim(text).c_str());
}

std::string Cents(std::int64_t cents)
{
    return cents ? m::FormatAmount(cents) : std::string();
}

std::int64_t ToCents(const std::string& text)
{
    const std::string t = m::Trim(text);
    if (t.empty()) return 0;
    return m::ParseAmount(t).value_or(0);
}

} // namespace record
