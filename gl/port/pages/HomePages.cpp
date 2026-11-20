//
// HomePages.cpp - see HomePages.h.
//
// Translation notes (decisions are spec #8 13):
//   - 5.5, Q5: the home page is new. Each tile is a command link naming a
//     figure from the cores (HomeModels.h LoadHomeFigures) and opening the page
//     that deals with it; the figures refresh on the data-changed broadcast.
//   - 5.4, D9: the report gallery replaces the report list (1022) for Custom
//     Report: built-in reports grouped by their menu, then the user reports;
//     search, Favourites and Recent. A report opens as it does from its menu
//     item (its options dialog, then its preview in a tab).
//   - Deferred (plan tasks 3 and 5): the options dialogs stay dialogs, opened
//     before the preview and again by its Refresh (RunReport's rebuild), not
//     panels above it; the designer stays a dialog. The preview is already a
//     page with Print, CSV, the page buttons and zoom (spec #3).
//   - 4: Ctrl+K searches the commands (greyed ones left out) and the masters;
//     choosing a record opens its page on it.
//
#include "HomePages.h"

#include "../Commands.h"
#include "../DataChanged.h"
#include "../Globals.h"
#include "../PortIds.h"
#include "../ReportWriter.h"
#include "../UiState.h"
#include "../Vouchers.h"
#include "../reports/ReportDesigner.h"
#include "../reports/RunReport.h"
#include "CustomersPage.h"
#include "HomeModels.h"
#include "TaskModels.h"   // data_group
#include "VoucherPages.h"

#include <CommCtrl.h>
#include <Uxtheme.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>

extern char szMsg[256];   // the session's menu code (App.cpp; gl.h declares it)

namespace {

namespace m = fx::model;

std::wstring wide(const std::string& s) { return fx::Utf8ToWide(s); }
std::wstring money(std::int64_t cents) { return wide(m::FormatAmount(cents)); }

std::string today()
{
    SYSTEMTIME t{};
    GetLocalTime(&t);
    char buf[16];
    snprintf(buf, sizeof buf, "%04u%02u%02u", t.wYear, t.wMonth, t.wDay);
    return buf;
}

std::wstring commandName(WORD id)
{
    const std::vector<std::wstring> path = cmd::PathOf(id);
    std::wstring name;
    for (const cmd::MenuRow& r : cmd::MenuRows())
        if (r.kind == cmd::Kind::Item && r.id == id) {
            name = cmd::PlainText(r.text);
            break;
        }
    while (!name.empty() && name.back() == L'.') name.pop_back();
    if (id == CM_HOME) return L"Home";
    if (name.empty()) return L"Command " + std::to_wstring(id);
    return path.empty() ? name : path.back() + L" > " + name;
}

std::vector<WORD> gRecent;

const std::vector<std::pair<int, unsigned>> kHomeAnchors = {
    {IDC_HM_HEADING, m::kTopWide}, {IDC_HM_REFRESH, m::kTopRight}, {IDC_HM_TILE0, m::kTopLeft}, {IDC_HM_TILE1, m::kTopLeft},
    {IDC_HM_TILE2, m::kTopLeft},   {IDC_HM_TILE3, m::kTopLeft},    {IDC_HM_TILE4, m::kTopLeft}, {IDC_HM_TILE5, m::kTopLeft},
    {IDC_HM_TILE6, m::kTopLeft},   {IDC_HM_TILE7, m::kTopLeft},    {IDC_HM_TILE8, m::kTopLeft}, {IDC_HM_TILE9, m::kTopLeft},
    {IDC_HM_TILE10, m::kTopLeft},  {IDC_HM_GRECENT, m::kLeftTall}, {IDC_HM_RECENT, m::kLeftTall}, {IDC_HM_GFAV, m::kAll},
    {IDC_HM_FAVOURITES, m::kAll},
};

const std::vector<std::pair<int, unsigned>> kGalleryAnchors = {
    {IDC_RG_OPEN, m::kTopLeft},   {IDC_RG_FAVOURITE, m::kTopLeft}, {IDC_RG_NEW, m::kTopLeft}, {IDC_RG_MODIFY, m::kTopLeft},
    {IDC_RG_COPY, m::kTopLeft},   {IDC_RG_DELETE, m::kTopLeft},    {IDC_RG_FILTER, m::kTopWide}, {IDC_RG_LIST, m::kAll},
};

bool shownNow(UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SHOWWINDOW) return wp != 0;
    return msg == WM_WINDOWPOSCHANGED && (reinterpret_cast<const WINDOWPOS*>(lp)->flags & SWP_SHOWWINDOW);
}

