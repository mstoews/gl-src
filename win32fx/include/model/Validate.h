//
// model/Validate.h - a field's checks (spec #8 6.2; plan S8-M1 task 4).
// Portable: a page runs a field's validator when the field loses the focus
// and on Save, and marks the field with the message (Page::MarkField).
//
// Messages are short and say what is wrong, in the program's words:
// "Required.", "At most 6 characters.", "A whole number from 0 to 99.",
// "Not an amount.", "Not a date.".
//
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace win32fx::model {

// Text as typed: UTF-8, spaces at the ends trimmed before checking.
using Check = std::function<std::optional<std::string>(const std::string& text)>;

Check Required();
Check MaxLength(size_t chars);                       // UTF-8 code points
Check WholeNumber(long long min, long long max);     // blank passes (combine with Required)
Check Amount();                                      // "1250", "1,250.00", "-12.5"; blank passes
Check Date();                                        // yyyymmdd, a real date; blank passes
Check Exists(std::function<bool(const std::string&)> found, std::string message);   // blank passes

// The checks of one field, in order: the first failure is the message.
class Validator {
public:
    Validator() = default;
    Validator(std::initializer_list<Check> checks) : checks_(checks) {}
    Validator& Add(Check c);
    std::optional<std::string> Run(const std::string& text) const;
    bool empty() const noexcept { return checks_.empty(); }

private:
    std::vector<Check> checks_;
};

// "  a b  " -> "a b".
std::string Trim(const std::string& s);

// "1,250.50" -> 125050 cents; "-12.5" -> -1250; nullopt if not an amount
// (more than two decimals, stray characters).
std::optional<std::int64_t> ParseAmount(const std::string& text);

// 125050 -> "1,250.50"; -1250 -> "-12.50".
std::string FormatAmount(std::int64_t cents);

} // namespace win32fx::model

namespace fx = win32fx;   // as win32fx.h names it: portable code uses fx::model too
