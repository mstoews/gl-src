//
// DataChanged.cpp - see DataChanged.h.
//
#include "DataChanged.h"

#include "win32fx.h"
#include "model/JobState.h"

namespace {

HWND gFrame = nullptr;

} // namespace

void SetDataChangedFrame(HWND frame)
{
    gFrame = frame;
}

void NotifyDataChanged(unsigned groups, HWND from)
{
    if (!gFrame || !IsWindow(gFrame)) return;
    if (auto* w = dynamic_cast<fx::Workspace*>(fx::Window::FromHandle(gFrame))) {
        const std::vector<HWND> pages = w->pages();   // a copy: a page may close while refreshing
        for (HWND p : pages)
            if (IsWindow(p)) SendMessageW(p, kDataChanged, groups, reinterpret_cast<LPARAM>(from));
    }
    SendMessageW(gFrame, kDataChanged, groups, reinterpret_cast<LPARAM>(from));
}

bool SaveWaitsForJob(fx::Page& page)
{
    if (!fx::model::JobGate::Busy()) return false;
    page.ShowInfo(fx::InfoKind::Warning, fx::Utf8ToWide(fx::model::JobGate::Holder()) +
                                             L" is writing to the books: save again when it has finished.");
    return true;
}
