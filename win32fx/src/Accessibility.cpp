//
// Accessibility.cpp - see Accessibility.h.
//
// Naming (spec #8 12): MSAA (and UI Automation's proxy over it) already names
// an edit, combo box or list from the static label just before it in the tab
// order, the dialog manager's rule, which the forms follow. The rest get an
// explicit name, in this order:
//   1. an edit's cue banner ("Filter by code or name");
//   2. an edit right after a named edit is the second part of that field
//      (an account's dept): "<the field's name> dept";
//   3. the nearest label on its left on the same row ("Bank:" beside the
//      bank box, whose label also serves the AP account);
//   4. the group box around it ("The level's commands");
//   5. a list's first column heading: "<heading> list".
//
#include "Accessibility.h"

#include "model/FrameLook.h"

#include <CommCtrl.h>
#include <dwmapi.h>
#include <oleacc.h>
#include <UIAutomation.h>

#include <algorithm>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "oleacc.lib")
#pragma comment(lib, "uiautomationcore.lib")

namespace win32fx {

namespace {

// oleacc.h declares these but no import library defines them (they need initguid.h,
// which would define UI Automation's too): the values from oleacc.h.
constexpr CLSID kAccPropServices = {0xb5f8350b, 0x0548, 0x48b1, {0xa6, 0xee, 0x88, 0xbd, 0x00, 0xb4, 0xa5, 0xe7}};
constexpr MSAAPROPID kPropAccName = {0x608d3df8, 0x8128, 0x4aa7, {0xa4, 0x28, 0xf5, 0x5e, 0x49, 0x26, 0x72, 0x91}};
// UI Automation's Name property (Name_Property_GUID): its Win32 proxies read this
// annotation, not the MSAA one, so Narrator and other UIA clients see the name.
constexpr MSAAPROPID kUiaName = {0xc3a6921b, 0x4a99, 0x44f1, {0xbc, 0xa6, 0x61, 0x18, 0x70, 0x52, 0xc4, 0x31}};

std::wstring classOf(HWND h)
{
    wchar_t c[64] = {};
    GetClassNameW(h, c, 64);
    return c;
}

std::wstring textOf(HWND h)
{
    const int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

// "&Bank:" -> "Bank"
std::wstring labelText(std::wstring s)
{
    s.erase(std::remove(s.begin(), s.end(), L'&'), s.end());
    while (!s.empty() && (s.back() == L':' || s.back() == L' ')) s.pop_back();
    return s;
}

bool visibleStyle(HWND h) { return (GetWindowLongW(h, GWL_STYLE) & WS_VISIBLE) != 0; }

bool isGroupBox(HWND h)
{
    return _wcsicmp(classOf(h).c_str(), L"Button") == 0 && (GetWindowLongW(h, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX;
}

// A control the keyboard can land on.
bool focusable(HWND h)
{
    const std::wstring c = classOf(h);
    if (_wcsicmp(c.c_str(), L"Button") == 0) {
        const LONG t = GetWindowLongW(h, GWL_STYLE) & BS_TYPEMASK;
        return t != BS_GROUPBOX && t != BS_OWNERDRAW;
    }
    return _wcsicmp(c.c_str(), L"Edit") == 0 || _wcsicmp(c.c_str(), L"ComboBox") == 0 || _wcsicmp(c.c_str(), L"ListBox") == 0 ||
           _wcsicmp(c.c_str(), WC_LISTVIEWW) == 0 || _wcsicmp(c.c_str(), DATETIMEPICK_CLASSW) == 0 ||
           _wcsicmp(c.c_str(), WC_TREEVIEWW) == 0 || _wcsicmp(c.c_str(), L"tbSpread") == 0;
}

bool isRadio(HWND h)
{
    const LONG t = GetWindowLongW(h, GWL_STYLE) & BS_TYPEMASK;
    return _wcsicmp(classOf(h).c_str(), L"Button") == 0 && (t == BS_RADIOBUTTON || t == BS_AUTORADIOBUTTON);
}

std::vector<HWND> children(HWND form)
{
    std::vector<HWND> v;
    for (HWND c = GetWindow(form, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) v.push_back(c);
    return v;
}

RECT rectIn(HWND form, HWND h)
{
    RECT r{};
    GetWindowRect(h, &r);
    MapWindowPoints(nullptr, form, reinterpret_cast<POINT*>(&r), 2);
    return r;
}

// The smallest group box around `h`, or null.
HWND groupOf(HWND form, HWND h, const std::vector<HWND>& kids)
{
    const RECT r = rectIn(form, h);
    HWND best = nullptr;
    long bestArea = 0;
    for (HWND g : kids) {
        if (g == h || !isGroupBox(g) || !visibleStyle(g)) continue;
        const RECT gr = rectIn(form, g);
        if (r.left >= gr.left && r.right <= gr.right && r.top >= gr.top && r.bottom <= gr.bottom) {
            const long area = (gr.right - gr.left) * (gr.bottom - gr.top);
            if (!best || area < bestArea) {
                best = g;
                bestArea = area;
            }
        }
    }
    return best;
}

std::wstring derivedName(HWND form, HWND h, const std::vector<HWND>& kids)
{
    const std::wstring c = classOf(h);
    if (_wcsicmp(c.c_str(), L"Edit") == 0) {
        wchar_t cue[256] = {};
        if (SendMessageW(h, EM_GETCUEBANNER, reinterpret_cast<WPARAM>(cue), 256) && cue[0]) return cue;
        HWND prev = GetWindow(h, GW_HWNDPREV);
        if (prev && _wcsicmp(classOf(prev).c_str(), L"Edit") == 0) {
            const std::wstring field = AccessibleName(prev);
            if (!field.empty()) return labelText(field) + L" dept";
        }
    }
    const RECT r = rectIn(form, h);
    HWND label = nullptr;
    long gap = 0;
    for (HWND s : kids) {   // a label on the left, on the same row
        if (_wcsicmp(classOf(s).c_str(), L"Static") != 0 || !visibleStyle(s) || textOf(s).empty()) continue;
        const RECT sr = rectIn(form, s);
        const bool sameRow = sr.top < r.bottom && sr.bottom > r.top;
        if (!sameRow || sr.right > r.left) continue;
        const long d = r.left - sr.right;
        if (!label || d < gap) {
            label = s;
            gap = d;
        }
    }
    if (label) return labelText(textOf(label));
    if (HWND g = groupOf(form, h, kids); g && !textOf(g).empty()) return labelText(textOf(g));
    if (_wcsicmp(c.c_str(), WC_LISTVIEWW) == 0) {
        wchar_t heading[128] = {};
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT;
        col.pszText = heading;
        col.cchTextMax = 128;
        if (ListView_GetColumn(h, 0, &col) && heading[0]) return std::wstring(heading) + L" list";
    }
    return {};
}

} // namespace

// ---- the frame ----------------------------------------------------------------------------

void ApplyFrameLook(HWND frame)
{
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW v{sizeof v};
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll"))
        if (auto f = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(nt, "RtlGetVersion")))) f(&v);
    HIGHCONTRASTW hc{sizeof hc};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0);
    auto setting = [](const wchar_t* name) {   // Settings > Personalization > Colors; 1 when absent
        DWORD value = 1, size = sizeof value;
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", name,
                     RRF_RT_REG_DWORD, nullptr, &value, &size);
        return value != 0;
    };
    const model::FrameLook look = model::ChooseFrameLook(v.dwBuildNumber, (hc.dwFlags & HCF_HIGHCONTRASTON) != 0,
                                                         setting(L"EnableTransparency"), setting(L"AppsUseLightTheme"));
    const BOOL dark = look.darkTitleBar;
    DwmSetWindowAttribute(frame, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
    if (v.dwBuildNumber >= model::kMicaBuild) {   // Windows 10 (Q7): no backdrop attribute at all
        const int backdrop = look.mica ? 2 /* DWMSBT_MAINWINDOW */ : 1 /* DWMSBT_NONE */;
        DwmSetWindowAttribute(frame, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &backdrop, sizeof backdrop);
    }
}

// ---- names --------------------------------------------------------------------------------

std::wstring AccessibleName(HWND control)
{
    IAccessible* acc = nullptr;
    if (FAILED(AccessibleObjectFromWindow(control, static_cast<DWORD>(OBJID_CLIENT), IID_IAccessible, reinterpret_cast<void**>(&acc))) || !acc)
        return {};
    VARIANT self{};
    self.vt = VT_I4;
    self.lVal = CHILDID_SELF;
    BSTR name = nullptr;
    std::wstring out;
    if (SUCCEEDED(acc->get_accName(self, &name)) && name) {
        out = name;
        SysFreeString(name);
    }
    acc->Release();
    while (!out.empty() && out.back() == L' ') out.pop_back();
    return out;
}

bool SetAccessibleName(HWND control, const std::wstring& name)
{
    IAccPropServices* props = nullptr;
    if (FAILED(CoCreateInstance(kAccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_IAccPropServices, reinterpret_cast<void**>(&props))) || !props)
        return false;
    HRESULT hr = props->SetHwndPropStr(control, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, kPropAccName, name.c_str());
    if (SUCCEEDED(hr)) hr = props->SetHwndPropStr(control, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, kUiaName, name.c_str());
    props->Release();
    return SUCCEEDED(hr);
}

void NameUnnamedControls(HWND form)
{
    const std::vector<HWND> kids = children(form);
    // Every focusable control gets the name explicitly, the label-derived one MSAA
    // already gives included: UI Automation's own proxies don't apply that rule.
    for (HWND h : kids) {
        if (!focusable(h)) continue;
        std::wstring name = labelText(AccessibleName(h));
        if (name.empty()) name = derivedName(form, h, kids);
        if (!name.empty()) SetAccessibleName(h, name);
    }
}

void ClearAccessibleNames(HWND form)
{
    IAccPropServices* props = nullptr;
    if (FAILED(CoCreateInstance(kAccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_IAccPropServices, reinterpret_cast<void**>(&props))) || !props)
        return;
    MSAAPROPID names[2] = {kPropAccName, kUiaName};
    for (HWND h : children(form))
        if (focusable(h)) props->ClearHwndProps(h, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, names, 2);
    props->Release();
}

std::vector<HWND> UnnamedControls(HWND form)
{
    std::vector<HWND> out;
    for (HWND h : children(form))
        if (focusable(h) && visibleStyle(h) && AccessibleName(h).empty()) out.push_back(h);
    return out;
}

// ---- the keyboard -------------------------------------------------------------------------

std::vector<HWND> UnreachableControls(HWND form)
{
    std::vector<HWND> out;
    for (HWND h : children(form))
        if (focusable(h) && visibleStyle(h) && !isRadio(h) && !(GetWindowLongW(h, GWL_STYLE) & WS_TABSTOP)) out.push_back(h);
    return out;
}

std::vector<std::pair<HWND, HWND>> TabOrderProblems(HWND form)
{
    const std::vector<HWND> kids = children(form);
    std::vector<HWND> stops;
    for (HWND h : kids)
        if (focusable(h) && visibleStyle(h) && (GetWindowLongW(h, GWL_STYLE) & WS_TABSTOP)) stops.push_back(h);
    std::vector<std::pair<HWND, HWND>> out;
    for (size_t i = 0; i + 1 < stops.size(); ++i) {
        HWND a = stops[i], b = stops[i + 1];
        if (groupOf(form, a, kids) != groupOf(form, b, kids)) continue;   // within a group (or both in none)
        const RECT ra = rectIn(form, a), rb = rectIn(form, b);
        const bool sameRow = rb.top < ra.bottom && rb.bottom > ra.top;
        const bool backwards = sameRow ? rb.left < ra.left : rb.bottom <= ra.top;
        if (backwards) out.push_back({a, b});
    }
    return out;
}

// ---- announcements ------------------------------------------------------------------------

void Announce(HWND hwnd, const std::wstring& text)
{
    IRawElementProviderSimple* provider = nullptr;
    if (FAILED(UiaHostProviderFromHwnd(hwnd, &provider)) || !provider) return;
    BSTR t = SysAllocString(text.c_str()), id = SysAllocString(L"gl.infobar");
    UiaRaiseNotificationEvent(provider, NotificationKind_Other, NotificationProcessing_ImportantMostRecent, t, id);
    SysFreeString(t);
    SysFreeString(id);
    provider->Release();
}

} // namespace win32fx
