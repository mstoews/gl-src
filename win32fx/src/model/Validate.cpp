//
// model/Validate.cpp - see model/Validate.h.
//
#include "model/Validate.h"

#include <cctype>
#include <cstdio>

namespace win32fx::model {

std::string Trim(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

namespace {

size_t codePoints(const std::string& s)
{
    size_t n = 0;
    for (unsigned char ch : s)
        if ((ch & 0xC0) != 0x80) ++n;
    return n;
}

bool parseWhole(const std::string& t, long long& out)
{
    if (t.empty()) return false;
    size_t i = 0;
    bool neg = false;
    if (t[0] == '-' || t[0] == '+') {
        neg = t[0] == '-';
        i = 1;
    }
    if (i >= t.size()) return false;
    long long v = 0;
    for (; i < t.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(t[i]))) return false;
        v = v * 10 + (t[i] - '0');
        if (v > 1000000000000LL) return false;
    }
    out = neg ? -v : v;
    return true;
}

bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

} // namespace

std::optional<std::int64_t> ParseAmount(const std::string& text)
{
    const std::string t = Trim(text);
    if (t.empty()) return std::nullopt;
    size_t i = 0;
    bool neg = false;
    if (t[0] == '-' || t[0] == '+') {
        neg = t[0] == '-';
        i = 1;
    }
    std::int64_t whole = 0, frac = 0;
    int fracDigits = -1, digits = 0;
    for (; i < t.size(); ++i) {
        const char ch = t[i];
        if (ch == ',' && fracDigits < 0) continue;   // thousands separators, before the point only
        if (ch == '.' && fracDigits < 0) {
            fracDigits = 0;
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(ch))) return std::nullopt;
        if (fracDigits >= 0) {
            if (++fracDigits > 2) return std::nullopt;
            frac = frac * 10 + (ch - '0');
        } else {
            whole = whole * 10 + (ch - '0');
            if (whole > 100000000000000LL) return std::nullopt;
        }
        ++digits;
    }
    if (digits == 0) return std::nullopt;
    if (fracDigits == 1) frac *= 10;
    const std::int64_t cents = whole * 100 + frac;
    return neg ? -cents : cents;
}

std::string FormatAmount(std::int64_t cents)
{
    const bool neg = cents < 0;
    const std::uint64_t a = neg ? 0 - static_cast<std::uint64_t>(cents) : static_cast<std::uint64_t>(cents);
    std::string whole = std::to_string(a / 100);
    for (int at = static_cast<int>(whole.size()) - 3; at > 0; at -= 3) whole.insert(static_cast<size_t>(at), ",");
    char frac[4];
    std::snprintf(frac, sizeof frac, "%02u", static_cast<unsigned>(a % 100));
    return (neg ? "-" : "") + whole + "." + frac;
}

Check Required()
{
    return [](const std::string& text) -> std::optional<std::string> {
        if (Trim(text).empty()) return std::string("Required.");
        return std::nullopt;
    };
}

Check MaxLength(size_t chars)
{
    return [chars](const std::string& text) -> std::optional<std::string> {
        if (codePoints(Trim(text)) > chars) return "At most " + std::to_string(chars) + " characters.";
        return std::nullopt;
    };
}

Check WholeNumber(long long min, long long max)
{
    return [min, max](const std::string& text) -> std::optional<std::string> {
        const std::string t = Trim(text);
        if (t.empty()) return std::nullopt;
        long long v = 0;
        if (!parseWhole(t, v) || v < min || v > max)
            return "A whole number from " + std::to_string(min) + " to " + std::to_string(max) + ".";
        return std::nullopt;
    };
}

Check Amount()
{
    return [](const std::string& text) -> std::optional<std::string> {
        if (Trim(text).empty() || ParseAmount(text)) return std::nullopt;
        return std::string("Not an amount.");
    };
}

Check Date()
{
    return [](const std::string& text) -> std::optional<std::string> {
        const std::string t = Trim(text);
        if (t.empty()) return std::nullopt;
        long long v = 0;
        if (t.size() != 8 || !parseWhole(t, v)) return std::string("Not a date.");
        const int y = static_cast<int>(v / 10000), m = static_cast<int>(v / 100 % 100), d = static_cast<int>(v % 100);
        static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (y < 1900 || m < 1 || m > 12 || d < 1 || d > days[m - 1] + (m == 2 && leap(y) ? 1 : 0))
            return std::string("Not a date.");
        return std::nullopt;
    };
}

Check Exists(std::function<bool(const std::string&)> found, std::string message)
{
    return [found = std::move(found), message = std::move(message)](const std::string& text) -> std::optional<std::string> {
        const std::string t = Trim(text);
        if (t.empty() || found(t)) return std::nullopt;
        return message;
    };
}

Validator& Validator::Add(Check c)
{
    checks_.push_back(std::move(c));
    return *this;
}

std::optional<std::string> Validator::Run(const std::string& text) const
{
    for (const Check& c : checks_)
        if (auto why = c(text)) return why;
    return std::nullopt;
}

} // namespace win32fx::model