// ---- home ---------------------------------------------------------------------------------

class HomePage : public fx::Page {
public:
    HomePage() : fx::Page(IDD_PG_HOME) { commandId = CM_HOME; }

    void Refresh()
    {
        try {
            f_ = LoadHomeFigures(cb, today());
        } catch (const std::exception& e) {
            ShowInfo(fx::InfoKind::Error, wide(e.what()));
            return;
        }
        auto plural = [](int n, const wchar_t* one, const wchar_t* many) { return std::to_wstring(n) + L" " + (n == 1 ? one : many); };
        SetDlgItemTextW(form(), IDC_HM_HEADING,
                        (L"Period " + std::to_wstring(f_.period) + L" (" + wide(f_.periodName) + L") is the period entry uses; " +
                         std::to_wstring(f_.openPeriods) + L" periods are open.")
                            .c_str());
        Tile(0, L"Period " + std::to_wstring(f_.period) + L": " + wide(f_.periodName), L"Enter a GL voucher", 10076);
        Tile(1, plural(f_.glWaiting, L"GL voucher to post", L"GL vouchers to post"), L"Post them", 10121);
        Tile(2, plural(f_.apWaiting, L"purchase or cash voucher to post", L"purchase and cash vouchers to post"),
             L"Post the purchase vouchers", 1048);
        Tile(3, plural(f_.arWaiting, L"sales voucher to post", L"sales vouchers to post"), L"Post them", 4049);
        Tile(4, plural(f_.journalRows, L"journal row to transfer", L"journal rows to transfer"), L"Update AR and AP", 1028);
        Tile(5, plural(f_.chequesToPrint, L"cheque to print", L"cheques to print"), L"Print cheques", 4091);
        Tile(6, f_.depreciationRun ? L"Depreciation run for period " + std::to_wstring(f_.period) + L": done"
                                   : L"Depreciation for period " + std::to_wstring(f_.period) + L": not run",
             f_.depreciationRun ? (f_.depreciationVoucher ? L"GL voucher " + std::to_wstring(f_.depreciationVoucher) : std::wstring(L"No voucher"))
                                : plural(f_.assetsDue, L"asset due", L"assets due") + L": run it",
             6003);
        Tile(7, L"Owed to us: " + money(f_.arOwed), L"Overdue " + money(f_.arOverdue) + L": open receivables", 4057);
        Tile(8, L"We owe: " + money(f_.apOwed), L"Overdue " + money(f_.apOverdue) + L": open payables", 1061);
        Tile(9, L"Reports", L"The report gallery", CM_REPORT_LIST);
        Tile(10, L"Search (Ctrl+K)", L"Commands, accounts, customers, vendors, vouchers", 0);
        HWND recent = Item(IDC_HM_RECENT);
        SendMessageW(recent, LB_RESETCONTENT, 0, 0);
        recent_ = gRecent;
        for (WORD c : recent_) SendMessageW(recent, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(commandName(c).c_str()));
        if (recent_.empty()) SendMessageW(recent, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(none yet)"));
        HWND fav = Item(IDC_HM_FAVOURITES);
        SendMessageW(fav, LB_RESETCONTENT, 0, 0);
        favourites_.clear();
        GalleryModel g;
        g.SetEntries(LoadGalleryEntries(cb));
        const std::string user = UiStateUser(szUser);
        g.LoadState(GetUiState(cb, user, "report.favourites").value_or(""), "");
        for (size_t i : g.Favourites()) {
            favourites_.push_back(g.entries()[i]);
            SendMessageW(fav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(g.entries()[i].name).c_str()));
        }
        if (favourites_.empty()) SendMessageW(fav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(none: mark them in the report gallery)"));
    }

protected:
    bool OnInitPage() override
    {
        SetAnchors(kHomeAnchors);
        Refresh();
        return true;
    }

    bool OnFormCommand(WORD id, WORD code, HWND) override
    {
        if (code == BN_CLICKED && id == IDC_HM_REFRESH) {
            Refresh();
            return true;
        }
        if (code == BN_CLICKED && id >= IDC_HM_TILE0 && id <= IDC_HM_TILE10) {
            const WORD c = commands_[id - IDC_HM_TILE0];
            if (c) PostMessageW(host()->hostWindow(), WM_COMMAND, c, 0);
            else PostMessageW(host()->hostWindow(), WM_COMMAND, CM_COMMAND_SEARCH, 0);
            return true;
        }
        if (code == LBN_DBLCLK && id == IDC_HM_RECENT) {
            const auto i = static_cast<size_t>(SendDlgItemMessageW(form(), id, LB_GETCURSEL, 0, 0));
            if (i < recent_.size()) PostMessageW(host()->hostWindow(), WM_COMMAND, recent_[i], 0);
            return true;
        }
        if (code == LBN_DBLCLK && id == IDC_HM_FAVOURITES) {
            const auto i = static_cast<size_t>(SendDlgItemMessageW(form(), id, LB_GETCURSEL, 0, 0));
            if (i < favourites_.size()) {
                if (favourites_[i].command) PostMessageW(host()->hostWindow(), WM_COMMAND, favourites_[i].command, 0);
                else RunUserReport(*host(), favourites_[i].definition);
            }
            return true;
        }
        return false;
    }

    bool OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override
    {
        if (msg == kDataChanged || shownNow(msg, wp, lp)) {   // spec #8 9.3: the figures follow the books
            if (form()) Refresh();
            if (msg == kDataChanged) {
                result = 0;
                return true;
            }
        }
        return fx::Page::OnMessage(msg, wp, lp, result);
    }

private:
    void Tile(int i, const std::wstring& title, const std::wstring& note, WORD command)
    {
        HWND b = Item(IDC_HM_TILE0 + i);
        SetWindowTextW(b, title.c_str());
        SendMessageW(b, BCM_SETNOTE, 0, reinterpret_cast<LPARAM>(note.c_str()));
        commands_[static_cast<size_t>(i)] = command;
    }

    HomeFigures f_;
    WORD commands_[11] = {};
    std::vector<WORD> recent_;
    std::vector<GalleryEntry> favourites_;
};

// ---- the report gallery ---------------------------------------------------------------------

class GalleryPage : public fx::Page {
public:
    GalleryPage() : fx::Page(IDD_PG_GALLERY) { commandId = CM_REPORT_LIST; }

