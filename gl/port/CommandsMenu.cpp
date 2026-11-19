//
// CommandsMenu.cpp - see CommandsMenu.h.
//
#include "CommandsMenu.h"

#include "Commands.h"

#include <vector>

HMENU BuildCommandMenu()
{
    HMENU bar = CreateMenu();
    if (!bar) return nullptr;
    std::vector<HMENU> open{bar};   // open[d]: the menu rows at depth d go into
    for (const cmd::MenuRow& r : cmd::MenuRows()) {
        if (r.depth < 0 || static_cast<size_t>(r.depth) >= open.size()) continue;   // can't happen: tested
        open.resize(static_cast<size_t>(r.depth) + 1);
        HMENU into = open.back();
        switch (r.kind) {
        case cmd::Kind::Popup: {
            HMENU sub = CreatePopupMenu();
            AppendMenuW(into, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(sub), r.text);
            open.push_back(sub);
            break;
        }
        case cmd::Kind::Separator: AppendMenuW(into, MF_SEPARATOR, 0, nullptr); break;
        case cmd::Kind::Item: {
            UINT flags = MF_STRING;
            if (r.flags & cmd::kGrayed) flags |= MF_GRAYED;
            if (r.flags & cmd::kChecked) flags |= MF_CHECKED;
            if (r.flags & cmd::kBarBreak) flags |= MF_MENUBARBREAK;
            if (r.flags & cmd::kBreak) flags |= MF_MENUBREAK;
            if (r.flags & cmd::kHelp) flags |= MF_HELP;
            AppendMenuW(into, flags, r.id, r.text);
            break;
        }
        }
    }
    return bar;
}

namespace {

std::wstring itemText(HMENU m, int i)
{
    wchar_t buf[256] = {};
    GetMenuStringW(m, static_cast<UINT>(i), buf, 256, MF_BYPOSITION);
    return buf;
}

bool compare(HMENU a, HMENU b, const std::wstring& where, std::wstring& why)
{
    const int n = GetMenuItemCount(a), m = GetMenuItemCount(b);
    if (n != m) {
        why = where + L": " + std::to_wstring(n) + L" items, not " + std::to_wstring(m);
        return false;
    }
    for (int i = 0; i < n; ++i) {
        MENUITEMINFOW x{}, y{};
        x.cbSize = y.cbSize = sizeof x;
        x.fMask = y.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_SUBMENU;
        GetMenuItemInfoW(a, static_cast<UINT>(i), TRUE, &x);
        GetMenuItemInfoW(b, static_cast<UINT>(i), TRUE, &y);
        const std::wstring text = itemText(a, i), other = itemText(b, i);
        const std::wstring here = where + L" > " + (text.empty() ? L"#" + std::to_wstring(i) : text);
        const UINT stateMask = MFS_GRAYED | MFS_CHECKED;
        if (text != other || (x.fType & MFT_SEPARATOR) != (y.fType & MFT_SEPARATOR) ||
            (x.fState & stateMask) != (y.fState & stateMask) || (!x.hSubMenu) != (!y.hSubMenu) ||
            (!x.hSubMenu && x.wID != y.wID)) {
            why = here + L": differs (\"" + text + L"\" " + std::to_wstring(x.wID) + L" / \"" + other + L"\" " +
                  std::to_wstring(y.wID) + L")";
            return false;
        }
        if (x.hSubMenu && !compare(x.hSubMenu, y.hSubMenu, here, why)) return false;
    }
    return true;
}

} // namespace

bool SameMenus(HMENU a, HMENU b, std::wstring& why)
{
    if (!a || !b) {
        why = L"a menu is missing";
        return false;
    }
    return compare(a, b, L"menu", why);
}
