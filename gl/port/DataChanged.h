//
// DataChanged.h - the data-changed broadcast (spec #8 9.3; plan S8-M4 task 5).
//
// After a save or a job, NotifyDataChanged(groups) tells every open page
// which groups of tables changed (pages/TaskModels.h data_group): the page
// gets kDataChanged (wParam: the groups) and refreshes what it shows; a page
// with unsaved changes says the record changed instead of reloading it. The
// frame then updates the status bar's waiting counts.
//
#pragma once

#include <Windows.h>

constexpr UINT kDataChanged = WM_APP + 70;   // wParam: data_group bits

// Sends kDataChanged to every page of the main window's workspace (lParam: `from`,
// the page that saved, which ignores it), and to the frame.
void NotifyDataChanged(unsigned groups, HWND from = nullptr);

namespace win32fx {
class Page;
}
// Spec #8 9.1, R5: while a job writes, a page's save waits. True (and the page's
// information bar says which job) when the save must wait; the user saves again after.
bool SaveWaitsForJob(win32fx::Page& page);

// The main window, which owns the workspace (set by ManilaGL).
void SetDataChangedFrame(HWND frame);