    void Search(const std::string& text) { SetText(IDC_RG_FILTER, text); }

protected:
    bool OnInitPage() override
    {
        SetAnchors(kGalleryAnchors);
        list_ = Item(IDC_RG_LIST);
        SetWindowTheme(list_, L"Explorer", nullptr);
        ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        const int dpi = static_cast<int>(GetDpiForWindow(list_));
        const std::pair<const wchar_t*, int> cols[] = {{L"Report", 300}, {L"Menu", 180}, {L"About", 360}};
        int i = 0;
        for (const auto& [h, w] : cols) {
            LVCOLUMNW c{};
            c.mask = LVCF_TEXT | LVCF_WIDTH;
            c.cx = MulDiv(w, dpi, 96);
            c.pszText = const_cast<wchar_t*>(h);
            ListView_InsertColumn(list_, i++, &c);
        }
        ListView_EnableGroupView(list_, TRUE);
        SendDlgItemMessageW(form(), IDC_RG_FILTER, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search the reports"));
        design_ = rw::MayDesignReports(cb, szMsg);   // Spec #7 D8: the Report Writer right
        for (int id : {IDC_RG_NEW, IDC_RG_MODIFY, IDC_RG_COPY, IDC_RG_DELETE}) EnableWindow(Item(id), design_);
        Load();
        return true;
    }

    bool OnFormCommand(WORD id, WORD code, HWND) override
    {
        if (id == IDC_RG_FILTER) {
            if (code == EN_CHANGE) Show();
            return code == EN_CHANGE || code == EN_KILLFOCUS;
        }
        if (code != BN_CLICKED) return false;
        const GalleryEntry* e = Selected();
        switch (id) {
        case IDC_RG_OPEN: OpenEntry(); return true;
        case IDC_RG_FAVOURITE:
            if (e) {
                const GalleryEntry copy = *e;
                model_.ToggleFavourite(copy);
                SaveState();
                Show();
                ShowInfo(fx::InfoKind::Success, wide(copy.name) + (model_.IsFavourite(copy) ? L" is a favourite: it's on the home page." : L" is no longer a favourite."));
            }
            return true;
        case IDC_RG_NEW:
            if (RunReportDesigner(host()->hostWindow(), 0)) Load();
            return true;
        case IDC_RG_MODIFY:
        case IDC_RG_COPY:
            if (e && e->definition) {
                if (RunReportDesigner(host()->hostWindow(), e->definition, id == IDC_RG_COPY)) Load();
            } else {
                ShowInfo(fx::InfoKind::Information, L"Choose a user report: the built-in ones can't be changed.");
            }
            return true;
        case IDC_RG_DELETE:
            if (e && e->definition) {
                enum { kDelete = 101 };
                if (fx::Ask(host()->hostWindow(), szApp, L"Delete the report " + wide(e->name) + L"?", L"It can't be undone.",
                            {{kDelete, L"Delete report"}, {IDCANCEL, L"Keep it"}}, IDCANCEL, IDCANCEL) == kDelete) {
                    rw::DeleteDefinition(cb, e->definition);
                    Load();
                }
            } else {
                ShowInfo(fx::InfoKind::Information, L"Choose a user report: the built-in ones can't be deleted.");
            }
            return true;
        default: return false;
        }
    }

    bool OnFormNotify(NMHDR* nm, LRESULT& result) override
    {
        if (nm->hwndFrom != list_) return false;
        if (nm->code == NM_DBLCLK) {
            OpenEntry();
            result = 0;
            return true;
        }
        if (nm->code == LVN_ITEMCHANGED) {
            const GalleryEntry* e = Selected();
            const bool user = e && e->definition;
            for (int id : {IDC_RG_MODIFY, IDC_RG_COPY, IDC_RG_DELETE}) EnableWindow(Item(id), design_ && user);
            if (e) SetDlgItemTextW(form(), IDC_RG_FAVOURITE, model_.IsFavourite(*e) ? L"Un&favourite" : L"&Favourite");
            result = 0;
            return true;
        }
        return false;
    }

private:
    void Load()
    {
        model_.SetEntries(LoadGalleryEntries(cb));
        const std::string user = UiStateUser(szUser);
        model_.LoadState(GetUiState(cb, user, "report.favourites").value_or(""), GetUiState(cb, user, "report.recent").value_or(""));
        Show();
    }

    void SaveState()
    {
        try {
            const std::string user = UiStateUser(szUser);
            SetUiState(cb, user, "report.favourites", model_.favouritesText());
            SetUiState(cb, user, "report.recent", model_.recentText());
        } catch (const std::exception&) {   // a read-only company: not kept
        }
    }

    // Favourites and Recent first (when not searching), then the groups.
    void Show()
    {
        const GalleryEntry* was = Selected();   // the selection stays on its report
        const std::string keep = was ? was->key() : std::string();
        SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list_);
        ListView_RemoveAllGroups(list_);
        shown_.clear();
        std::map<std::string, int> groupIds;
        auto group = [&](const std::string& name) {
            auto it = groupIds.find(name);
            if (it != groupIds.end()) return it->second;
            const int id = static_cast<int>(groupIds.size()) + 1;
            std::wstring header = wide(name);
            LVGROUP g{};
            g.cbSize = sizeof g;
            g.mask = LVGF_HEADER | LVGF_GROUPID;
            g.pszHeader = header.data();
            g.iGroupId = id;
            ListView_InsertGroup(list_, -1, &g);
            groupIds[name] = id;
            return id;
        };
        auto add = [&](size_t index, int groupId) {
            const GalleryEntry& e = model_.entries()[index];
            std::wstring name = wide(e.name), menu = wide(e.group), about = wide(e.detail);
            LVITEMW it{};
            it.mask = LVIF_TEXT | LVIF_GROUPID | LVIF_PARAM;
            it.iItem = static_cast<int>(shown_.size());
            it.pszText = name.data();
            it.iGroupId = groupId;
            it.lParam = static_cast<LPARAM>(shown_.size());
            const int at = ListView_InsertItem(list_, &it);
            ListView_SetItemText(list_, at, 1, menu.data());
            ListView_SetItemText(list_, at, 2, about.data());
            shown_.push_back(index);
        };
        const std::string filter = m::Trim(Text(IDC_RG_FILTER));
        if (filter.empty()) {
            for (size_t i : model_.Favourites()) add(i, group("Favourites"));
            for (size_t i : model_.Recent()) add(i, group("Recent"));
        }
        for (size_t i : model_.Filter(filter)) add(i, group(model_.entries()[i].group));
        if (!keep.empty())
            for (size_t row = shown_.size(); row-- > 0;)   // the last place: its group, after Favourites and Recent
                if (model_.entries()[shown_[row]].key() == keep) {
                    ListView_SetItemState(list_, static_cast<int>(row), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_EnsureVisible(list_, static_cast<int>(row), FALSE);
                    break;
                }
        SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(list_, nullptr, TRUE);
    }

    const GalleryEntry* Selected() const
    {
        const int row = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        if (row < 0) return nullptr;
        LVITEMW it{};
        it.mask = LVIF_PARAM;
        it.iItem = row;
        ListView_GetItem(list_, &it);
        const auto k = static_cast<size_t>(it.lParam);
        return k < shown_.size() ? &model_.entries()[shown_[k]] : nullptr;
    }

    void OpenEntry()
    {
        const GalleryEntry* e = Selected();
        if (!e) {
            ShowInfo(fx::InfoKind::Information, L"Choose a report in the list.");
            return;
        }
        const GalleryEntry copy = *e;
        model_.Opened(copy);
        SaveState();
        if (copy.command) PostMessageW(host()->hostWindow(), WM_COMMAND, copy.command, 0);   // its options, then its preview
        else RunUserReport(*host(), copy.definition);
        Show();
    }

    GalleryModel model_;
    std::vector<size_t> shown_;   // list row -> entry
    HWND list_ = nullptr;
    bool design_ = false;
};

// ---- command search ---------------------------------------------------------------------------

class SearchBox {
public:
    static void Show(fx::PageHost& host, std::function<bool(WORD)> enabled)
    {
        if (instance_ && IsWindow(instance_->wnd_)) {
            SetForegroundWindow(instance_->wnd_);
            return;
        }
        instance_ = new SearchBox(host, std::move(enabled));
    }

private:
    SearchBox(fx::PageHost& host, std::function<bool(WORD)> enabled) : host_(host)
    {
        model_.SetItems(LoadSearchItems(cb, [&enabled](std::uint16_t id) { return !enabled || enabled(id); }));
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc{sizeof wc};
            wc.lpfnWndProc = Proc;
            wc.hInstance = hInst;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = L"GLCommandSearch";
            RegisterClassExW(&wc);
            registered = true;
        }
        HWND owner = host.hostWindow();
        RECT fr{};
        GetWindowRect(owner, &fr);
        const int dpi = static_cast<int>(GetDpiForWindow(owner));
        const int w = MulDiv(560, dpi, 96), h = MulDiv(340, dpi, 96);
        wnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, L"GLCommandSearch", L"Search", WS_POPUP | WS_BORDER | WS_CAPTION | WS_SYSMENU,
                               fr.left + (fr.right - fr.left - w) / 2, fr.top + MulDiv(80, dpi, 96), w, h, owner, nullptr, hInst, this);
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(owner, WM_GETFONT, 0, 0));
        NONCLIENTMETRICSW ncm{sizeof ncm};
        SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, static_cast<UINT>(dpi));
        font_ = CreateFontIndirectW(&ncm.lfMessageFont);
        (void)font;
        RECT c{};
        GetClientRect(wnd_, &c);
        const int pad = MulDiv(8, dpi, 96), eh = MulDiv(24, dpi, 96);
        edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, pad, pad, c.right - 2 * pad, eh,
                                wnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)), hInst, nullptr);
        list_ = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER,
                                pad, 2 * pad + eh, c.right - 2 * pad, c.bottom - 3 * pad - eh, wnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(2)), hInst, nullptr);
        for (HWND x : {edit_, list_}) SendMessageW(x, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
        SendMessageW(edit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search commands, accounts, customers, vendors, vouchers"));
        SetWindowTheme(list_, L"Explorer", nullptr);
        ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        RECT lr{};
        GetClientRect(list_, &lr);
        LVCOLUMNW col{};
        col.mask = LVCF_WIDTH;
        col.cx = lr.right - GetSystemMetricsForDpi(SM_CXVSCROLL, static_cast<UINT>(dpi));
        ListView_InsertColumn(list_, 0, &col);
        SetWindowSubclass(edit_, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
        ShowWindow(wnd_, SW_SHOW);
        SetFocus(edit_);
    }

    ~SearchBox()
    {
        if (font_) DeleteObject(font_);
        if (instance_ == this) instance_ = nullptr;
    }

    void Refilter()
    {
        const std::string text = fx::WideToUtf8(fx::GetDlgItemString(wnd_, 1));
        hits_ = model_.Match(text, 30);
        SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list_);
        int row = 0;
        for (size_t i : hits_) {
            const SearchItem& s = model_.items()[i];
            std::wstring t = wide(s.label);
            LVITEMW it{};
            it.mask = LVIF_TEXT;
            it.iItem = row++;
            it.pszText = t.data();
            ListView_InsertItem(list_, &it);
        }
        if (row) ListView_SetItemState(list_, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(list_, nullptr, TRUE);
    }

    void Move(int by)
    {
        const int n = ListView_GetItemCount(list_);
        if (!n) return;
        int at = ListView_GetNextItem(list_, -1, LVNI_SELECTED) + by;
        at = std::clamp(at, 0, n - 1);
        ListView_SetItemState(list_, at, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list_, at, FALSE);
    }

    void Choose()
    {
        const int row = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        if (row < 0 || static_cast<size_t>(row) >= hits_.size()) return;
        const SearchItem s = model_.items()[hits_[static_cast<size_t>(row)]];
        fx::PageHost& host = host_;
        HWND frame = host.hostWindow();
        DestroyWindow(wnd_);   // first: the page it opens takes the focus
        switch (s.kind) {
        case SearchItem::Kind::Command: PostMessageW(frame, WM_COMMAND, s.command, 0); break;
        case SearchItem::Kind::Account: ShowRecord(OpenRecordPage(host, 1004), s.code); break;
        case SearchItem::Kind::Vendor: ShowRecord(OpenRecordPage(host, 4045), s.code); break;
        case SearchItem::Kind::Customer: ShowCustomer(OpenCustomersPage(host), s.code); break;
        case SearchItem::Kind::Voucher: {
            const long n = std::atol(s.code.c_str());
            bool adjustment = false;
            VoucherHeaders h(cb);
            if (SeekVoucher(h, n)) adjustment = ReadHeader(h).mode == voucher_mode::kAdjustment;
            h.close();
            OpenGlVoucherPage(host, adjustment, n);
            break;
        }
        }
    }

    static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
    {
        auto* self = reinterpret_cast<SearchBox*>(ref);
        if (msg == WM_KEYDOWN) switch (wp) {
            case VK_DOWN: self->Move(1); return 0;
            case VK_UP: self->Move(-1); return 0;
            case VK_NEXT: self->Move(10); return 0;
            case VK_PRIOR: self->Move(-10); return 0;
            case VK_RETURN: self->Choose(); return 0;
            case VK_ESCAPE: DestroyWindow(self->wnd_); return 0;
            default: break;
            }
        if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
        if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, EditProc, 1);
        return DefSubclassProc(h, msg, wp, lp);
    }

    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        auto* self = reinterpret_cast<SearchBox*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        switch (msg) {
        case WM_COMMAND:
            if (self && LOWORD(wp) == 1 && HIWORD(wp) == EN_CHANGE) self->Refilter();
            return 0;
        case WM_NOTIFY:
            if (self && reinterpret_cast<NMHDR*>(lp)->idFrom == 2 && reinterpret_cast<NMHDR*>(lp)->code == NM_DBLCLK) self->Choose();
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && self && !self->closing_) PostMessageW(h, WM_CLOSE, 0, 0);   // clicking away closes it
            else if (self) SetFocus(self->edit_);
            return 0;
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY:
            if (self) self->closing_ = true;
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            delete self;
            return 0;
        default: return DefWindowProcW(h, msg, wp, lp);
        }
    }

    static inline SearchBox* instance_ = nullptr;
    fx::PageHost& host_;
    SearchModel model_;
    std::vector<size_t> hits_;
    HWND wnd_ = nullptr, edit_ = nullptr, list_ = nullptr;
    HFONT font_ = nullptr;
    bool closing_ = false;
};

