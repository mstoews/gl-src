//
// Commands.h - the command table and the rights (spec #8 8; plan S8-M0
// task 4). Portable: no Win32. The Win32 half, which builds the menu bar and
// the navigation pane from these rows, is CommandsMenu.{h,cpp}.
//
// The rows are generated from the MANILA menu (CommandsTable.inc, by
// gl/res/tools/make_commands.py), so the table starts as exactly the menu
// the program had. Every command keeps its ID (D6).
//
// Rights are the original's, unchanged in storage: a level's menu code is
// one character per accessm row, in the "menu" tag's order (COUNT), '1'
// greying that row's command. ApplyMenuAccess walked the rows in step with
// the code and greyed the menu; Rights looks a command's row up by its ID
// instead, so one check (MayRun) serves the menu, the toolbar, the
// navigation pane and, later, pages (spec #8 8).
//
#pragma once

#include "cb4sqlite.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cmd {

using Id = std::uint16_t;

enum class Kind { Popup, Item, Separator };

// Menu item flags, as the rc file gives them.
inline constexpr unsigned kGrayed = 1, kChecked = 2, kBarBreak = 4, kBreak = 8, kHelp = 16;

struct MenuRow {
    int depth = 0;            // 0: a top-level popup
    Kind kind = Kind::Item;
    const wchar_t* text = L"";   // with '&' and any "\t" accelerator text, as in the menu
    Id id = 0;                // Item only
    unsigned flags = 0;
};

// The MANILA menu, line by line, in order.
const std::vector<MenuRow>& MenuRows();

// The commands (Item rows) in menu order, each once.
std::vector<Id> CommandIds();

// The menu text without '&', trailing dots or an accelerator ("Chart of Accounts").
std::wstring PlainText(const wchar_t* menuText);

// The popup path of a command ("Transactions", "Post Voucher"); empty if it isn't in the table.
std::vector<std::wstring> PathOf(Id id);

// The command whose right a command follows, or itself. Statement Layout is
// behind the Chart of Accounts (spec #3 6.4), the Recover Purchase and Sales
// Voucher items behind Recover General Ledger Voucher, and the Voucher List
// behind General Ledger Voucher entry: the greying ManilaGL's
// WM_INITMENUPOPUP added after ApplyMenuAccess. The classic Sales Customers
// dialog (spec #8 S8-M1, not on the menu) follows Sales Customers.
Id RightOf(Id id);

class Rights {
public:
    // accessm in the "menu" tag's order, as ApplyMenuAccess walked it (deleted
    // rows included: Data4 shows them, as CodeBase did). No accessm: no rows.
    static Rights Load(cb4sqlite::Code4& code);
    static Rights FromIds(std::vector<Id> rowIds);   // tests

    // False only when the command's row has '1' in menuCode: of its rows, the
    // last one the code reaches (the walk stopped at the code's end, and a
    // later EnableMenuItem won). No row, or a code too short to reach any:
    // allowed, as the walk left such a menu item enabled. RightOf applies first.
    bool MayRun(Id id, const std::string& menuCode) const;

    size_t rows() const noexcept { return rows_; }

private:
    std::map<Id, std::vector<size_t>> index_;   // command -> its rows' places, in order
    size_t rows_ = 0;
};

} // namespace cmd
