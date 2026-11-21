//
// gl_tests: the entry grid's model (spec #8 6.4; plan S8-M3 task 7): every
// key in every column kind, validation, totals in cents, balancing, and
// undoing a removed line; fixed rows (S9-M0).
//
#include "catch_amalgamated.hpp"

#include "model/EntryGridModel.h"

#include <string>
#include <vector>

namespace m = fx::model;
using m::GridAction;
using m::GridKey;

namespace {

m::EntryGridModel voucherGrid()
{
    std::vector<m::GridColumn> cols = {
        {"account", "Account", m::CellKind::Lookup, 7},
        {"name", "Name", m::CellKind::Text, 20, true},   // read-only
        {"doc", "Document", m::CellKind::Upper, 8},
        {"qty", "Qty", m::CellKind::Number, 4},
        {"debit", "Debit", m::CellKind::Amount, 12, false, true},
        {"credit", "Credit", m::CellKind::Amount, 12, false, true},
    };
    cols[0].validator.Add(m::Exists([](const std::string& s) { return s.empty() || s == "101000" || s == "410001"; },
                                    "Not an account in the chart."));
    return m::EntryGridModel(cols);
}

// Types `text` into the current cell as a key would: an edit from empty, then Tab.
GridAction type(m::EntryGridModel& g, const std::string& text, GridKey then = GridKey::Tab)
{
    REQUIRE(g.BeginEdit(false));
    g.SetEditText(text);
    return g.Key(then);
}

} // namespace

TEST_CASE("Entry grid: Tab, Shift+Tab, Enter, Up and Down move the current cell (S8-M3)", "[grid]")
{
    m::EntryGridModel g = voucherGrid();
    CHECK(g.Rows() == 1);   // the blank row
    CHECK(g.Lines() == 0);
    CHECK(g.col() == 0);
    CHECK(g.Key(GridKey::Tab) == GridAction::Moved);
    CHECK(g.col() == 2);   // the read-only name is skipped
    CHECK(g.Key(GridKey::Enter) == GridAction::Moved);   // Enter moves on, as Tab (Q3)
    CHECK(g.col() == 3);
    CHECK(g.Key(GridKey::ShiftTab) == GridAction::Moved);
    CHECK(g.col() == 2);
    g.SetCurrent(0, 5);
    CHECK(g.Key(GridKey::Tab) == GridAction::Moved);   // the blank row's end: back to its first cell
    CHECK(g.row() == 0);
    CHECK(g.col() == 0);
    CHECK(g.Key(GridKey::Up) == GridAction::None);
    CHECK(g.Key(GridKey::Down) == GridAction::None);
    CHECK(g.Key(GridKey::Esc) == GridAction::None);    // Esc closes nothing
}

TEST_CASE("Entry grid: editing commits, checks, cancels; lines and the blank row (S8-M3)", "[grid]")
{
    m::EntryGridModel g = voucherGrid();
    int commits = 0;
    g.OnCommit = [&](int row, int col) {
        ++commits;
        if (col == 0) g.SetCell(row, 1, g.Cell(row, 0) == "101000" ? "CASH" : "SALES");   // the page fills the name
    };

    // An account not in the chart is refused: still editing, with the message.
    CHECK(type(g, "999999") == GridAction::Refused);
    CHECK(g.editing());
    CHECK(g.message() == "Not an account in the chart.");
    g.SetEditText("101000");
    CHECK(g.Key(GridKey::Tab) == GridAction::Committed);
    CHECK(g.Cell(0, "name") == "CASH");
    CHECK(g.col() == 2);
    CHECK(g.Rows() == 2);   // a line now: a new blank row below it
    CHECK(g.Lines() == 1);

    CHECK(type(g, "inv-1") == GridAction::Committed);
    CHECK(g.Cell(0, "doc") == "INV-1");   // upper case
    CHECK(type(g, "1.5") == GridAction::Refused);   // a whole number
    g.SetEditText("3");
    CHECK(g.Key(GridKey::Tab) == GridAction::Committed);
    CHECK(type(g, "1,2x") == GridAction::Refused);   // not an amount
    g.SetEditText("1250.5");
    CHECK(g.Key(GridKey::Enter) == GridAction::Committed);
    CHECK(g.Cell(0, "debit") == "1,250.50");   // formatted
    CHECK(g.col() == 5);

    // F2 keeps the text; Esc drops the edit.
    g.SetCurrent(0, 4);
    CHECK(g.Key(GridKey::F2) == GridAction::Editing);
    CHECK(g.editText() == "1,250.50");
    g.SetEditText("7");
    CHECK(g.Key(GridKey::Esc) == GridAction::Cancelled);
    CHECK(g.Cell(0, "debit") == "1,250.50");
    CHECK_FALSE(g.editing());
    CHECK(g.Key(GridKey::F2) == GridAction::Editing);
    CHECK(g.Key(GridKey::F2) == GridAction::None);   // already editing
    g.CancelEdit();

    // A read-only cell isn't edited; Down commits and moves.
    g.SetCurrent(0, 1);
    CHECK_FALSE(g.BeginEdit());
    g.SetCurrent(0, 3);
    REQUIRE(g.BeginEdit());
    g.SetEditText("4");
    CHECK(g.Key(GridKey::Down) == GridAction::Committed);
    CHECK(g.row() == 1);
    CHECK(g.Cell(0, "qty") == "4");
    REQUIRE(g.BeginEdit());
    g.SetEditText("2");
    CHECK(g.Key(GridKey::Up) == GridAction::Committed);
    CHECK(g.row() == 0);
    CHECK(g.Lines() == 2);
    CHECK(commits >= 6);
}

