//
// RecordPages.cpp - see RecordPage.h.
//
// Translation notes (decisions are spec #8 13):
//   - D4, 5.1: the list replaces typing a code and pressing Enter, and Top,
//     Next, Previous, Find and List.
//   - D9: Save doesn't ask "already added, modify?" (RecordModel.h); a
//     record's key can't be changed once saved.
//   - 6.3: accounts are lookups over the chart, account and dept, with the
//     account's name beside them.
//   - 7: a problem marks its field and shows in the information bar; Delete
//     asks in a task dialog with named buttons.
//   - User Access (Login.cpp's 1159 and 1160) is one page: the users, and the
//     selected user's level's commands as check boxes grouped by menu.
//     Checked is allowed. The level's MENUCODE is written as before, a
//     character per accessm row (RecordModels.h AccessModel). Reset (8152,
//     the session takes the level's code at once) isn't on the page: the
//     rights are read at the next login, as for every other change here.
//   - Fixed Assets: Dispose... opens Fixed Assets > Disposal (6035) for the
//     asset; the list then shows it disposed.
//   - Tabs (spec #9 S9-M0): a page's tabs sit below its groups, a strip with
//     no body; choosing one shows its controls and hides the others'. A grid
//     tab is an fx::EntryGrid over the model's Grid(key). Save commits the
//     grid's edit first, and a field refused on a hidden tab brings its tab
//     forward. The tab check page (IDD_PG_TABCHECK) has a tab of fields and a
//     grid tab over an in-memory record, for /checkres and /stress; no
//     command opens it.
//   - Budget Data (spec #9 S9-M1, D1) is the Chart of Accounts page's Budget
//     tab (RecordModels.cpp, AccountsModel); 10074 opens the page on it
//     (ShowRecordTab).
//   - Company Profile (spec #9 S9-M2, D3) is a page with no list (`nolist`):
//     the one record, "company", is shown from the start, and only Save is
//     on it. Its tabs are pages/CompanyModel.h's. A grid's account column (a
//     Lookup cell) lists the chart as the account fields do; picking one fills
//     the row's dept. "Reset depreciation flag..." asks 6025 and blanks
//     gl_fa.mode. A save tells the frame (data_group::kCompany), which shows
//     the company and period again, as 6015's dialog did with RefreshStatus.
//
#include "RecordPage.h"

#include "../Accounts.h"   // TrimText
#include "../DataChanged.h"
#include "../FixedAssetDisposal.h"
#include "../Globals.h"
#include "../Ini.h"
#include "../PortIds.h"
#include "CompanyModel.h"
#include "RecordModels.h"
#include "TaskModels.h"   // data_group

#include <CommCtrl.h>
#include <Uxtheme.h>

#include <algorithm>
#include <functional>
#include <map>
#include <string>

namespace {

namespace m = fx::model;

#include "PageDescs.inc"

std::wstring wide(const std::string& s) { return fx::Utf8ToWide(s); }

// The command each page replaces, its descriptor and model, and its classic dialog.
struct PageEntry {
    WORD command;
    const PageDesc* desc;
    std::function<std::unique_ptr<RecordModel>()> make;
    WORD classic;
    const wchar_t* classicText;
    unsigned groups = data_group::kMasters;   // what a save or delete changes (spec #8 9.3)
};

// Company Profile's model, with GL.INI's withholding setting (1021's
// GetWithHolding / SetWithHolding: [WITHHOLDING], string 8150) and the
// company database's path.
std::unique_ptr<RecordModel> makeCompanyModel()
{
    auto m = std::make_unique<CompanyModel>(cb);
    m->dataPath = fx::WideToUtf8(CurDir);
    m->readWithholding = [] {
        const std::wstring section = fx::LoadResString(hInst, 8150);
        return GetIniString(L"GL.INI", section.c_str(), L"TAX", L"T") == L"T" ? std::string("T") : std::string("F");
    };
    m->writeWithholding = [](const std::string& v) {
        const std::wstring section = fx::LoadResString(hInst, 8150);
        WriteIniString(L"GL.INI", section.c_str(), L"TAX", v == "T" ? L"T" : L"F");
    };
    return m;
}

const std::vector<PageEntry>& entries()
{
    static const std::vector<PageEntry> all = {
        {1004, &k_accounts, [] { return MakeAccountsModel(cb); }, CM_ACCOUNTS_CLASSIC, L"Chart of Accounts (classic)"},
        {10061, &k_departments, [] { return MakeDepartmentsModel(cb); }, CM_DEPARTMENTS_CLASSIC,
         L"Department Codes (classic)"},
        {4045, &k_vendors, [] { return MakeVendorsModel(cb); }, CM_VENDORS_CLASSIC, L"Purchase Vendors (classic)"},
        {6042, &k_medicalReps, [] { return MakeRepsModel(cb, RepTable::Kind::MedicalRep); }, CM_MEDREPS_CLASSIC,
         L"Medical Reps (classic)"},
        {6005, &k_products, [] { return MakeProductsModel(cb); }, CM_PRODUCTS_CLASSIC, L"Product Codes (classic)"},
        {6040, &k_doctors, [] { return MakeRepsModel(cb, RepTable::Kind::Doctor); }, CM_DOCTORS_CLASSIC,
         L"Doctor Codes (classic)"},
        {6000, &k_assets, [] { return MakeAssetsModel(cb); }, CM_ASSETS_CLASSIC, L"Add/Modify (classic)"},
        {6036, &k_assetTypes, [] { return MakeAssetTypesModel(cb); }, CM_ASSETTYPES_CLASSIC, L"Asset Types (classic)"},
        {4078, &k_users, [] { return std::make_unique<UsersModel>(cb); }, CM_USERS_CLASSIC, L"User Access (classic)"},
        {6015, &k_company, makeCompanyModel, CM_PROFILE_CLASSIC, L"Company Profile (classic)",
         data_group::kMasters | data_group::kCompany},   // spec #9 S9-M2
    };
    return all;
}

const PageEntry* entryOf(WORD command)
{
    for (const PageEntry& e : entries())
        if (e.command == command) return &e;
    return nullptr;
}

std::map<WORD, HWND> gOpen;   // command -> its open page

void addColumns(HWND list, const std::vector<ColumnDesc>& cols, int dpi)
{
    // ui-ux-pro-max truncation-strategy (design review): no column narrower than its heading
    // ("Acco..." and "D..." were), and the last column (the description or name) takes the
    // list's remaining width rather than leaving an empty strip while its text is cut off.
    const int pad = MulDiv(16, dpi, 96);
    int i = 0;
    for (const ColumnDesc& col : cols) {
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        c.fmt = col.right ? LVCFMT_RIGHT : LVCFMT_LEFT;
        c.cx = std::max(MulDiv(col.width, dpi, 96), ListView_GetStringWidth(list, col.heading) + pad);
        c.pszText = const_cast<wchar_t*>(col.heading);
        ListView_InsertColumn(list, i++, &c);
    }
    if (i > 0) ListView_SetColumnWidth(list, i - 1, LVSCW_AUTOSIZE_USEHEADER);   // the last one fills the list
}

bool isEditable(const FieldDesc& f)
{
    return f.kind != FieldKind::ReadOnly;
}

bool isInput(const FieldDesc* f)
{
    return f && f->flag == FieldFlag::Input;
}

bool isKey(const FieldDesc& f)
{
    return f.flag == FieldFlag::Key || f.flag == FieldFlag::KeyPart;
}

// "1,234.567": up to three decimals (Products' cost and unit price).
m::Check decimalCheck()
{
    return [](const std::string& text) -> std::optional<std::string> {
        const std::string t = m::Trim(text);
        size_t decimals = 0;
        bool point = false;
        for (size_t i = 0; i < t.size(); ++i) {
            const char c = t[i];
            if (c >= '0' && c <= '9') {
                if (point && ++decimals > 3) return "A number, with up to three decimals.";
            } else if (c == '.' && !point) {
                point = true;
            } else if (!(c == ',' && !point) && !(c == '-' && i == 0)) {
                return "A number, with up to three decimals.";
            }
        }
        return std::nullopt;
    };
}

class RecordPage : public fx::Page {
public:
    RecordPage(const PageEntry& e)
        : fx::Page(e.desc->form), d_(*e.desc), command_(e.command), groups_(e.groups), model_(e.make())
    {
        commandId = e.command;   // so the tab can be pinned
    }

