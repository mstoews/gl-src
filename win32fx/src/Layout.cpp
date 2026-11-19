//
// Layout.cpp - see Layout.h.
//
#include "Layout.h"

#include <algorithm>

namespace win32fx {

namespace {

unsigned anchorOf(const std::vector<std::pair<int, unsigned>>& anchors, int id, bool* found = nullptr)
{
    for (const auto& [i, a] : anchors)
        if (i == id) {
            if (found) *found = true;
            return a;
        }
    if (found) *found = false;
    return model::kTopLeft;
}

} // namespace

void Layout::Capture(HWND form, const std::vector<std::pair<int, unsigned>>& anchors)
{
    form_ = form;
    items_.clear();
    RECT rc{};
    GetClientRect(form, &rc);
    original_ = {rc.right, rc.bottom};
    for (HWND c = GetWindow(form, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        RECT r{};
        GetWindowRect(c, &r);
        MapWindowPoints(nullptr, form, reinterpret_cast<POINT*>(&r), 2);
        items_.push_back({c, {r.left, r.top, r.right, r.bottom}, anchorOf(anchors, GetDlgCtrlID(c))});
    }
}

void Layout::Apply(int cx, int cy) const
{
    if (!form_ || items_.empty()) return;
    HDWP dwp = BeginDeferWindowPos(static_cast<int>(items_.size()));
    for (const Item& it : items_) {
        if (!IsWindow(it.hwnd)) continue;
        const model::Box b = model::Place(it.box, it.anchors, original_, {cx, cy});
        if (dwp)
            dwp = DeferWindowPos(dwp, it.hwnd, nullptr, b.left, b.top, b.width(), b.height(),
                                 SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (dwp) EndDeferWindowPos(dwp);
}

void Layout::Rescale(UINT oldDpi, UINT newDpi)
{
    if (!oldDpi || oldDpi == newDpi) return;
    auto s = [&](int v) { return MulDiv(v, static_cast<int>(newDpi), static_cast<int>(oldDpi)); };
    original_ = {s(original_.cx), s(original_.cy)};
    for (Item& it : items_) it.box = {s(it.box.left), s(it.box.top), s(it.box.right), s(it.box.bottom)};
}

std::vector<int> Layout::Unanchored(HWND form, const std::vector<std::pair<int, unsigned>>& anchors)
{
    std::vector<int> out;
    for (HWND c = GetWindow(form, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        const int id = GetDlgCtrlID(c);
        if (id <= 0 || id == 0xFFFF) continue;   // labels without an ID (-1) stay where the template put them
        bool found = false;
        anchorOf(anchors, id, &found);
        if (!found) out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace win32fx