TEST_CASE("Entry grid: totals in cents and balancing (S8-M3)", "[grid]")
{
    m::EntryGridModel g = voucherGrid();
    g.SetRows({{"101000", "", "A", "", "3,300.00", ""},
               {"410001", "", "A", "", "", "3,000.00"},
               {"410001", "", "A", "", "", "300.00"}});
    CHECK(g.Lines() == 3);
    CHECK(g.Rows() == 4);
    CHECK(g.Total("debit") == 330000);
    CHECK(g.Total("credit") == 330000);
    CHECK(g.Balances("debit", "credit"));
    g.SetCell(2, "credit", "300.01");
    CHECK(g.Total("credit") == 330001);
    CHECK_FALSE(g.Balances("debit", "credit"));
    m::EntryGridModel empty = voucherGrid();
    CHECK_FALSE(empty.Balances("debit", "credit"));   // nothing is not balanced
    // 0.1 + 0.2 in cents: no float drift.
    m::EntryGridModel cents = voucherGrid();
    cents.SetRows({{"101000", "", "", "", "0.10", ""}, {"101000", "", "", "", "0.20", ""}, {"410001", "", "", "", "", "0.30"}});
    CHECK(cents.Balances("debit", "credit"));
}

TEST_CASE("Entry grid: Insert, Ctrl+Delete and Ctrl+Z (S8-M3)", "[grid]")
{
    m::EntryGridModel g = voucherGrid();
    g.SetRows({{"101000", "", "A"}, {"410001", "", "B"}, {"410001", "", "C"}});
    g.SetCurrent(1, 0);
    CHECK(g.Key(GridKey::Insert) == GridAction::Inserted);
    CHECK(g.Rows() == 5);
    CHECK(g.IsBlank(1));
    CHECK(g.Lines() == 3);
    CHECK(g.Key(GridKey::CtrlDelete) == GridAction::Removed);   // the inserted blank row
    CHECK(g.Cell(1, "doc") == "B");
    CHECK(g.Key(GridKey::CtrlDelete) == GridAction::Removed);   // B
    CHECK(g.Cell(1, "doc") == "C");
    CHECK(g.Lines() == 2);
    CHECK(g.undo().front()[2] == "B");
    CHECK(g.Key(GridKey::CtrlZ) == GridAction::Restored);
    CHECK(g.Cell(1, "doc") == "B");
    CHECK(g.row() == 1);
    CHECK(g.Lines() == 3);
    CHECK(g.Key(GridKey::CtrlZ) == GridAction::Restored);       // the blank row comes back too
    CHECK(g.Key(GridKey::CtrlZ) == GridAction::None);           // nothing left to undo
    g.SetCurrent(g.Rows() - 1, 0);
    CHECK(g.Key(GridKey::CtrlDelete) == GridAction::None);      // the last blank row stays
    REQUIRE(g.BeginEdit());
    CHECK(g.Key(GridKey::Insert) == GridAction::None);          // not while editing
}

namespace {

// Budget Data's grid (spec #9 D1, D2): 12 periods, the budget edited.
m::EntryGridModel budgetGrid()
{
    m::EntryGridModel g({
        {"period", "Period", m::CellKind::Number, 6, true},
        {"budget", "Budget", m::CellKind::Amount, 16},
        {"current", "Current Year", m::CellKind::Amount, 16, true},
        {"previous", "Previous Year", m::CellKind::Amount, 16, true},
    });
    g.SetFixedRows(12);
    return g;
}

} // namespace