    bool Save() override;

    // /stress
    void Filter(const std::string& text) { SetText(kPageFilter, text); }
    // Command search: the record with `key`, asking about unsaved changes first.
    bool ShowKey(const std::string& key)
    {
        if (!model_->isNew() && model_->key() == key) return true;
        if (!SwitchAllowed()) return false;
        HideInfo();
        SetText(kPageFilter, "");
        if (!model_->Select(key)) {
            ShowInfo(fx::InfoKind::Warning, wide(key) + L" is no longer on file.");
            return false;
        }
        Fill();
        ShowList();
        return true;
    }
    void SelectShown(int row) { SelectRow(row); }
    int shownCount() const { return static_cast<int>(shown_.size()); }
    // /stress: type into the field `key`; true if the page stays clean (an input, spec #10 D5).
    bool TypeInField(const char* key, const std::string& text)
    {
        const FieldDesc* f = fieldOfKey(key);
        if (!f) return false;
        SetText(f->id, text);
        return !IsDirty();
    }
    // /stress: choose a tab; type into the current cell of its grid, then Tab.
    void ChooseTab(int tab) { ShowTab(tab); }
    int tabCount() const { return static_cast<int>(d_.tabs.size()); }
    bool ChooseTab(const std::wstring& caption)
    {
        for (size_t i = 0; i < d_.tabs.size(); ++i)
            if (caption == d_.tabs[i].caption) {
                ShowTab(static_cast<int>(i));
                return true;
            }
        return false;
    }
    void TypeInGrid(int tab, const std::string& text)
    {
        if (tab < 0 || static_cast<size_t>(tab) >= d_.tabs.size()) return;
        const auto it = grids_.find(d_.tabs[static_cast<size_t>(tab)].grid);
        if (it == grids_.end()) return;
        it->second->Type(text);
        it->second->Press(m::GridKey::Tab);
    }

protected:
    bool OnInitPage() override;
    bool OnFormCommand(WORD id, WORD code, HWND ctl) override;
    bool OnFormNotify(NMHDR* nm, LRESULT& result) override;
    void OnFieldChanged(int id) override;
    void OnNew() override;
    bool OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override;

private:
    const FieldDesc* fieldOf(int id) const;
    const FieldDesc* fieldOfKey(const std::string& key) const;
    std::wstring Named() const;   // "department 10", "the new department"
    void LoadAccounts();
    void ShowList();
    void Fill();
    void FillChoice(const FieldDesc& f);
    void FillCommands();
    void Read();
    void RefreshDerived();
    void Changed(int id);   // a field changed: dirty (unless it's an input), its mark cleared, derived fields shown
public:
    // A command's inputs (spec #10 D5) on the tab `tab` (-1: all) pass their checks; if not, they're marked.
    bool InputsValid(int tab);
private:
    void ShowAccountName(const FieldDesc& f);
    bool SwitchAllowed();
    void SelectRow(int row);
    void Delete();
    void OnButton(int id);
    fx::Lookup* lookupOf(int id);
    void ResetDepreciation();
    UsersModel* users() { return d_.commands ? static_cast<UsersModel*>(model_.get()) : nullptr; }
    void ShowTab(int tab);
    int TabOf(int id) const;   // -1: not on a tab
    bool CommitGrids();

