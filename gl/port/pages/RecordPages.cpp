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
//
#include "RecordPage.h"

#include "../Accounts.h"   // TrimText
#include "../DataChanged.h"
#include "../FixedAssetDisposal.h"
#include "../Globals.h"
#include "../PortIds.h"
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
};

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
    int i = 0;
    for (const ColumnDesc& col : cols) {
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        c.fmt = col.right ? LVCFMT_RIGHT : LVCFMT_LEFT;
        c.cx = MulDiv(col.width, dpi, 96);
        c.pszText = const_cast<wchar_t*>(col.heading);
        ListView_InsertColumn(list, i++, &c);
    }
}

bool isEditable(const FieldDesc& f)
{
    return f.kind != FieldKind::ReadOnly;
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
    RecordPage(const PageEntry& e) : fx::Page(e.desc->form), d_(*e.desc), command_(e.command), model_(e.make())
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
    void ShowAccountName(const FieldDesc& f);
    bool SwitchAllowed();
    void SelectRow(int row);
    void Delete();
    void OnButton(int id);
    fx::Lookup* lookupOf(int id);
    UsersModel* users() { return d_.commands ? static_cast<UsersModel*>(model_.get()) : nullptr; }

    const PageDesc& d_;
    WORD command_;
    std::unique_ptr<RecordModel> model_;
    m::LookupModel accounts_;
    std::map<int, std::unique_ptr<fx::Lookup>> lookups_;            // account edit -> its lookup
    std::map<int, std::vector<std::string>> choices_;                // combo -> the values, by index
    std::vector<size_t> shown_;                                      // list row -> model row
    HWND list_ = nullptr, commands_ = nullptr;
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
                SetDirty(true);
                ClearMark(fp->id);
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
    model_->Load();
    model_->New();
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
    EnableWindow(Item(IDC_PG_DELETE), !model_->isNew());
    for (const ButtonDesc& b : d_.buttons) EnableWindow(Item(b.id), !model_->isNew());
    ClearMarks();
    SetDirty(false);
    SetPageTitle(model_->isNew() ? std::wstring(d_.title) : std::wstring(d_.title) + L" - " + wide(model_->key()));
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

void RecordPage::OnFieldChanged(int id)
{
    fx::Page::OnFieldChanged(id);
    Read();
    RefreshDerived();
}

bool RecordPage::Save()
{
    if (SaveWaitsForJob(*this)) return false;
    Read();
    bool ok = true;
    for (const FieldDesc& f : d_.fields)
        if (isEditable(f) && f.kind != FieldKind::Choice && f.kind != FieldKind::Date) ok = Validate(f.id) && ok;
    if (!ok) {
        ShowInfo(fx::InfoKind::Error, L"Check the marked fields: hover over one to see what's wrong.");
        return false;
    }
    const RecordModel::Result r = model_->Save();
    if (!r.ok) {
        if (const FieldDesc* f = fieldOfKey(r.field)) MarkField(f->id, r.message, true);
        ShowInfo(fx::InfoKind::Error, wide(r.message));
        return false;
    }
    Fill();
    ShowList();
    ShowInfo(fx::InfoKind::Success, wide(r.message));
    NotifyDataChanged(data_group::kMasters, hwnd());   // spec #8 9.3
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

void RecordPage::OnButton(int id)
{
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
        SetDirty(true);
        ClearMark(id);
        Read();
        RefreshDerived();
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
    if (nm->code == DTN_DATETIMECHANGE && !filling()) {
        SetDirty(true);
        ClearMark(static_cast<int>(nm->idFrom));
        Read();
        RefreshDerived();
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
        if ((wp & data_group::kMasters) && reinterpret_cast<HWND>(lp) != hwnd()) {
            if (!IsDirty()) {
                const std::string key = model_->key();
                const bool wasNew = model_->isNew();
                model_->Load();
                if (!wasNew && !model_->Select(key)) model_->New();
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

bool IsRecordPageCommand(WORD command)
{
    return entryOf(command) != nullptr;
}

HWND OpenRecordPage(fx::PageHost& host, WORD command)
{
    const PageEntry* e = entryOf(command);
    if (!e) return nullptr;
    const auto it = gOpen.find(command);
    if (it != gOpen.end() && IsWindow(it->second)) {
        host.ActivatePage(it->second);
        return it->second;
    }
    auto* page = new OwnedRecordPage(*e);
    HWND h = page->Open(host, e->desc->title);
    if (!h) {
        delete page;
        return nullptr;
    }
    gOpen[command] = h;
    return h;
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
            p->SelectShown(0);
            w.ClosePage(h);
            if (IsWindow(h)) return false;
        }
        return true;
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
