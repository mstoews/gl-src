//
// EntryGridModel.cpp - see EntryGridModel.h.
//
#include "model/EntryGridModel.h"

#include <algorithm>
#include <cctype>

namespace win32fx::model {

namespace {

const std::string kEmpty;

std::string upper(std::string s)
{
    for (char& c : s)
        if (static_cast<unsigned char>(c) < 0x80) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

EntryGridModel::EntryGridModel(std::vector<GridColumn> columns)
{
    SetColumns(std::move(columns));
}

void EntryGridModel::SetColumns(std::vector<GridColumn> columns)
{
    columns_ = std::move(columns);
    Clear();
}

int EntryGridModel::ColumnOf(const std::string& key) const
{
    for (size_t i = 0; i < columns_.size(); ++i)
        if (columns_[i].key == key) return static_cast<int>(i);
    return -1;
}

int EntryGridModel::Lines() const
{
    int n = 0;
    for (int r = 0; r < Rows(); ++r)
        if (!IsBlank(r)) ++n;
    return n;
}

bool EntryGridModel::IsBlank(int row) const
{
    if (row < 0 || row >= Rows()) return true;
    for (const std::string& c : rows_[static_cast<size_t>(row)])
        if (!Trim(c).empty()) return false;
    return true;
}

const std::string& EntryGridModel::Cell(int row, int col) const
{
    if (row < 0 || row >= Rows() || col < 0 || col >= static_cast<int>(columns_.size())) return kEmpty;
    return rows_[static_cast<size_t>(row)][static_cast<size_t>(col)];
}

std::string EntryGridModel::Cell(int row, const std::string& key) const
{
    return Cell(row, ColumnOf(key));
}

void EntryGridModel::SetCell(int row, int col, const std::string& text)
{
    if (row < 0 || row >= Rows() || col < 0 || col >= static_cast<int>(columns_.size())) return;
    rows_[static_cast<size_t>(row)][static_cast<size_t>(col)] = text;
    EnsureBlankRow();
    Changed();
}

void EntryGridModel::SetCell(int row, const std::string& key, const std::string& text)
{
    SetCell(row, ColumnOf(key), text);
}

void EntryGridModel::SetFixedRows(int count)
{
    fixed_ = std::max(0, count);
    Clear();
}

void EntryGridModel::SetReadOnly(bool readOnly)
{
    readOnly_ = readOnly;
    Clear();
}

void EntryGridModel::Clear()
{
    rows_.clear();
    if (fixed_ > 0) rows_.assign(static_cast<size_t>(fixed_), std::vector<std::string>(columns_.size()));
    removed_.clear();
    editing_ = false;
    edit_.clear();
    row_ = 0;
    col_ = std::max(0, nextEditable(-1, 1));
    EnsureBlankRow();
    Changed();
}

void EntryGridModel::SetRows(const std::vector<std::vector<std::string>>& rows)
{
    rows_.clear();
    removed_.clear();
    for (std::vector<std::string> r : rows) {
        if (fixed_ > 0 && Rows() == fixed_) break;
        r.resize(columns_.size());
        rows_.push_back(std::move(r));
    }
    while (fixed_ > 0 && Rows() < fixed_) rows_.push_back(std::vector<std::string>(columns_.size()));
    editing_ = false;
    row_ = 0;
    col_ = std::max(0, nextEditable(-1, 1));
    EnsureBlankRow();
    Changed();
}

void EntryGridModel::SetCurrent(int row, int col)
{
    if (editing_ && Commit() == GridAction::Refused) CancelEdit();
    row_ = std::clamp(row, 0, std::max(0, Rows() - 1));   // a read-only grid can have no rows
    col_ = std::clamp(col, 0, std::max(0, static_cast<int>(columns_.size()) - 1));
}

bool EntryGridModel::editable(int col) const
{
    return !readOnly_ && col >= 0 && col < static_cast<int>(columns_.size()) &&
           !columns_[static_cast<size_t>(col)].readOnly;
}

int EntryGridModel::nextEditable(int col, int step) const
{
    for (int c = col + step; c >= 0 && c < static_cast<int>(columns_.size()); c += step)
        if (editable(c)) return c;
    return -1;
}

bool EntryGridModel::BeginEdit(bool keepText)
{
    if (!editable(col_) || row_ < 0 || row_ >= Rows()) return false;
    editing_ = true;
    edit_ = keepText ? Cell(row_, col_) : std::string();
    message_.clear();
    return true;
}

GridAction EntryGridModel::Commit()
{
    if (!editing_) return GridAction::None;
    const GridColumn& c = columns_[static_cast<size_t>(col_)];
    std::string text = Trim(edit_);
    if (c.kind == CellKind::Upper || c.kind == CellKind::Lookup) text = upper(text);
    std::optional<std::string> problem;
    if (c.kind == CellKind::Amount) problem = Amount()(text);
    if (!problem && c.kind == CellKind::Number) problem = WholeNumber(0, 999999999)(text);
    if (!problem && !c.validator.empty()) problem = c.validator.Run(text);
    if (problem) {
        message_ = *problem;
        return GridAction::Refused;
    }
    if (c.kind == CellKind::Amount && !text.empty()) text = FormatAmount(*ParseAmount(text));
    editing_ = false;
    message_.clear();
    const int r = row_, col = col_;
    rows_[static_cast<size_t>(r)][static_cast<size_t>(col)] = text;
    if (OnCommit) OnCommit(r, col);
    EnsureBlankRow();
    Changed();
    return GridAction::Committed;
}

void EntryGridModel::CancelEdit()
{
    editing_ = false;
    edit_.clear();
    message_.clear();
}

GridAction EntryGridModel::Move(int step)
{
    bool committed = false;
    if (editing_) {
        if (Commit() == GridAction::Refused) return GridAction::Refused;
        committed = true;
    }
    const int c = nextEditable(col_, step);
    if (c >= 0) {
        col_ = c;
    } else if (step > 0) {
        if (row_ + 1 < Rows()) {
            ++row_;
            col_ = std::max(0, nextEditable(-1, 1));
        } else if (fixed_ > 0 || readOnly_) {
            // The last row's last cell: stay.
        } else {
            col_ = std::max(0, nextEditable(-1, 1));   // the blank row: back to its first cell
        }
    } else if (row_ > 0) {
        --row_;
        col_ = std::max(0, nextEditable(static_cast<int>(columns_.size()), -1));
    }
    return committed ? GridAction::Committed : GridAction::Moved;
}

GridAction EntryGridModel::MoveRow(int step)
{
    bool committed = false;
    if (editing_) {
        if (Commit() == GridAction::Refused) return GridAction::Refused;
        committed = true;
    }
    const int r = std::clamp(row_ + step, 0, std::max(0, Rows() - 1));
    if (r == row_ && !committed) return GridAction::None;
    row_ = r;
    return committed ? GridAction::Committed : GridAction::Moved;
}

GridAction EntryGridModel::Key(GridKey key)
{
    switch (key) {
    case GridKey::Tab:
    case GridKey::Enter: return Move(1);
    case GridKey::ShiftTab: return Move(-1);
    case GridKey::Up: return MoveRow(-1);
    case GridKey::Down: return MoveRow(1);
    case GridKey::F2: return !editing_ && BeginEdit(true) ? GridAction::Editing : GridAction::None;
    case GridKey::Esc:
        if (!editing_) return GridAction::None;
        CancelEdit();
        return GridAction::Cancelled;
    case GridKey::Insert:
        if (editing_ || fixed_ > 0 || readOnly_) return GridAction::None;
        rows_.insert(rows_.begin() + row_, std::vector<std::string>(columns_.size()));
        Changed();
        return GridAction::Inserted;
    case GridKey::CtrlDelete:
        if (editing_ || fixed_ > 0 || readOnly_ || (IsBlank(row_) && row_ == Rows() - 1)) return GridAction::None;
        removed_.push_back({row_, rows_[static_cast<size_t>(row_)]});
        rows_.erase(rows_.begin() + row_);
        EnsureBlankRow();
        row_ = std::min(row_, Rows() - 1);
        Changed();
        return GridAction::Removed;
    case GridKey::CtrlZ: {
        if (editing_ || fixed_ > 0 || readOnly_ || removed_.empty()) return GridAction::None;
        auto [at, cells] = removed_.back();
        removed_.pop_back();
        at = std::min(at, Rows() - 1);   // before the blank row at most
        rows_.insert(rows_.begin() + at, std::move(cells));
        row_ = at;
        EnsureBlankRow();
        Changed();
        return GridAction::Restored;
    }
    }
    return GridAction::None;
}

std::int64_t EntryGridModel::Total(int col) const
{
    std::int64_t sum = 0;
    for (int r = 0; r < Rows(); ++r) {
        const std::string t = Trim(Cell(r, col));
        if (t.empty()) continue;
        if (const auto v = ParseAmount(t)) sum += *v;
    }
    return sum;
}

std::int64_t EntryGridModel::Total(const std::string& key) const
{
    return Total(ColumnOf(key));
}

bool EntryGridModel::Balances(const std::string& debitKey, const std::string& creditKey) const
{
    const std::int64_t d = Total(debitKey), c = Total(creditKey);
    return d != 0 && d == c;
}

std::vector<std::vector<std::string>> EntryGridModel::undo() const
{
    std::vector<std::vector<std::string>> v;
    for (auto it = removed_.rbegin(); it != removed_.rend(); ++it) v.push_back(it->second);
    return v;
}

void EntryGridModel::EnsureBlankRow()
{
    if (fixed_ > 0 || readOnly_) return;   // no blank row to type into
    if (rows_.empty() || !IsBlank(Rows() - 1)) rows_.push_back(std::vector<std::string>(columns_.size()));
}

void EntryGridModel::Changed()
{
    if (OnChanged) OnChanged();
}

} // namespace win32fx::model
