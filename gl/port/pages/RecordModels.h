//
// RecordModels.h - the list-and-detail pages' models (spec #8 5; plan S8-M2).
// Portable: no Win32. Each reads and writes its table through the core the
// classic dialog uses, so the rules are that dialog's:
//
//   page            table        core               the dialog it replaces
//   Departments     gldept       MasterTables.h     MasterData.cpp (1023)
//   Medical Reps    gl_medrp     MasterTables.h     MasterData.cpp (1006)
//   Doctors         gl_drcod     MasterTables.h     MasterData.cpp (1040)
//   Products        gl_itemm     MasterTables.h     MasterData.cpp (1007)
//   Vendors         ap_vend      Ap.h               ApVendors.cpp
//   Chart           glaccts      Accounts.h         ChartOfAccounts.cpp (1010)
//   Asset Types     gl_fatyp     Assets.h           FixedAssetTypes.cpp (4002)
//   Fixed Assets    gl_fa        Assets.h           FixedAssets.cpp (4004)
//   Users           access.psw   AccessModel below  Login.cpp UserAdministration (1159), AccessMenus (1160)
//
// Field keys are the descriptors' (gl/res/tools/make_pages.py); an account
// field `x` has its dept as `x_dept`.
//
#pragma once

#include "../MasterTables.h"
#include "RecordModel.h"

#include <memory>
#include <string>
#include <vector>

std::unique_ptr<RecordModel> MakeDepartmentsModel(cb4sqlite::Code4& code);
std::unique_ptr<RecordModel> MakeRepsModel(cb4sqlite::Code4& code, RepTable::Kind kind);
std::unique_ptr<RecordModel> MakeProductsModel(cb4sqlite::Code4& code);
std::unique_ptr<RecordModel> MakeVendorsModel(cb4sqlite::Code4& code);
std::unique_ptr<RecordModel> MakeAccountsModel(cb4sqlite::Code4& code);
std::unique_ptr<RecordModel> MakeAssetTypesModel(cb4sqlite::Code4& code);
std::unique_ptr<RecordModel> MakeAssetsModel(cb4sqlite::Code4& code);

// ---- User Access -----------------------------------------------------------
//
// The rights are per level: accessl's MENUCODE has one character per accessm
// row (in its "menu" order), '1' for a command grayed at that level. A user
// gets the level's code, or the first level's if theirs has none
// (Login.cpp LevelMenuCode). The page edits the level's code as check boxes,
// checked for allowed, and writes the same string: only the characters of
// the rows changed are written, so an unchanged code is saved byte for byte.

struct AccessRow {
    std::string popup, text;   // accessm POPUP, TEXT
    long id = 0;               // ID: the command
};

class AccessModel {
public:
    explicit AccessModel(cb4sqlite::Code4& code) : code_(code) {}

    void Load();                                            // accessm, in its "menu" order
    const std::vector<AccessRow>& rows() const noexcept { return rows_; }

    bool HasLevel(const std::string& level) const;
    // The code a user of `level` gets: the level's, else the first level's; "" without any.
    std::string CodeOf(const std::string& level) const;
    // Adds or replaces the level's MENUCODE.
    void SaveLevel(const std::string& level, const std::string& menuCode);

    // Row i allowed in `code` (not '1'; a row past the code's end is allowed).
    static bool Allowed(const std::string& code, size_t row);
    // `code` with row i set; characters past the end are '0' up to it.
    static std::string WithAllowed(std::string code, size_t row, bool allowed);

private:
    cb4sqlite::Code4& code_;
    std::vector<AccessRow> rows_;
};

// The Users page's model: access.psw's records, and the level's commands.
class UsersModel : public RecordModel {
public:
    explicit UsersModel(cb4sqlite::Code4& code);
    std::string Noun() const override { return "user"; }

    AccessModel& access() noexcept { return access_; }
    // The level the commands are shown for, and its code as edited.
    void ShowLevel(const std::string& level);
    const std::string& shownLevel() const noexcept { return level_; }
    const std::string& levelCode() const noexcept { return levelCode_; }
    bool levelChanged() const noexcept { return levelChanged_; }
    void SetAllowed(size_t row, bool allowed);

    void Set(const std::string& field, const std::string& value) override;

protected:
    std::vector<RecordRow> LoadRows() override;
    bool Read(const std::string& key, Values& out) override;
    std::string KeyOf(const Values& v) const override { return value(v, "username"); }
    std::string KeyField() const override { return "username"; }
    Result Write(Values& v, bool isNew) override;
    Result Remove(const std::string& key) override;

private:
    AccessModel access_;
    std::string level_, levelCode_;
    bool levelChanged_ = false;
};