template <class P>
class Owned : public P {
public:
    using P::P;

protected:
    void OnFinalMessage() override { delete this; }
};

HWND gHome = nullptr, gGallery = nullptr;

template <class P>
HWND openOne(fx::PageHost& host, HWND& slot, const wchar_t* title)
{
    if (slot && IsWindow(slot)) {
        host.ActivatePage(slot);
        return slot;
    }
    auto* page = new Owned<P>();
    HWND h = page->Open(host, title);
    if (!h) {
        delete page;
        return nullptr;
    }
    slot = h;
    return h;
}

} // namespace

HWND OpenHomePage(fx::PageHost& host) { return openOne<HomePage>(host, gHome, L"Home"); }
HWND OpenReportGallery(fx::PageHost& host) { return openOne<GalleryPage>(host, gGallery, L"Reports"); }

void ShowCommandSearch(fx::PageHost& host, std::function<bool(WORD)> enabled)
{
    SearchBox::Show(host, std::move(enabled));
}

void NoteRecentPage(WORD command)
{
    if (!command || command == CM_HOME) return;
    gRecent.erase(std::remove(gRecent.begin(), gRecent.end(), command), gRecent.end());
    gRecent.insert(gRecent.begin(), command);
    if (gRecent.size() > 8) gRecent.resize(8);
}

