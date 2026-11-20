//
// model/LookupModel.h - which codes match what is typed (spec #8 6.3; plan
// S8-M1 task 4). Portable: fx::Lookup (Lookup.h) shows the matches under
// its edit, and a list page's filter box uses the same matching.
//
// An item has a code, an optional sub-code (an account's dept) and a name.
// Typed text matches, best first:
//   1. the code exactly (or code-sub, "410001-10");
//   2. a code starting with it;
//   3. a word of the name starting with it ("sales" finds "Sales revenue",
//      "rev" finds it too);
//   4. the name containing it anywhere.
// Case is ignored (ASCII). Ties keep the items' own order. Blank text
// matches everything, in order.
//
#pragma once

#include <string>
#include <vector>

namespace win32fx::model {

struct LookupItem {
    std::string code;
    std::string sub;    // "" if none
    std::string name;

    // "410001-10" or "111101".
    std::string key() const { return sub.empty() ? code : code + "-" + sub; }
};

class LookupModel {
public:
    LookupModel() = default;
    explicit LookupModel(std::vector<LookupItem> items) : items_(std::move(items)) {}

    void SetItems(std::vector<LookupItem> items) { items_ = std::move(items); }
    const std::vector<LookupItem>& items() const noexcept { return items_; }

    // The indexes of the items matching `text`, best first, at most `limit` (0: all).
    std::vector<size_t> Match(const std::string& text, size_t limit = 0) const;

    // The item whose key() or code (when it has no sub) is exactly `text`, or -1.
    int Exact(const std::string& text) const;

private:
    std::vector<LookupItem> items_;
};

} // namespace win32fx::model

namespace fx = win32fx;   // as win32fx.h names it: portable code uses fx::model too