    const PageDesc& d_;
    WORD command_;
    unsigned groups_;
    std::unique_ptr<RecordModel> model_;
    m::LookupModel accounts_;
    std::map<int, std::unique_ptr<fx::Lookup>> lookups_;            // account edit -> its lookup
    std::map<int, std::vector<std::string>> choices_;                // combo -> the values, by index
    std::vector<size_t> shown_;                                      // list row -> model row
    HWND list_ = nullptr, commands_ = nullptr, tabs_ = nullptr;
    std::map<int, std::unique_ptr<fx::EntryGrid>> grids_;           // a tab's grid ListView -> its grid
    bool gridLookups_ = false;                                       // a grid has an account column
    bool selecting_ = false;
};

const FieldDesc* RecordPage::fieldOf(int id) const
{
    for (const FieldDesc& f : d_.fields)
        if (f.id == id) return &f;
    return nullptr;
}

const FieldDesc* RecordPage::fieldOfKey(const std::string& key) const
{
    for (const FieldDesc& f : d_.fields)
        if (key == f.key) return &f;
    return nullptr;
}

std::wstring RecordPage::Named() const
{
    return model_->isNew() ? L"the new " + wide(model_->Noun()) : wide(model_->Noun() + " " + model_->key());
}

bool RecordPage::OnInitPage()
{
    SetAnchors(d_.anchors);
    list_ = Item(kPageList);
    if (list_) fx::SetAccessibleName(list_, std::wstring(d_.title) + L" list");   // spec #8 12
    const int dpi = static_cast<int>(GetDpiForWindow(form()));
    SetWindowTheme(list_, L"Explorer", nullptr);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    addColumns(list_, d_.columns, dpi);
    SendDlgItemMessageW(form(), kPageFilter, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Filter by code or name"));
    bool anyAccount = false;
    for (const FieldDesc& f : d_.fields) {
        m::Validator v;
        if (f.flag == FieldFlag::Key || f.flag == FieldFlag::Required) v.Add(m::Required());
        switch (f.kind) {
        case FieldKind::Text:
        case FieldKind::Upper:
        case FieldKind::Password:
            SendDlgItemMessageW(form(), f.id, EM_LIMITTEXT, static_cast<WPARAM>(f.width), 0);
            v.Add(m::MaxLength(static_cast<size_t>(f.width)));
            break;
        case FieldKind::Number: {
            long long hi = 1;
            for (int i = 0; i < f.width; ++i) hi *= 10;
            SendDlgItemMessageW(form(), f.id, EM_LIMITTEXT, static_cast<WPARAM>(f.width), 0);
            v.Add(m::WholeNumber(0, hi - 1));
            break;
        }
        case FieldKind::Decimal: v.Add(decimalCheck()); break;
        case FieldKind::Amount: v.Add(m::Amount()); break;
        case FieldKind::Account:
            anyAccount = true;
            SendDlgItemMessageW(form(), f.id, EM_LIMITTEXT, 7, 0);
            SendDlgItemMessageW(form(), f.dept, EM_LIMITTEXT, 4, 0);
            v.Add(m::Exists(
                [this](const std::string& s) {
                    for (const m::LookupItem& it : accounts_.items())
                        if (it.code == s) return true;
                    return false;
                },
                "Not an account in the chart."));
            break;
        default: break;
        }
        if (!v.empty()) SetValidator(f.id, std::move(v));
    }
    if (anyAccount) {
        LoadAccounts();
        for (const FieldDesc& f : d_.fields) {
            if (f.kind != FieldKind::Account) continue;
            auto l = std::make_unique<fx::Lookup>();
            const FieldDesc* fp = &f;
            l->Attach(Item(f.id), &accounts_, [this, fp](const m::LookupItem& it) {
                {
                    Filling filling(*this);
                    SetText(fp->dept, it.sub);
                    SetText(fp->name, it.name);
                }
                Changed(fp->id);
            });
            lookups_[f.id] = std::move(l);
        }
    }
    if (d_.commands) {
        commands_ = Item(kPageCommands);
        SetWindowTheme(commands_, L"Explorer", nullptr);
        ListView_SetExtendedListViewStyle(commands_, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);
        RECT rc{};
        GetClientRect(commands_, &rc);
        LVCOLUMNW c{};
        c.mask = LVCF_WIDTH;
        c.cx = rc.right - GetSystemMetricsForDpi(SM_CXVSCROLL, static_cast<UINT>(dpi));
        ListView_InsertColumn(commands_, 0, &c);
        ListView_EnableGroupView(commands_, TRUE);
    }
    if (!d_.tabs.empty()) {
        tabs_ = Item(kPageTabs);
        for (size_t i = 0; i < d_.tabs.size(); ++i) {
            TCITEMW t{};
            t.mask = TCIF_TEXT;
            t.pszText = const_cast<wchar_t*>(d_.tabs[i].caption);
            TabCtrl_InsertItem(tabs_, static_cast<int>(i), &t);
            if (!d_.tabs[i].grid) continue;
            m::EntryGridModel* g = model_->Grid(d_.tabs[i].gridKey);
            if (!g) continue;
            auto grid = std::make_unique<fx::EntryGrid>();
            grid->Attach(Item(d_.tabs[i].grid), g);
            // An account cell (a Lookup column) lists the chart; picking one fills the row's dept.
            const int deptCol = g->ColumnOf("dept");
            for (int c = 0; c < static_cast<int>(g->columns().size()); ++c)
                if (g->columns()[static_cast<size_t>(c)].kind == m::CellKind::Lookup) {
                    grid->SetLookup(c, &accounts_);
                    gridLookups_ = true;
                }
            grid->OnPick = [g, deptCol](int row, int, const m::LookupItem& item) {
                if (deptCol >= 0) g->SetCell(row, deptCol, item.sub);
            };
            fx::SetAccessibleName(Item(d_.tabs[i].grid), std::wstring(d_.tabs[i].caption) + L" grid");
            fx::EntryGrid* gp = grid.get();
            g->OnChanged = [this, gp, g] {
                if (filling()) return;
                if (!g->readOnly()) SetDirty(true);   // a read-only grid (spec #10 D2) is only shown
                gp->Refresh();
            };
            grid->OnAction = [this, gp](m::GridAction a) {
                if (a == m::GridAction::Refused) ShowInfo(fx::InfoKind::Error, wide(gp->model()->message()));
                else if (a == m::GridAction::Committed) HideInfo();
            };
            grids_[d_.tabs[i].grid] = std::move(grid);
        }
        ShowTab(0);
    }
    if (gridLookups_ && !anyAccount) LoadAccounts();
    model_->Load();
    model_->New();
    if (d_.nolist && !model_->rows().empty()) model_->Select(model_->rows().front().key);   // the one record
    ShowList();
    Fill();
    return true;
}

void RecordPage::LoadAccounts()
{
    std::vector<m::LookupItem> items;
    cb4sqlite::Query q(cb, "SELECT trim(account), coalesce(trim(dept), ''), trim(descript) FROM glaccts "
                           "WHERE _deleted = 0 ORDER BY 1, 2");
    while (q.next()) items.push_back({q.text(0), q.text(1), q.text(2)});
    accounts_.SetItems(std::move(items));
}

fx::Lookup* RecordPage::lookupOf(int id)
{
    const auto it = lookups_.find(id);
    return it == lookups_.end() ? nullptr : it->second.get();
}

void RecordPage::ShowList()
{
    shown_ = model_->Filter(Text(kPageFilter));
    selecting_ = true;
    ListView_SetItemCountEx(list_, static_cast<int>(shown_.size()), LVSICF_NOSCROLL);
    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (!model_->isNew()) {
        const int row = model_->RowOf(model_->key());
        for (size_t i = 0; i < shown_.size(); ++i)
            if (static_cast<int>(shown_[i]) == row) {
                ListView_SetItemState(list_, static_cast<int>(i), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(list_, static_cast<int>(i), FALSE);
            }
    }
    InvalidateRect(list_, nullptr, FALSE);
    selecting_ = false;
}

void RecordPage::FillChoice(const FieldDesc& f)
{
    HWND combo = Item(f.id);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    std::vector<std::string>& values = choices_[f.id];
    values.clear();
    const std::string current = model_->Get(f.key);
    int selected = -1;
    for (const auto& [value, text] : model_->Choices(f.key)) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(text).c_str()));
        if (value == current) selected = static_cast<int>(values.size());
        values.push_back(value);
    }
    if (selected < 0 && !current.empty()) {   // a value on file that isn't a choice: shown as it is
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(current).c_str()));
        selected = static_cast<int>(values.size());
        values.push_back(current);
    }
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
}

