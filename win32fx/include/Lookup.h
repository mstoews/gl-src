//
// Lookup.h - an edit that finds codes as you type (spec #8 6.3; plan S8-M1
// task 4).
//
// It attaches to an EDIT on a page and, as the user types, lists the
// LookupModel's matches in a popup under it: Down and Up move in the list,
// Enter (or a click) picks, Esc closes it, F4 shows every item. Picking
// puts the item's code in the edit and calls `onPick` (the page fills the
// dept and the name beside it). A code typed in full and Tab works with no
// list, as before. The page forwards the edit's EN_CHANGE (TextChanged) and
// EN_KILLFOCUS (FocusLost).
//
#pragma once

#include "model/LookupModel.h"

#include <Windows.h>

#include <functional>
#include <vector>

namespace win32fx {

class Lookup {
public:
    using Pick = std::function<void(const model::LookupItem&)>;

    Lookup() = default;
    Lookup(const Lookup&) = delete;
    Lookup& operator=(const Lookup&) = delete;
    ~Lookup();

    void Attach(HWND edit, const model::LookupModel* model, Pick onPick);
    HWND edit() const noexcept { return edit_; }

    void TextChanged();   // EN_CHANGE: refilter and show the matches (not while the page fills it)
    void FocusLost();     // EN_KILLFOCUS: close the list
    void Close();
    bool open() const noexcept { return popup_ && IsWindowVisible(popup_); }

    // Tell it the page is setting the text, so the change isn't typing.
    void SetFilling(bool filling) noexcept { filling_ = filling; }

private:
    static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref);
    static LRESULT CALLBACK ListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref);
    void Show(bool all);
    void Move(int by);
    void PickRow(int row);

    HWND edit_ = nullptr;
    HWND popup_ = nullptr;   // a ListView, WS_POPUP, never activated
    const model::LookupModel* model_ = nullptr;
    Pick onPick_;
    std::vector<size_t> shown_;   // popup row -> model item
    bool filling_ = false;
};

} // namespace win32fx
