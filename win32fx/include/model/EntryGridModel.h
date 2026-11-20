//
// EntryGridModel.h - the entry grid's logic, without a window (spec #8 6.4;
// plan S8-M3 task 2). Portable: fx::EntryGrid shows one, and gl_tests drives
// every key through it.
//
// The grid holds rows of typed text, one cell per column, and a current
// cell. There is always one blank row at the end to type the next line into;
// a row whose cells are all blank is not a line (Lines()).
//
// The key table (spec #8 6.4, Q3: Enter moves on, as Tab does):
//
//   key          not editing                        editing the cell
//   Tab, Enter   the next cell (wrapping to the     commit, then the next cell;
//                next row's first)                  refused: stay, with the message
//   Shift+Tab    the previous cell                  commit, then the previous cell
//   Up, Down     the row above / below              commit, then the row above / below
//   F2           edit the cell, keeping its text    -
//   (a key)      edit the cell from empty           -
//   Esc          nothing                            cancel: the text before
//   Insert       a blank row above this one         -
//   Ctrl+Delete  remove this row (undoable)         -
//   Ctrl+Z       put back the last row removed      -
//
// A read-only column is skipped by Tab and never edited. Amount columns
// marked `total` are summed in cents (Total). After a commit, OnCommit lets
// the page react (an account fills its name; a debit clears the credit).
//
#pragma once

#include "model/Validate.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace win32fx::model {

enum class CellKind { Text, Upper, Amount, Number, Lookup };

struct GridColumn {
    std::string key, heading;
    CellKind kind = CellKind::Text;
    int width = 10;          // characters (the view's column width, the edit's limit)
    bool readOnly = false;
    bool total = false;      // Amount: summed in Total()
    Validator validator;     // run on commit; Amount and Number get theirs from the kind as well
};

enum class GridKey { Tab, ShiftTab, Enter, Up, Down, F2, Esc, Insert, CtrlDelete, CtrlZ };

enum class GridAction {
    None,        // nothing happened
    Moved,       // the current cell changed
    Editing,     // an edit began
    Committed,   // the cell took the text (and the current cell may have moved)
    Cancelled,   // the edit was dropped
    Refused,     // the text failed the column's checks: still editing (message())
    Inserted,    // a blank row was inserted
    Removed,     // a row was removed
    Restored,    // a removed row was put back
};

class EntryGridModel {
public:
    explicit EntryGridModel(std::vector<GridColumn> columns = {});

    void SetColumns(std::vector<GridColumn> columns);
    const std::vector<GridColumn>& columns() const noexcept { return columns_; }
    int ColumnOf(const std::string& key) const;   // -1 if none

    // ---- the rows ----
    int Rows() const noexcept { return static_cast<int>(rows_.size()); }   // with the blank row at the end
    int Lines() const;                                                      // rows with any text
    bool IsBlank(int row) const;
    const std::string& Cell(int row, int col) const;
    std::string Cell(int row, const std::string& key) const;
    void SetCell(int row, int col, const std::string& text);                // no checks, no OnCommit
    void SetCell(int row, const std::string& key, const std::string& text);
    void Clear();                                                           // one blank row
    void SetRows(const std::vector<std::vector<std::string>>& rows);        // then the blank row

    // ---- the current cell ----
    int row() const noexcept { return row_; }
    int col() const noexcept { return col_; }
    void SetCurrent(int row, int col);   // clamped; ends an edit (committing it if it passes)

    // ---- editing ----
    bool editing() const noexcept { return editing_; }
    const std::string& editText() const noexcept { return edit_; }
    bool BeginEdit(bool keepText = true);    // false on a read-only cell
    void SetEditText(const std::string& text) { edit_ = text; }
    GridAction Commit();                     // Committed or Refused
    void CancelEdit();

    // ---- keys ----
    GridAction Key(GridKey key);
    const std::string& message() const noexcept { return message_; }   // why the last commit was refused

    // ---- totals ----
    std::int64_t Total(int col) const;   // the column's amounts, in cents
    std::int64_t Total(const std::string& key) const;
    // Debits equal credits, and are not zero.
    bool Balances(const std::string& debitKey, const std::string& creditKey) const;

    // After a cell takes new text: (row, col). The page may change other cells.
    std::function<void(int row, int col)> OnCommit;
    // After anything changes the rows or a cell (for the view and the totals).
    std::function<void()> OnChanged;

    std::vector<std::vector<std::string>> undo() const;   // the removed rows, the last first (tests)

private:
    bool editable(int col) const;
    int nextEditable(int col, int step) const;   // -1 past the end
    GridAction Move(int step);                   // Tab / Shift+Tab
    GridAction MoveRow(int step);
    void EnsureBlankRow();
    void Changed();

    std::vector<GridColumn> columns_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<std::pair<int, std::vector<std::string>>> removed_;   // (row, cells)
    int row_ = 0, col_ = 0;
    bool editing_ = false;
    std::string edit_, message_;
};

} // namespace win32fx::model

namespace fx = win32fx;
