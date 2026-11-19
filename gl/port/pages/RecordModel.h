//
// RecordModel.h - a list-and-detail page's logic, without a window (spec #8
// 5; plan S8-M2). Portable: RecordPage shows one, and gl_tests drives each
// through its sheet's master criterion.
//
// A model holds the records as rows for the list (a key and the cells), and
// the record being edited as text values by field key, as the page's
// controls show them. Each kind of record (RecordModels.h) reads and writes
// its table through the cores the dialogs use (MasterTables.h, Accounts.h,
// Ap.h, Assets.h), so the rules are the dialogs' rules.
//
// What changes for every page (spec #8 D9): the model knows a new record
// from one on file, so Save never asks "has already been added. Do you want
// to modify the entry?"; a new record whose key is taken is refused
// instead, and a record's key can't change once saved.
//
#pragma once

#include "cb4sqlite.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct RecordRow {
    std::string key;                  // RecordModel::Select's argument
    std::vector<std::string> cells;   // [0] the code, then the list's other columns; cells past them are filtered on, not shown
};

class RecordModel {
public:
    struct Result {
        bool ok = false;
        std::string field;     // the field to mark, by key ("" for none)
        std::string message;
    };

    explicit RecordModel(cb4sqlite::Code4& code) : code_(code) {}
    virtual ~RecordModel() = default;
    RecordModel(const RecordModel&) = delete;
    RecordModel& operator=(const RecordModel&) = delete;

    // "department": for the messages ("Department 10 saved.").
    virtual std::string Noun() const = 0;

    // ---- the list ----
    void Load() { rows_ = LoadRows(); }
    const std::vector<RecordRow>& rows() const noexcept { return rows_; }
    // The rows matching a filter (the code, then the name; fx::model::LookupModel), best first.
    std::vector<size_t> Filter(const std::string& text) const;
    int RowOf(const std::string& key) const;   // -1 if none

    // ---- the record ----
    void New();
    bool Select(const std::string& key);       // false if it isn't on file
    bool isNew() const noexcept { return isNew_; }
    const std::string& key() const noexcept { return key_; }

    virtual std::string Get(const std::string& field) const;
    virtual void Set(const std::string& field, const std::string& value) { values_[field] = value; }
    // A choice field's items: (value, what the list shows).
    virtual std::vector<std::pair<std::string, std::string>> Choices(const std::string& /*field*/) const { return {}; }

    // On success the record is on file, the list reloaded, and the record
    // stays selected (shown as saved).
    Result Save();
    // After the page has asked: deleted (and a new record started), or why not.
    Result Delete();

protected:
    using Values = std::map<std::string, std::string>;

    virtual std::vector<RecordRow> LoadRows() = 0;
    virtual Values Defaults() { return {}; }
    virtual bool Read(const std::string& key, Values& out) = 0;
    // The key a record's values make (the fields flagged Key / KeyPart).
    virtual std::string KeyOf(const Values& v) const = 0;
    // The key field to mark when a new record's key is taken.
    virtual std::string KeyField() const = 0;
    // Checks and writes the record. `isNew`: append, else replace `key`.
    virtual Result Write(Values& v, bool isNew) = 0;
    virtual Result Remove(const std::string& key) = 0;

    std::string value(const Values& v, const std::string& field) const;
    static Result Refuse(const std::string& field, const std::string& message) { return {false, field, message}; }

    cb4sqlite::Code4& code_;
    Values values_;

private:
    std::vector<RecordRow> rows_;
    std::string key_;
    bool isNew_ = true;
};

// Field checks the models share (the messages the page shows by the field).
namespace record {
bool IsWholeNumber(const std::string& text, int lo, int hi);
int ToInt(const std::string& text);                  // "" or junk -> 0
std::string Cents(std::int64_t cents);               // "1,250.50"; "" for 0
std::int64_t ToCents(const std::string& text);       // "" or junk -> 0
}
