//
// EntryGrid.h - the entry grid (spec #8 6.4; plan S8-M3 task 2): a report
// ListView (LVS_OWNERDATA, on a page's form) showing a model::EntryGridModel.
//
// The keys go to the model (EntryGridModel.h has the table). Editing puts an
// edit over the current cell, with a Lookup when the column has one; the edit
// is made for each edit and destroyed after it. The current cell is drawn
// highlighted (custom draw), its row lightly.
//
// The form forwards the list's notifications: OnNotify(nm, result) from its
// WM_NOTIFY. The grid wants every key (DLGC_WANTALLKEYS) except with Ctrl
// held, so Ctrl+Tab leaves it as Tab leaves any other control.
//
// Deviation from the plan's "fx::Grid in report mode": fx::Grid (the
// tbSpread port) edits cells by its own rules; a plain ListView lets the
// model own every key.
//
#pragma once

#include "Lookup.h"
#include "model/EntryGridModel.h"

#include <Windows.h>

#include <functional>
#include <map>
#include <memory>

namespace win32fx {

class EntryGrid {
public:
    EntryGrid() = default;
    ~EntryGrid();
    EntryGrid(const EntryGrid&) = delete;
    EntryGrid& operator=(const EntryGrid&) = delete;

    // `list` is a report ListView with LVS_OWNERDATA; its columns are made from the model's.
    void Attach(HWND list, model::EntryGridModel* model);
    void Detach();
    HWND list() const noexcept { return list_; }
    model::EntryGridModel* model() const noexcept { return model_; }

    void SetLookup(int col, const model::LookupModel* lookup) { lookups_[col] = lookup; }

    // The rows or the current cell changed outside the grid: show them.
    void Refresh();
    bool OnNotify(NMHDR* nm, LRESULT& result);

    bool editing() const noexcept { return edit_ != nullptr; }
    // A key, as if typed in the grid (the /stress test page; the drives).
    model::GridAction Press(model::GridKey key);
    // Text typed into the current cell, as from the keyboard: the editor opens over it (/stress).
    void Type(const std::string& utf8);

    // After a key or a click did something (Refused: model()->message() says why).
    std::function<void(model::GridAction)> OnAction;
    // A lookup's item was picked in (row, col): the page may fill other cells.
    std::function<void(int row, int col, const model::LookupItem& item)> OnPick;

private:
    static LRESULT CALLBACK ListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref);
    static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref);
    bool ListKey(WPARAM vk);
    model::GridAction Apply(model::GridKey key);
    void ShowEditor(const wchar_t* firstChar);
    void HideEditor();
    void SyncEditText();
    void Done(model::GridAction a);

    HWND list_ = nullptr, edit_ = nullptr;
    model::EntryGridModel* model_ = nullptr;
    std::map<int, const model::LookupModel*> lookups_;
    std::unique_ptr<Lookup> lookup_;
    bool hiding_ = false;
};

} // namespace win32fx
