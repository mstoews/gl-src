//
// CustomersPage.cpp - see CustomersPage.h.
//
// Translation notes (decisions are spec #8 §13):
//   - D4, §5.1: the list replaces typing a number and pressing Enter, and
//     Top, Next, Previous, Find and List; Options' fields (accounts, terms,
//     VAT, contact, VAT No.) are on the page, so there is no Options dialog.
//   - D9: the page knows a new customer from one on file, so Save doesn't
//     ask "has already been added. Do you want to modify the entry?"; a
//     customer's number can't be changed once saved.
//   - §6.3: the accounts are lookups over the chart, account and dept, with
//     the account's name beside them.
//   - §7: a problem marks its field and shows in the information bar; Delete
//     asks in a task dialog with named buttons.
//
#include "CustomersPage.h"

#include "../Accounts.h"   // TrimText
#include "../DataChanged.h"
#include "TaskModels.h"   // data_group
#include "../Globals.h"
#include "../PortIds.h"
#include "CustomersModel.h"

#include <CommCtrl.h>
#include <Uxtheme.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>

namespace {

namespace m = fx::model;

HWND gOpen = nullptr;   // the one Customers page

const std::vector<std::pair<int, unsigned>> kAnchors = {
    {IDC_PG_NEW, m::kTopLeft},          {IDC_PG_SAVE, m::kTopLeft},          {IDC_PG_DELETE, m::kTopLeft},
    {IDC_PGC_FILTER, m::kTopLeft},      {IDC_PGC_LIST, m::kLeftTall},        {IDC_PGC_GCUST, m::kTopLeft},
    {IDC_PGC_NUMBER, m::kTopLeft},      {IDC_PGC_SHORT, m::kTopLeft},        {IDC_PGC_NAME, m::kTopLeft},
    {IDC_PGC_GADDR, m::kTopLeft},       {IDC_PGC_ADDR1, m::kTopLeft},        {IDC_PGC_ADDR2, m::kTopLeft},
    {IDC_PGC_ADDR3, m::kTopLeft},       {IDC_PGC_POSTAL, m::kTopLeft},       {IDC_PGC_PHONE, m::kTopLeft},
    {IDC_PGC_FAX, m::kTopLeft},         {IDC_PGC_GTERMS, m::kTopLeft},       {IDC_PGC_TERMS, m::kTopLeft},
    {IDC_PGC_VATRATE, m::kTopLeft},     {IDC_PGC_VIN, m::kTopLeft},          {IDC_PGC_CONTACT, m::kTopLeft},
    {IDC_PGC_GSHIP, m::kTopLeft},       {IDC_PGC_SHIP1, m::kTopLeft},        {IDC_PGC_SHIP2, m::kTopLeft},
    {IDC_PGC_SHIP3, m::kTopLeft},       {IDC_PGC_SHIPPOSTAL, m::kTopLeft},   {IDC_PGC_SHIPPHONE, m::kTopLeft},
    {IDC_PGC_SHIPFAX, m::kTopLeft},     {IDC_PGC_GACCT, m::kTopLeft},        {IDC_PGC_ARACCT, m::kTopLeft},
    {IDC_PGC_ARDEPT, m::kTopLeft},      {IDC_PGC_ARNAME, m::kTopLeft},       {IDC_PGC_SALESACCT, m::kTopLeft},
    {IDC_PGC_SALESDEPT, m::kTopLeft},   {IDC_PGC_SALESNAME, m::kTopLeft},    {IDC_PGC_VATACCT, m::kTopLeft},
    {IDC_PGC_VATDEPT, m::kTopLeft},     {IDC_PGC_VATNAME, m::kTopLeft},      {IDC_PGC_GOPEN, m::kAll},
    {IDC_PGC_OPEN, m::kAll},
};

// The text fields and the customer's members they show.
struct TextField {
    int id;
    std::string Customer::*member;
    size_t width;   // AR_CUST's field width
    bool required;
};
const TextField kText[] = {
    {IDC_PGC_NUMBER, &Customer::number, 6, true},          {IDC_PGC_SHORT, &Customer::shortName, 12, true},
    {IDC_PGC_NAME, &Customer::name, 40, true},             {IDC_PGC_ADDR1, &Customer::address1, 30, false},
    {IDC_PGC_ADDR2, &Customer::address2, 30, false},       {IDC_PGC_ADDR3, &Customer::address3, 30, false},
    {IDC_PGC_POSTAL, &Customer::postal, 8, false},         {IDC_PGC_PHONE, &Customer::phone, 16, false},
    {IDC_PGC_FAX, &Customer::fax, 16, false},              {IDC_PGC_SHIP1, &Customer::shipAddress1, 40, false},
    {IDC_PGC_SHIP2, &Customer::shipAddress2, 40, false},   {IDC_PGC_SHIP3, &Customer::shipAddress3, 40, false},
    {IDC_PGC_SHIPPOSTAL, &Customer::shipPostal, 8, false}, {IDC_PGC_SHIPPHONE, &Customer::shipPhone, 16, false},
    {IDC_PGC_SHIPFAX, &Customer::shipFax, 16, false},      {IDC_PGC_VIN, &Customer::vin, 10, false},
    {IDC_PGC_CONTACT, &Customer::contact, 12, false},
};

// An account and its dept: the edits, the name beside them, and the customer's member.
struct AccountField {
    int account, dept, name;
    ApAccount Customer::*member;
};
const AccountField kAccounts[] = {
    {IDC_PGC_ARACCT, IDC_PGC_ARDEPT, IDC_PGC_ARNAME, &Customer::ar},
    {IDC_PGC_SALESACCT, IDC_PGC_SALESDEPT, IDC_PGC_SALESNAME, &Customer::sales},
    {IDC_PGC_VATACCT, IDC_PGC_VATDEPT, IDC_PGC_VATNAME, &Customer::vat},
};

int controlOf(CustomerInput in)
{
    switch (in) {
    case CustomerInput::Number: return IDC_PGC_NUMBER;
    case CustomerInput::ShortName: return IDC_PGC_SHORT;
    case CustomerInput::Name: return IDC_PGC_NAME;
    case CustomerInput::ArAccount: return IDC_PGC_ARACCT;
    case CustomerInput::SalesAccount: return IDC_PGC_SALESACCT;
    case CustomerInput::VatAccount: return IDC_PGC_VATACCT;
    case CustomerInput::VatRate: return IDC_PGC_VATRATE;
    case CustomerInput::Terms: return IDC_PGC_TERMS;
    }
    return IDC_PGC_NAME;
}

std::wstring wide(const std::string& s) { return fx::Utf8ToWide(s); }

void addColumns(HWND list, std::initializer_list<std::pair<const wchar_t*, int>> cols, int dpi, int rightFrom)
{
    int i = 0;
    for (const auto& [text, width96] : cols) {
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        c.fmt = i >= rightFrom ? LVCFMT_RIGHT : LVCFMT_LEFT;
        c.cx = MulDiv(width96, dpi, 96);
        c.pszText = const_cast<wchar_t*>(text);
        ListView_InsertColumn(list, i++, &c);
    }
}

class CustomersPage : public fx::Page {
public:
    CustomersPage() : fx::Page(IDD_PG_CUSTOMERS), model_(cb) {}