void RecordPage::Fill()
{
    {
        Filling filling(*this);
        for (const FieldDesc& f : d_.fields) {
            const std::string value = model_->Get(f.key);
            switch (f.kind) {
            case FieldKind::Choice: FillChoice(f); break;
            case FieldKind::Date: fx::SetDateField(Item(f.id), value); break;
            case FieldKind::Account:
                SetText(f.id, value);
                SetText(f.dept, model_->Get(std::string(f.key) + "_dept"));
                ShowAccountName(f);
                break;
            default: SetText(f.id, value); break;
            }
            if (isKey(f)) {
                // A key on file can't be changed (D9).
                if (f.kind == FieldKind::Choice || f.kind == FieldKind::Date) EnableWindow(Item(f.id), model_->isNew());
                else SendDlgItemMessageW(form(), f.id, EM_SETREADONLY, model_->isNew() ? FALSE : TRUE, 0);
            }
        }
    }
    if (users()) {
        users()->ShowLevel(model_->Get("level"));
        FillCommands();
    }
    for (auto& [id, grid] : grids_) grid->Refresh();
    EnableWindow(Item(IDC_PG_DELETE), !model_->isNew());
    for (const ButtonDesc& b : d_.buttons) EnableWindow(Item(b.id), !model_->isNew());
    ClearMarks();
    SetDirty(false);
    SetPageTitle(model_->isNew() || d_.nolist ? std::wstring(d_.title) : std::wstring(d_.title) + L" - " + wide(model_->key()));
}