TEST_CASE("Entry grid: fixed rows have no blank row and no Insert or Ctrl+Delete (S9-M0)", "[grid]")
{
    m::EntryGridModel g = budgetGrid();
    CHECK(g.fixedRows() == 12);
    CHECK(g.Rows() == 12);
    CHECK(g.Lines() == 0);
    CHECK(g.col() == 1);   // the first editable column

    // SetRows pads to the count, and cuts what's beyond it.
    g.SetRows({{"1", "100.00", "90.00", "80.00"}, {"2", "200.00"}});
    CHECK(g.Rows() == 12);
    CHECK(g.Lines() == 2);
    std::vector<std::vector<std::string>> thirteen;
    for (int i = 1; i <= 13; ++i) thirteen.push_back({std::to_string(i)});
    g.SetRows(thirteen);
    CHECK(g.Rows() == 12);
    CHECK(g.Cell(11, "period") == "12");

    // Typing into the last row adds no row below it.
    g.SetCurrent(11, 1);
    CHECK(type(g, "1250", GridKey::Enter) == GridAction::Committed);
    CHECK(g.Cell(11, "budget") == "1,250.00");
    CHECK(g.Rows() == 12);
    CHECK(g.row() == 11);   // the last row's last editable cell: Enter stays
    CHECK(g.col() == 1);
    CHECK(g.Key(GridKey::Tab) == GridAction::Moved);
    CHECK(g.row() == 11);
    CHECK(g.Key(GridKey::Down) == GridAction::None);

    // Tab moves down the periods, skipping the read-only columns.
    g.SetCurrent(0, 1);
    CHECK(g.Key(GridKey::Tab) == GridAction::Moved);
    CHECK(g.row() == 1);
    CHECK(g.col() == 1);
    CHECK(g.Key(GridKey::ShiftTab) == GridAction::Moved);
    CHECK(g.row() == 0);

    // No lines are added or removed.
    CHECK(g.Key(GridKey::Insert) == GridAction::None);
    CHECK(g.Key(GridKey::CtrlDelete) == GridAction::None);
    CHECK(g.Key(GridKey::CtrlZ) == GridAction::None);
    CHECK(g.Rows() == 12);
    g.SetCell(5, "budget", "");
    CHECK(g.Rows() == 12);

    // The totals still sum the column; Clear gives 12 blank rows.
    g.SetRows({{"1", "100.00"}, {"2", "200.50"}});
    CHECK(g.Total("budget") == 30050);
    g.Clear();
    CHECK(g.Rows() == 12);
    CHECK(g.Lines() == 0);

    // Back to the usual grid: the blank row returns.
    g.SetFixedRows(0);
    CHECK(g.Rows() == 1);
    CHECK(g.Key(GridKey::Insert) == GridAction::Inserted);
}

TEST_CASE("Entry grid: a read-only grid shows any rows and takes no edit (S10-M0)", "[grid]")
{
    m::EntryGridModel g({{"period", "Pd", m::CellKind::Number, 4},
                         {"descript", "Description", m::CellKind::Text, 30},
                         {"amount", "Amount", m::CellKind::Amount, 16, false, true}});
    int changes = 0;
    g.OnChanged = [&] { ++changes; };
    g.SetReadOnly(true);
    CHECK(g.readOnly());
    CHECK(g.Rows() == 0);   // no blank row
    CHECK(changes == 1);

    // No rows: the keys do nothing, and nothing breaks.
    CHECK(g.Key(GridKey::Down) == GridAction::None);
    CHECK(g.Key(GridKey::Up) == GridAction::None);
    g.SetCurrent(5, 2);
    CHECK(g.row() == 0);
    CHECK_FALSE(g.BeginEdit(false));

    // Any number of rows, exactly as given.
    g.SetRows({{"1", "Opening", "1,000.00"}, {"4", "A change", "250.00"}, {"7", "Another", "-500.00"}});
    CHECK(g.Rows() == 3);
    CHECK(g.Lines() == 3);
    CHECK(g.Total("amount") == 75000);

    // No cell takes an edit; F2 and the row keys do nothing to the rows.
    CHECK_FALSE(g.BeginEdit(true));
    CHECK(g.Key(GridKey::F2) == GridAction::None);
    CHECK(g.Key(GridKey::Insert) == GridAction::None);
    CHECK(g.Key(GridKey::CtrlDelete) == GridAction::None);
    CHECK(g.Key(GridKey::CtrlZ) == GridAction::None);
    CHECK(g.Rows() == 3);

    // The keys move between the rows; Tab and Enter on the last row stay there.
    CHECK(g.row() == 0);
    CHECK(g.Key(GridKey::Tab) == GridAction::Moved);
    CHECK(g.row() == 1);
    CHECK(g.Key(GridKey::Enter) == GridAction::Moved);
    CHECK(g.row() == 2);
    CHECK(g.Key(GridKey::Enter) == GridAction::Moved);
    CHECK(g.row() == 2);
    CHECK(g.Key(GridKey::Down) == GridAction::None);
    CHECK(g.Key(GridKey::ShiftTab) == GridAction::Moved);
    CHECK(g.row() == 1);
    CHECK(g.Key(GridKey::Up) == GridAction::Moved);
    CHECK(g.row() == 0);

    // Fewer rows next time, then none.
    g.SetRows({{"2", "One line", "5.00"}});
    CHECK(g.Rows() == 1);
    g.SetRows({});
    CHECK(g.Rows() == 0);
    g.Clear();
    CHECK(g.Rows() == 0);

    // Back to the usual grid: the blank row returns and takes an edit.
    g.SetReadOnly(false);
    CHECK(g.Rows() == 1);
    CHECK(g.BeginEdit(false));
}
