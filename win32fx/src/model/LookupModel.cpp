//
// model/LookupModel.cpp - see model/LookupModel.h.
//
#include "model/LookupModel.h"

#include <algorithm>
#include <cctype>

namespace win32fx::model {

namespace {

std::string lower(const std::string& s)
{
    std::string out = s;
    for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

std::string trim(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t") - first + 1);
}

bool startsWith(const std::string& s, const std::string& prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// Does a word of `name` (after a space, a dash, a slash or a bracket) start with `q`?
bool wordStarts(const std::string& name, const std::string& q)
{
    for (size_t i = 0; i < name.size(); ++i) {
        const bool start = i == 0 || std::string(" -/(,.").find(name[i - 1]) != std::string::npos;
        if (start && name.compare(i, q.size(), q) == 0) return true;
    }
    return false;
}

} // namespace

std::vector<size_t> LookupModel::Match(const std::string& text, size_t limit) const
{
    const std::string q = lower(trim(text));
    std::vector<std::pair<int, size_t>> ranked;   // rank, index
    for (size_t i = 0; i < items_.size(); ++i) {
        const LookupItem& it = items_[i];
        int rank = -1;
        if (q.empty()) {
            rank = 0;
        } else {
            const std::string code = lower(it.code), key = lower(it.key()), name = lower(it.name);
            if (key == q || code == q) rank = 0;
            else if (startsWith(key, q) || startsWith(code, q)) rank = 1;
            else if (wordStarts(name, q)) rank = 2;
            else if (name.find(q) != std::string::npos) rank = 3;
        }
        if (rank >= 0) ranked.push_back({rank, i});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<size_t> out;
    for (const auto& [rank, i] : ranked) {
        if (limit && out.size() >= limit) break;
        out.push_back(i);
    }
    return out;
}

int LookupModel::Exact(const std::string& text) const
{
    const std::string q = lower(trim(text));
    if (q.empty()) return -1;
    for (size_t i = 0; i < items_.size(); ++i)
        if (lower(items_[i].key()) == q || (items_[i].sub.empty() && lower(items_[i].code) == q)) return static_cast<int>(i);
    return -1;
}

} // namespace win32fx::model