void RecordPage::FillCommands()
{
    UsersModel* u = users();
    if (!u || !commands_) return;
    Filling filling(*this);
    SendMessageW(commands_, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(commands_);
    ListView_RemoveAllGroups(commands_);
    std::map<std::string, int> groups;
    const std::vector<AccessRow>& rows = u->access().rows();
    for (size_t i = 0; i < rows.size(); ++i) {
        const AccessRow& r = rows[i];
        auto g = groups.find(r.popup);
        if (g == groups.end()) {
            const int id = static_cast<int>(groups.size()) + 1;
            std::wstring header = wide(r.popup);
            LVGROUP lg{};
            lg.cbSize = sizeof lg;
            lg.mask = LVGF_HEADER | LVGF_GROUPID;
            lg.pszHeader = header.data();
            lg.iGroupId = id;
            ListView_InsertGroup(commands_, -1, &lg);
            g = groups.emplace(r.popup, id).first;
        }
        std::wstring text = wide(r.text);
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_GROUPID | LVIF_PARAM;
        it.iItem = static_cast<int>(i);
        it.pszText = text.data();
        it.iGroupId = g->second;
        it.lParam = static_cast<LPARAM>(i);
        const int at = ListView_InsertItem(commands_, &it);
        ListView_SetCheckState(commands_, at, AccessModel::Allowed(u->levelCode(), i) ? TRUE : FALSE);
    }
    SendMessageW(commands_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(commands_, nullptr, TRUE);
    const std::wstring caption = u->shownLevel().empty() ? std::wstring(L"The level's commands")
                                                         : L"Level " + wide(u->shownLevel()) + L"'s commands";
    for (HWND c = GetWindow(form(), GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        wchar_t cls[16] = {};
        GetClassNameW(c, cls, 16);
        if (_wcsicmp(cls, L"Button") == 0 && (GetWindowLongW(c, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX &&
            GetWindow(c, GW_HWNDNEXT) == commands_)
            SetWindowTextW(c, caption.c_str());
    }
}

void RecordPage::ShowAccountName(const FieldDesc& f)
{
    const int i = accounts_.Exact(m::LookupItem{m::Trim(Text(f.id)), m::Trim(Text(f.dept)), ""}.key());
    Filling filling(*this);
    SetText(f.name, i >= 0 ? accounts_.items()[static_cast<size_t>(i)].name : std::string());
}

void RecordPage::Read()
{
    for (const FieldDesc& f : d_.fields) {
        if (!isEditable(f)) continue;
        if (isKey(f) && !model_->isNew()) continue;   // the key stays
        switch (f.kind) {
        case FieldKind::Choice: {
            const int sel = static_cast<int>(SendMessageW(Item(f.id), CB_GETCURSEL, 0, 0));
            const std::vector<std::string>& values = choices_[f.id];
            model_->Set(f.key, sel >= 0 && static_cast<size_t>(sel) < values.size() ? values[static_cast<size_t>(sel)]
                                                                                    : std::string());
            break;
        }
        case FieldKind::Date: model_->Set(f.key, fx::GetDateField(Item(f.id))); break;
        case FieldKind::Account:
            model_->Set(f.key, m::Trim(Text(f.id)));
            model_->Set(std::string(f.key) + "_dept", m::Trim(Text(f.dept)));
            break;
        default: model_->Set(f.key, Text(f.id)); break;
        }
    }
}

void RecordPage::RefreshDerived()
{
    Filling filling(*this);
    for (const FieldDesc& f : d_.fields)
        if (f.kind == FieldKind::ReadOnly) SetText(f.id, model_->Get(f.key));
}

void RecordPage::Changed(int id)
{
    if (isInput(fieldOf(id))) ClearMark(id);
    else fx::Page::OnFieldChanged(id);   // dirty, the mark cleared
    Read();
    RefreshDerived();
}

void RecordPage::OnFieldChanged(int id)
{
    Changed(id);
}

bool RecordPage::InputsValid(int tab)
{
    bool ok = true;
    int firstBad = 0;
    for (const FieldDesc& f : d_.fields)
        if (isInput(&f) && (tab < 0 || TabOf(f.id) == tab) && f.kind != FieldKind::Choice &&
            f.kind != FieldKind::Date && !Validate(f.id)) {
            if (ok) firstBad = f.id;
            ok = false;
        }
    if (!ok) {
        if (TabOf(firstBad) >= 0) ShowTab(TabOf(firstBad));
        ShowInfo(fx::InfoKind::Error, L"Check the marked fields: hover over one to see what's wrong.");
    }
    return ok;
}

bool RecordPage::Save()
{
    if (SaveWaitsForJob(*this)) return false;
    if (!CommitGrids()) return false;
    Read();
    bool ok = true;
    int firstBad = 0;
    for (const FieldDesc& f : d_.fields)
        if (isEditable(f) && !isInput(&f) && f.kind != FieldKind::Choice && f.kind != FieldKind::Date &&
            !Validate(f.id)) {
            if (ok) firstBad = f.id;
            ok = false;
        }
    if (!ok) {
        if (TabOf(firstBad) >= 0) ShowTab(TabOf(firstBad));
        ShowInfo(fx::InfoKind::Error, L"Check the marked fields: hover over one to see what's wrong.");
        return false;
    }
    const RecordModel::Result r = model_->Save();
    if (!r.ok) {
        if (const FieldDesc* f = fieldOfKey(r.field)) {
            if (TabOf(f->id) >= 0) ShowTab(TabOf(f->id));
            MarkField(f->id, r.message, true);
        }
        ShowInfo(fx::InfoKind::Error, wide(r.message));
        return false;
    }
    Fill();
    ShowList();
    ShowInfo(fx::InfoKind::Success, wide(r.message));
    NotifyDataChanged(groups_, hwnd());   // spec #8 9.3
    return true;
}

void RecordPage::ShowTab(int tab)
{
    if (!tabs_ || tab < 0 || static_cast<size_t>(tab) >= d_.tabs.size()) return;
    TabCtrl_SetCurSel(tabs_, tab);
    for (size_t i = 0; i < d_.tabs.size(); ++i)
        if (static_cast<int>(i) != tab)
            for (int id : d_.tabs[i].ids) ShowWindow(Item(id), SW_HIDE);
    for (int id : d_.tabs[static_cast<size_t>(tab)].ids) ShowWindow(Item(id), SW_SHOW);
}

int RecordPage::TabOf(int id) const
{
    for (size_t i = 0; i < d_.tabs.size(); ++i)
        if (std::find(d_.tabs[i].ids.begin(), d_.tabs[i].ids.end(), id) != d_.tabs[i].ids.end())
            return static_cast<int>(i);
    return -1;
}

// Ends an edit in a tab's grid before saving: false (its tab shown, and why) if the cell's text is refused.
bool RecordPage::CommitGrids()
{
    for (auto& [id, grid] : grids_) {
        m::EntryGridModel* g = grid->model();
        if (!g || !g->editing()) continue;
        if (grid->editing()) grid->Press(m::GridKey::Tab);
        else g->Commit();
        if (g->editing()) {
            if (TabOf(id) >= 0) ShowTab(TabOf(id));
            ShowInfo(fx::InfoKind::Error, wide(g->message()));
            return false;
        }
    }
    return true;
}

bool RecordPage::SwitchAllowed()
{
    if (!IsDirty()) return true;
    enum { kSave = 101, kDiscard = 102 };
    const int chosen = fx::Ask(host() ? host()->hostWindow() : hwnd(), szApp, L"Save the changes to " + Named() + L"?", L"",
                               {{kSave, L"Save"}, {kDiscard, L"Don't save"}, {IDCANCEL, L"Cancel"}}, kSave, IDCANCEL);
    if (chosen == kSave) return Save();
    return chosen == kDiscard;
}

void RecordPage::SelectRow(int row)
{
    if (row < 0 || static_cast<size_t>(row) >= shown_.size()) return;
    const RecordRow& r = model_->rows()[shown_[static_cast<size_t>(row)]];
    if (!model_->isNew() && model_->key() == r.key) return;
    if (!SwitchAllowed()) {
        ShowList();   // back to the record being edited
        return;
    }
    HideInfo();
    const std::string key = r.key;
    if (!model_->Select(key)) {
        ShowInfo(fx::InfoKind::Warning, wide(key) + L" is no longer on file.");
        model_->Load();
        ShowList();
        return;
    }
    Fill();
}

void RecordPage::OnNew()
{
    if (d_.nolist) return;   // one record: nothing to start
    if (!SwitchAllowed()) return;
    HideInfo();
    model_->New();
    Fill();
    ShowList();
    if (!d_.fields.empty()) SendMessageW(form(), WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(Item(d_.fields[0].id)), TRUE);
}

void RecordPage::Delete()
{
    if (model_->isNew()) return;
    const std::wstring what = wide(model_->Noun() + " " + model_->key());
    enum { kDelete = 101 };
    if (fx::Ask(host() ? host()->hostWindow() : hwnd(), szApp, L"Delete " + what + L"?", L"It can't be undone.",
                {{kDelete, L"Delete " + wide(model_->Noun())}, {IDCANCEL, L"Keep it"}}, IDCANCEL, IDCANCEL) != kDelete)
        return;
    const RecordModel::Result r = model_->Delete();
    if (!r.ok) {
        ShowInfo(fx::InfoKind::Error, wide(r.message));
        return;
    }
    Fill();
    ShowList();
    ShowInfo(fx::InfoKind::Success, wide(r.message));
    NotifyDataChanged(data_group::kMasters, hwnd());
}

// 1028's "Reset the depreciation journal flag" (Company Profile, spec #9 S9-M2).
void RecordPage::ResetDepreciation()
{
    auto* company = dynamic_cast<CompanyModel*>(model_.get());
    if (!company || SaveWaitsForJob(*this)) return;
    enum { kReset = 101 };
    const std::wstring question = fx::LoadResString(hInst, 6025);   // "Reset the depreciation journal flag."
    if (fx::Ask(host() ? host()->hostWindow() : hwnd(), szApp, L"Reset the depreciation journal flag?",
                question.empty() ? std::wstring(L"Every fixed asset's depreciation mark is cleared.") : question,
                {{kReset, L"Reset the flag"}, {IDCANCEL, L"Keep it"}}, IDCANCEL, IDCANCEL) != kReset)
        return;
    const int n = company->ResetDepreciationFlag();
    ShowInfo(fx::InfoKind::Success, fx::Format(L"The depreciation journal flag is reset on %d fixed asset(s).", n));
    NotifyDataChanged(data_group::kAssets, hwnd());
}

void RecordPage::OnButton(int id)
{
    if (command_ == 6015 && !d_.buttons.empty() && id == d_.buttons.front().id) {
        ResetDepreciation();
        return;
    }
    if (command_ == 6000 && id == d_.buttons.front().id && !model_->isNew()) {   // Fixed Assets: Dispose...
        if (!SwitchAllowed()) return;
        const std::string key = model_->key();
        RunAssetDisposal(host() ? host()->hostWindow() : hwnd());   // Fixed Assets > Disposal (6035)
        model_->Load();
        if (!model_->Select(key)) model_->New();
        Fill();
        ShowList();
    }
}

bool RecordPage::OnFormCommand(WORD id, WORD code, HWND)
{
    if (code == BN_CLICKED) {
        switch (id) {
        case IDC_PG_NEW: OnNew(); return true;
        case IDC_PG_SAVE: Save(); return true;
        case IDC_PG_DELETE: Delete(); return true;
        default:
            for (const ButtonDesc& b : d_.buttons)
                if (b.id == id) {
                    OnButton(id);
                    return true;
                }
            break;
        }
    }
    if (id == kPageFilter) {
        if (code == EN_CHANGE) ShowList();
        return code == EN_CHANGE || code == EN_KILLFOCUS;   // the filter isn't the record: not dirty
    }
    if (fx::Lookup* l = lookupOf(id)) {
        if (code == EN_CHANGE && !filling()) l->TextChanged();
        if (code == EN_KILLFOCUS) {
            l->FocusLost();
            if (const FieldDesc* f = fieldOf(id)) ShowAccountName(*f);
        }
        return false;   // and the page's own handling: dirty, the validator
    }
    if (code == EN_CHANGE && !filling())
        for (const FieldDesc& f : d_.fields)
            if (f.kind == FieldKind::Account && f.dept == id) ShowAccountName(f);
    if (code == CBN_SELCHANGE && !filling()) {
        Changed(id);
        return true;
    }
    if (code == EN_KILLFOCUS && users()) {
        const FieldDesc* f = fieldOf(id);
        if (f && std::string(f->key) == "level") {
            Read();   // a new level shows its commands (unless this one's were changed)
            FillCommands();
        }
    }
    return false;
}

bool RecordPage::OnFormNotify(NMHDR* nm, LRESULT& result)
{
    if (tabs_ && nm->hwndFrom == tabs_) {
        if (nm->code == TCN_SELCHANGE) ShowTab(TabCtrl_GetCurSel(tabs_));
        result = 0;
        return true;
    }
    for (auto& [id, grid] : grids_)
        if (grid->OnNotify(nm, result)) return true;
    if (nm->code == DTN_DATETIMECHANGE && !filling()) {
        Changed(static_cast<int>(nm->idFrom));
        return false;
    }
    if (commands_ && nm->hwndFrom == commands_ && nm->code == LVN_ITEMCHANGED) {
        const auto* lv = reinterpret_cast<NMLISTVIEW*>(nm);
        if (!filling() && (lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_STATEIMAGEMASK) &&
            (lv->uOldState & LVIS_STATEIMAGEMASK)) {
            const bool allowed = ((lv->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2;
            users()->SetAllowed(static_cast<size_t>(lv->lParam), allowed);
            SetDirty(true);
        }
        result = 0;
        return true;
    }
    if (nm->hwndFrom != list_) return false;
    switch (nm->code) {
    case LVN_GETDISPINFOW: {
        auto* di = reinterpret_cast<NMLVDISPINFOW*>(nm);
        const int row = di->item.iItem;
        if (!(di->item.mask & LVIF_TEXT) || row < 0 || static_cast<size_t>(row) >= shown_.size()) return true;
        const RecordRow& r = model_->rows()[shown_[static_cast<size_t>(row)]];
        const size_t col = static_cast<size_t>(di->item.iSubItem);
        const std::string text = col < r.cells.size() ? r.cells[col] : std::string();
        wcsncpy_s(di->item.pszText, static_cast<size_t>(di->item.cchTextMax), wide(text).c_str(), _TRUNCATE);
        result = 0;
        return true;
    }
    case LVN_ITEMCHANGED: {
        const auto* lv = reinterpret_cast<NMLISTVIEW*>(nm);
        if (!selecting_ && (lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED)) SelectRow(lv->iItem);
        result = 0;
        return true;
    }
    default: return false;
    }
}

bool RecordPage::OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result)
{
    if (msg == kDataChanged) {   // spec #8 9.3: another page or a job changed the masters
        if ((wp & groups_) && reinterpret_cast<HWND>(lp) != hwnd()) {
            if (!IsDirty()) {
                const std::string key = model_->key();
                const bool wasNew = model_->isNew();
                model_->Load();
                if (!wasNew && !model_->Select(key)) model_->New();
                if (d_.nolist && model_->isNew() && !model_->rows().empty()) model_->Select(model_->rows().front().key);
                Fill();
                ShowList();
            } else {
                ShowInfo(fx::InfoKind::Warning, L"This record may have changed since you opened it. Save keeps your changes; "
                                                L"Don't save, and choose it again, to see the others'.");
            }
        }
        result = 0;
        return true;
    }
    if (msg == WM_DESTROY) {
        const auto it = gOpen.find(command_);
        if (it != gOpen.end() && it->second == hwnd()) gOpen.erase(it);
    }
    return fx::Page::OnMessage(msg, wp, lp, result);
}

// The tab check page's record (spec #9 S9-M0): in memory, a code, a name, a
// Details tab (a note, an amount, an account) and a Budget tab (12 periods).
// Its grid is Budget Data's (spec #9 D1, D2), so the check is the real shape.
// Spec #10 S10-M0: a History tab, a read-only grid of any number of rows (D2,
// the Activity tab's shape), and a Try tab, an input (Rate %) and the field
// that follows it (Result: the amount at that rate) (D5, Disposal's shape).
class TabCheckModel : public RecordModel {
public:
    explicit TabCheckModel(cb4sqlite::Code4& code) : RecordModel(code)
    {
        grid_.SetColumns({{"period", "Period", m::CellKind::Number, 6, true},
                          {"budget", "Budget", m::CellKind::Amount, 16},
                          {"current", "Current Year", m::CellKind::Amount, 16, true},
                          {"previous", "Previous Year", m::CellKind::Amount, 16, true}});
        grid_.SetFixedRows(12);
        history_.SetColumns({{"period", "Pd", m::CellKind::Number, 4, true},
                             {"descript", "Description", m::CellKind::Text, 30, true},
                             {"amount", "Amount", m::CellKind::Amount, 16, true}});
        history_.SetReadOnly(true);
        store_["A1"] = {{"code", "A1"}, {"name", "First"}, {"note", "a note"}, {"amount", "1,250.00"}};
        store_["B2"] = {{"code", "B2"}, {"name", "Second"}};
    }
    std::string Noun() const override { return "check record"; }
    m::EntryGridModel* Grid(const std::string& key) override
    {
        return key == "budget" ? &grid_ : key == "history" ? &history_ : nullptr;
    }
    std::string Get(const std::string& field) const override
    {
        if (field != "result") return RecordModel::Get(field);
        const std::int64_t cents = record::ToCents(RecordModel::Get("amount")) * record::ToInt(RecordModel::Get("rate")) / 100;
        return record::Cents(cents);
    }

protected:
    std::vector<RecordRow> LoadRows() override
    {
        std::vector<RecordRow> rows;
        for (const auto& [key, v] : store_) rows.push_back({key, {key, value(v, "name")}});
        return rows;
    }
    bool Read(const std::string& key, Values& out) override
    {
        const auto it = store_.find(key);
        if (it == store_.end()) return false;
        out = it->second;
        return true;
    }
    std::string KeyOf(const Values& v) const override { return value(v, "code"); }
    std::string KeyField() const override { return "code"; }
    Result Write(Values& v, bool) override
    {
        Values stored = v;
        for (int r = 0; r < grid_.Rows(); ++r) stored["budget" + std::to_string(r + 1)] = grid_.Cell(r, "budget");
        store_[value(v, "code")] = stored;
        return {true, "", ""};
    }
    Result Remove(const std::string& key) override
    {
        store_.erase(key);
        return {true, "", ""};
    }
    void LoadGrids(const std::string& key) override
    {
        const auto it = store_.find(key);
        std::vector<std::vector<std::string>> rows;
        for (int r = 1; r <= 12; ++r)
            rows.push_back({std::to_string(r),
                            it == store_.end() ? std::string() : value(it->second, "budget" + std::to_string(r))});
        grid_.SetRows(rows);
        std::vector<std::vector<std::string>> history;   // A1 has three lines, the others none
        if (key == "A1")
            history = {{"1", "Opening", "1,000.00"}, {"4", "A change", "250.00"}, {"7", "Another", "-500.00"}};
        history_.SetRows(history);
    }
    Values InputDefaults(const std::string&) override { return {{"rate", "10"}}; }

private:
    m::EntryGridModel grid_, history_;
    std::map<std::string, Values> store_;
};

const PageEntry& tabCheckEntry()
{
    static const PageEntry e{0, &k_tabCheck, [] { return std::make_unique<TabCheckModel>(cb); }, 0, L""};
    return e;
}

// Owns itself: deleted after its window goes.
class OwnedRecordPage : public RecordPage {
public:
    using RecordPage::RecordPage;

protected:
    void OnFinalMessage() override { delete this; }
};

} // namespace

bool ShowRecord(HWND page, const std::string& key)
{
    auto* p = dynamic_cast<RecordPage*>(fx::Window::FromHandle(page));
    return p && p->ShowKey(key);
}

bool ShowRecordTab(HWND page, const std::wstring& caption)
{
    auto* p = dynamic_cast<RecordPage*>(fx::Window::FromHandle(page));
    return p && p->ChooseTab(caption);
}

bool IsRecordPageCommand(WORD command)
{
    return entryOf(command) != nullptr;
}

namespace {

HWND openEntry(fx::PageHost& host, const PageEntry& e)
{
    const auto it = gOpen.find(e.command);
    if (it != gOpen.end() && IsWindow(it->second)) {
        host.ActivatePage(it->second);
        return it->second;
    }
    auto* page = new OwnedRecordPage(e);
    HWND h = page->Open(host, e.desc->title);
    if (!h) {
        delete page;
        return nullptr;
    }
    gOpen[e.command] = h;
    return h;
}

} // namespace

HWND OpenRecordPage(fx::PageHost& host, WORD command)
{
    const PageEntry* e = entryOf(command);
    return e ? openEntry(host, *e) : nullptr;
}

const std::vector<ClassicCommand>& ClassicCommands()
{
    static const std::vector<ClassicCommand> all = [] {
        std::vector<ClassicCommand> v;
        for (const PageEntry& e : entries()) v.push_back({e.command, e.classic, e.classicText});
        return v;
    }();
    return all;
}

std::vector<const PageDesc*> RecordPageDescs()
{
    std::vector<const PageDesc*> v;
    for (const PageEntry& e : entries()) v.push_back(e.desc);
    v.push_back(tabCheckEntry().desc);   // spec #9 S9-M0
    return v;
}

int StressRecordPages(int times)
{
    class StressWorkspace : public fx::Workspace {
    protected:
        bool OnDestroy() override { return true; }   // not the main window
    };
    auto objects = [] {
        HANDLE self = GetCurrentProcess();
        return static_cast<int>(GetGuiResources(self, GR_GDIOBJECTS) + GetGuiResources(self, GR_USEROBJECTS));
    };
    StressWorkspace w;
    if (!w.CreateWorkspace(hInst, L"GLRecordPageStress", L"page stress", nullptr, -1)) return -1;
    auto once = [&] {
        for (const PageEntry& e : entries()) {
            HWND h = OpenRecordPage(w, e.command);
            auto* p = dynamic_cast<RecordPage*>(fx::Window::FromHandle(h));
            if (!p) return false;
            p->Filter("1");
            p->SelectShown(0);
            p->Filter("");
            p->SelectShown(1);
            for (int t = 0; t < p->tabCount(); ++t) p->ChooseTab(t);   // the Budget tab (S9-M1), Company Profile's (S9-M2)
            p->SelectShown(0);
            p->ChooseTab(0);
            w.ClosePage(h);
            if (IsWindow(h)) return false;
        }
        // The tab check page (spec #9 S9-M0): each tab, the budget typed into and saved.
        HWND h = openEntry(w, tabCheckEntry());
        auto* p = dynamic_cast<RecordPage*>(fx::Window::FromHandle(h));
        if (!p) return false;
        p->SelectShown(0);
        p->ChooseTab(1);
        p->TypeInGrid(1, "125");
        p->ChooseTab(0);
        if (p->IsDirty() && !p->Save()) return false;   // saved, so choosing another asks nothing
        p->ChooseTab(2);                                 // the read-only History grid (spec #10 D2)
        p->TypeInGrid(2, "9");                           // takes nothing...
        if (p->IsDirty()) return false;                  // ...and leaves the page clean
        p->ChooseTab(3);
        if (!p->TypeInField("rate", "25")) return false; // an input (D5): still clean
        p->SelectShown(1);
        p->ChooseTab(1);
        w.ClosePage(h);
        return !IsWindow(h);
    };
    if (!once()) {   // first open: class registration, the theme
        DestroyWindow(w.hwnd());
        return -1;
    }
    const int before = objects();
    for (int i = 0; i < times; ++i)
        if (!once()) {
            DestroyWindow(w.hwnd());
            return -1;
        }
    const int growth = objects() - before;
    DestroyWindow(w.hwnd());
    return growth;
}
