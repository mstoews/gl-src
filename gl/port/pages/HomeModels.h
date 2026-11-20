//
// HomeModels.h - the home page, the report gallery and command search,
// without windows (spec #8 4, 5.4, 5.5; plan S8-M5). Portable: HomePages.cpp
// shows them, and gl_tests checks the figures against the reports, the
// gallery's favourites and recent, and the search's ranking.
//
#pragma once

#include "cb4sqlite.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// ---- the home page (spec #8 5.5) --------------------------------------------------------

struct HomeFigures {
    int period = 0;                  // the lowest open period (what entry selects)
    std::string periodName;          // its gl_pds description
    int openPeriods = 0;
    int glWaiting = 0, apWaiting = 0, arWaiting = 0;   // open vouchers: to post
    int journalRows = 0;             // AP and AR journal rows waiting for Update AR and AP
    int chequesToPrint = 0;          // cheques made, not printed ("P")
    bool depreciationRun = false;    // the period's depreciation has been run
    long depreciationVoucher = 0;
    int assetsDue = 0;               // assets the run would depreciate
    std::int64_t arOwed = 0, arOverdue = 0;   // Open Receivables Detail's total; the part past due
    std::int64_t apOwed = 0, apOverdue = 0;   // Open Payables Detail's total; the part past due
};

// From the cores: Vouchers.h, Ap.h, Ar.h, Assets.h. `today` is yyyymmdd.
HomeFigures LoadHomeFigures(cb4sqlite::Code4& code, const std::string& today);

// ---- the report gallery (spec #8 5.4) ----------------------------------------------------

struct GalleryEntry {
    std::uint16_t command = 0;       // a built-in report: its menu command
    long definition = 0;             // a user report: its gl_rptdef number
    std::string name, group;         // group: the menu it's under ("General Ledger"), or "User reports"
    std::string detail;              // a user report's description and maker
    std::string key() const;         // "c1234" / "u5": favourites and recent
};

class GalleryModel {
public:
    void SetEntries(std::vector<GalleryEntry> entries) { entries_ = std::move(entries); }
    const std::vector<GalleryEntry>& entries() const noexcept { return entries_; }

    // The entries matching `text` (name words, then anywhere in the name or group), in gallery order.
    std::vector<size_t> Filter(const std::string& text) const;

    // Favourites and the recent ones, as GL_UISTATE keeps them: "c1234,u5".
    void LoadState(const std::string& favourites, const std::string& recent);
    std::string favouritesText() const;
    std::string recentText() const;

    bool IsFavourite(const GalleryEntry& e) const;
    void ToggleFavourite(const GalleryEntry& e);
    void Opened(const GalleryEntry& e);   // first in Recent; at most kRecent kept
    // The favourites and the recent entries that still exist, in their order.
    std::vector<size_t> Favourites() const;
    std::vector<size_t> Recent() const;

    static constexpr size_t kRecent = 8;

private:
    int IndexOf(const std::string& key) const;
    std::vector<GalleryEntry> entries_;
    std::vector<std::string> favourites_, recent_;
};

// The built-in reports (the menu's report commands under Reporting, and the
// AP and AR report items), grouped by their menu, then gl_rptdef's.
std::vector<GalleryEntry> LoadGalleryEntries(cb4sqlite::Code4& code);

// ---- command search (spec #8 4) -------------------------------------------------------

struct SearchItem {
    enum class Kind { Command, Account, Customer, Vendor, Voucher };
    Kind kind = Kind::Command;
    std::string code;                // an account, customer, vendor or voucher number; "" for a command
    std::string label;               // the command's menu path ("Accounts > Chart of Accounts"), or the name
    std::uint16_t command = 0;       // Command
};

class SearchModel {
public:
    void SetItems(std::vector<SearchItem> items) { items_ = std::move(items); }
    const std::vector<SearchItem>& items() const noexcept { return items_; }
    // Best first: an exact code; a code prefix; a word of the label starting with
    // the text (each word of the text, for several); anywhere in the label.
    // Commands before masters at the same rank; then in the items' order.
    std::vector<size_t> Match(const std::string& text, size_t limit = 20) const;

private:
    std::vector<SearchItem> items_;
};

// The commands (those `enabled` allows: greyed ones left out), and the
// accounts, customers, vendors and open GL vouchers.
std::vector<SearchItem> LoadSearchItems(cb4sqlite::Code4& code, const std::function<bool(std::uint16_t)>& enabled);