    bool Save() override;

    void SelectNumber(const std::string& number);   // /stress
    void Filter(const std::string& text) { SetText(IDC_PGC_FILTER, text); }

protected:
    bool OnInitPage() override;
    bool OnFormCommand(WORD id, WORD code, HWND ctl) override;
    bool OnFormNotify(NMHDR* nm, LRESULT& result) override;
    void OnNew() override;
    bool OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override;

private:
    void LoadAccounts();
    void ShowList();
    void Fill();
    void FillOpenItems();
    void Read();
    void ShowAccountName(const AccountField& a);
    bool SwitchAllowed();   // unsaved changes: save, discard, or stay
    void SelectRow(int row);
    void Delete();
    fx::Lookup* lookupOf(int id);

    CustomersModel model_;
    m::LookupModel accounts_;
    fx::Lookup lookups_[3];
    std::vector<size_t> shown_;   // list row -> model row
    HWND list_ = nullptr, open_ = nullptr;
    bool selecting_ = false;
};

bool CustomersPage::OnInitPage()
{
    SetAnchors(kAnchors);
    list_ = Item(IDC_PGC_LIST);
    open_ = Item(IDC_PGC_OPEN);
    const int dpi = static_cast<int>(GetDpiForWindow(form()));
    for (HWND l : {list_, open_}) {
        SetWindowTheme(l, L"Explorer", nullptr);
        ListView_SetExtendedListViewStyle(l, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    }
    addColumns(list_, {{L"Customer", 64}, {L"Name", 150}, {L"Owed", 80}}, dpi, 2);
    addColumns(open_, {{L"Invoice", 64}, {L"Due", 72}, {L"Amount", 76}, {L"Owed", 76}}, dpi, 2);
    SendDlgItemMessageW(form(), IDC_PGC_FILTER, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Filter by number or name"));
    for (const TextField& f : kText) {
        SendDlgItemMessageW(form(), f.id, EM_LIMITTEXT, f.width, 0);
        m::Validator v;
        if (f.required) v.Add(m::Required());
        v.Add(m::MaxLength(f.width));
        SetValidator(f.id, std::move(v));
    }
    SetValidator(IDC_PGC_TERMS, {m::WholeNumber(0, 999)});
    SetValidator(IDC_PGC_VATRATE, {m::WholeNumber(0, 99)});
    LoadAccounts();
    for (size_t i = 0; i < std::size(kAccounts); ++i) {
        const AccountField a = kAccounts[i];
        SendDlgItemMessageW(form(), a.account, EM_LIMITTEXT, 7, 0);
        SendDlgItemMessageW(form(), a.dept, EM_LIMITTEXT, 4, 0);
        SetValidator(a.account, {m::Exists([this](const std::string& s) {
                                     for (const m::LookupItem& it : accounts_.items())
                                         if (it.code == s) return true;
                                     return false;
                                 }, "Not an account in the chart.")});
        lookups_[i].Attach(Item(a.account), &accounts_, [this, a](const m::LookupItem& it) {
            Filling f(*this);
            SetText(a.dept, it.sub);
            SetText(a.name, it.name);
            SetDirty(true);
            ClearMark(a.account);
        });
    }
    model_.Load();
    model_.New();
    ShowList();
    Fill();
    return true;
}

void CustomersPage::LoadAccounts()
{
    std::vector<m::LookupItem> items;
    cb4sqlite::Query q(cb, "SELECT trim(account), coalesce(trim(dept), ''), trim(descript) FROM glaccts "
                           "WHERE _deleted = 0 ORDER BY 1, 2");
    while (q.next()) items.push_back({q.text(0), q.text(1), q.text(2)});
    accounts_.SetItems(std::move(items));
}

fx::Lookup* CustomersPage::lookupOf(int id)
{
    for (size_t i = 0; i < std::size(kAccounts); ++i)
        if (kAccounts[i].account == id) return &lookups_[i];
    return nullptr;
}

void CustomersPage::ShowList()
{
    shown_ = model_.Filter(Text(IDC_PGC_FILTER));
    selecting_ = true;
    ListView_SetItemCountEx(list_, static_cast<int>(shown_.size()), LVSICF_NOSCROLL);
    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (!model_.isNew()) {
        const int row = model_.RowOf(model_.current().number);
        for (size_t i = 0; i < shown_.size(); ++i)
            if (static_cast<int>(shown_[i]) == row) {
                ListView_SetItemState(list_, static_cast<int>(i), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(list_, static_cast<int>(i), FALSE);
            }
    }
    InvalidateRect(list_, nullptr, FALSE);
    selecting_ = false;
}

void CustomersPage::Fill()
{
    Filling f(*this);
    const Customer& c = model_.current();
    for (const TextField& t : kText) SetText(t.id, TrimText(c.*(t.member)));
    for (const AccountField& a : kAccounts) {
        SetText(a.account, TrimText((c.*(a.member)).account));
        SetText(a.dept, TrimText((c.*(a.member)).dept));
        ShowAccountName(a);
    }
    SetText(IDC_PGC_TERMS, model_.isNew() && c.terms == 0 ? std::string() : std::to_string(c.terms));
    SetText(IDC_PGC_VATRATE, model_.isNew() && c.vatRate == 0 ? std::string() : std::to_string(c.vatRate));
    // A number on file is the customer's key: it can't be changed (D9).
    SendDlgItemMessageW(form(), IDC_PGC_NUMBER, EM_SETREADONLY, model_.isNew() ? FALSE : TRUE, 0);
    EnableWindow(Item(IDC_PG_DELETE), !model_.isNew());
    FillOpenItems();
    ClearMarks();
    SetDirty(false);
    SetPageTitle(model_.isNew() ? L"Customers" : L"Customers - " + wide(TrimText(c.number)));
}

void CustomersPage::FillOpenItems()
{
    ListView_DeleteAllItems(open_);
    int row = 0;
    for (const ArOpenItem& i : model_.OpenItems()) {
        std::wstring inv = wide(TrimText(i.invoiceNo));
        std::wstring due = i.dueDate.size() == 8 ? wide(i.dueDate.substr(4, 2) + "/" + i.dueDate.substr(6, 2) + "/" +
                                                        i.dueDate.substr(0, 4))
                                                 : std::wstring();
        std::wstring amount = wide(m::FormatAmount(i.amount)), owed = wide(m::FormatAmount(i.remainder));
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = row;
        it.pszText = inv.data();
        ListView_InsertItem(open_, &it);
        ListView_SetItemText(open_, row, 1, due.data());
        ListView_SetItemText(open_, row, 2, amount.data());
        ListView_SetItemText(open_, row, 3, owed.data());
        ++row;
    }
}

void CustomersPage::ShowAccountName(const AccountField& a)
{
    const int i = accounts_.Exact(m::LookupItem{Text(a.account), Text(a.dept), ""}.key());
    Filling f(*this);
    SetText(a.name, i >= 0 ? accounts_.items()[static_cast<size_t>(i)].name : std::string());
}

void CustomersPage::Read()
{
    Customer& c = model_.edit();
    for (const TextField& t : kText) {
        if (t.id == IDC_PGC_NUMBER && !model_.isNew()) continue;   // the key stays
        c.*(t.member) = m::Trim(Text(t.id));
    }
    for (const AccountField& a : kAccounts) c.*(a.member) = {m::Trim(Text(a.account)), m::Trim(Text(a.dept))};
}

bool CustomersPage::Save()
{
    if (SaveWaitsForJob(*this)) return false;
    Read();
    bool ok = true;
    for (const TextField& t : kText) ok = Validate(t.id) && ok;
    ok = Validate(IDC_PGC_TERMS) && ok;
    ok = Validate(IDC_PGC_VATRATE) && ok;
    if (!ok) {
        ShowInfo(fx::InfoKind::Error, L"Check the marked fields: hover over one to see what's wrong.");
        return false;
    }
    const CustomersModel::Result r = model_.Save(Text(IDC_PGC_TERMS), Text(IDC_PGC_VATRATE));
    if (!r.ok) {
        if (r.input) MarkField(controlOf(*r.input), r.message, true);
        ShowInfo(fx::InfoKind::Error, wide(r.message));
        return false;
    }
    Fill();
    ShowList();
    ShowInfo(fx::InfoKind::Success, wide(r.message));
    NotifyDataChanged(data_group::kMasters | data_group::kAr, hwnd());   // spec #8 9.3
    return true;
}

bool CustomersPage::SwitchAllowed()
{
    if (!IsDirty()) return true;
    enum { kSave = 101, kDiscard = 102 };
    const int chosen = fx::Ask(host() ? host()->hostWindow() : hwnd(), szApp,
                               L"Save the changes to " + (model_.isNew() ? std::wstring(L"the new customer")
                                                                          : L"customer " + wide(TrimText(model_.current().number))) +
                                   L"?",
                               L"", {{kSave, L"Save"}, {kDiscard, L"Don't save"}, {IDCANCEL, L"Cancel"}}, kSave, IDCANCEL);
    if (chosen == kSave) return Save();
    return chosen == kDiscard;
}

void CustomersPage::SelectRow(int row)
{
    if (row < 0 || static_cast<size_t>(row) >= shown_.size()) return;
    const CustomerRow& r = model_.rows()[shown_[static_cast<size_t>(row)]];
    if (!model_.isNew() && TrimText(model_.current().number) == r.number) return;
    if (!SwitchAllowed()) {
        ShowList();   // back to the customer being edited
        return;
    }
    HideInfo();
    if (!model_.Select(r.number)) {
        ShowInfo(fx::InfoKind::Warning, L"Customer " + wide(r.number) + L" is no longer on file.");
        model_.Load();
        ShowList();
        return;
    }
    Fill();
}

void CustomersPage::SelectNumber(const std::string& number)
{
    const int row = model_.RowOf(number);
    for (size_t i = 0; i < shown_.size(); ++i)
        if (static_cast<int>(shown_[i]) == row) SelectRow(static_cast<int>(i));
}

void CustomersPage::OnNew()
{
    if (!SwitchAllowed()) return;
    HideInfo();
    model_.New();
    Fill();
    ShowList();
    SendMessageW(form(), WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(Item(IDC_PGC_NUMBER)), TRUE);
}

void CustomersPage::Delete()
{
    if (model_.isNew()) return;
    const std::wstring number = wide(TrimText(model_.current().number));
    enum { kDelete = 101 };
    if (fx::Ask(host() ? host()->hostWindow() : hwnd(), szApp, L"Delete customer " + number + L"?",
                L"Only the customer is deleted. It can't be undone.", {{kDelete, L"Delete customer"}, {IDCANCEL, L"Keep it"}},
                IDCANCEL, IDCANCEL) != kDelete)
        return;
    const CustomersModel::Result r = model_.Delete();
    if (!r.ok) {
        ShowInfo(fx::InfoKind::Error, wide(r.message));
        return;
    }
    Fill();
    ShowList();
    ShowInfo(fx::InfoKind::Success, wide(r.message));
}

bool CustomersPage::OnFormCommand(WORD id, WORD code, HWND)
{
    if (code == BN_CLICKED) {
        switch (id) {
        case IDC_PG_NEW: OnNew(); return true;
        case IDC_PG_SAVE: Save(); return true;
        case IDC_PG_DELETE: Delete(); return true;
        default: break;
        }
    }
    if (id == IDC_PGC_FILTER) {
        if (code == EN_CHANGE) ShowList();
        return code == EN_CHANGE || code == EN_KILLFOCUS;   // the filter isn't the customer: not dirty
    }
    if (fx::Lookup* l = lookupOf(id)) {
        if (code == EN_CHANGE && !filling()) l->TextChanged();
        if (code == EN_KILLFOCUS) {
            l->FocusLost();
            for (const AccountField& a : kAccounts)
                if (a.account == id) ShowAccountName(a);
        }
        return false;   // and the page's own handling: dirty, the validator
    }
    if (code == EN_CHANGE && !filling())
        for (const AccountField& a : kAccounts)
            if (a.dept == id) ShowAccountName(a);
    return false;
}

bool CustomersPage::OnFormNotify(NMHDR* nm, LRESULT& result)
{
    if (nm->hwndFrom != list_) return false;
    switch (nm->code) {
    case LVN_GETDISPINFOW: {
        auto* di = reinterpret_cast<NMLVDISPINFOW*>(nm);
        const int row = di->item.iItem;
        if (!(di->item.mask & LVIF_TEXT) || row < 0 || static_cast<size_t>(row) >= shown_.size()) return true;
        const CustomerRow& r = model_.rows()[shown_[static_cast<size_t>(row)]];
        const std::string text = di->item.iSubItem == 0   ? r.number
                                 : di->item.iSubItem == 1 ? r.name
                                                          : (r.owed ? m::FormatAmount(r.owed) : std::string());
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

bool CustomersPage::OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result)
{
    if (msg == WM_DESTROY && gOpen == hwnd()) gOpen = nullptr;
    return fx::Page::OnMessage(msg, wp, lp, result);
}

// Owns itself: deleted after its window goes.
class OwnedCustomersPage : public CustomersPage {
protected:
    void OnFinalMessage() override { delete this; }
};

} // namespace

HWND OpenCustomersPage(fx::PageHost& host)
{
    if (gOpen && IsWindow(gOpen)) {
        host.ActivatePage(gOpen);
        return gOpen;
    }
    auto* page = new OwnedCustomersPage;
    page->commandId = 4059;   // Accounts > Sales Customers: so the tab can be pinned
    HWND h = page->Open(host, L"Customers");
    if (!h) {
        delete page;
        return nullptr;
    }
    gOpen = h;
    return h;
}

const std::vector<std::pair<int, unsigned>>& CustomersPageAnchors()
{
    return kAnchors;
}

int StressCustomersPage(int times)
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
    if (!w.CreateWorkspace(hInst, L"GLPageStress", L"page stress", nullptr, -1)) return -1;
    auto once = [&] {
        HWND h = OpenCustomersPage(w);
        auto* p = dynamic_cast<CustomersPage*>(fx::Window::FromHandle(h));
        if (!p) return false;
        p->Filter("C0");
        p->SelectNumber("C002");
        p->Filter("");
        p->SelectNumber("C001");
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