const std::vector<WORD>& RecentPages() { return gRecent; }
void SetRecentPages(std::vector<WORD> commands) { gRecent = std::move(commands); }

const std::vector<std::pair<int, unsigned>>& HomePageAnchors() { return kHomeAnchors; }
const std::vector<std::pair<int, unsigned>>& GalleryPageAnchors() { return kGalleryAnchors; }

int StressHomePages(int refreshes)
{
    class StressWorkspace : public fx::Workspace {
    protected:
        bool OnDestroy() override { return true; }
    };
    auto objects = [] {
        HANDLE self = GetCurrentProcess();
        return static_cast<int>(GetGuiResources(self, GR_GDIOBJECTS) + GetGuiResources(self, GR_USEROBJECTS));
    };
    StressWorkspace w;
    if (!w.CreateWorkspace(hInst, L"GLHomeStress", L"home stress", nullptr, -1)) return -1;
    HWND home = OpenHomePage(w);
    auto* hp = dynamic_cast<HomePage*>(fx::Window::FromHandle(home));
    if (!hp) {
        DestroyWindow(w.hwnd());
        return -1;
    }
    hp->Refresh();   // first: the command links' fonts, the theme
    auto galleryOnce = [&] {
        HWND g = OpenReportGallery(w);
        auto* gp = dynamic_cast<GalleryPage*>(fx::Window::FromHandle(g));
        if (!gp) return false;
        gp->Search("trial");
        gp->Search("");
        w.ClosePage(g);
        return !IsWindow(g);
    };
    if (!galleryOnce()) {
        DestroyWindow(w.hwnd());
        return -1;
    }
    const int before = objects();
    for (int i = 0; i < refreshes; ++i) hp->Refresh();
    for (int i = 0; i < 20; ++i)
        if (!galleryOnce()) {
            DestroyWindow(w.hwnd());
            return -1;
        }
    const int growth = objects() - before;
    w.ClosePage(home);
    DestroyWindow(w.hwnd());
    return growth;
}
