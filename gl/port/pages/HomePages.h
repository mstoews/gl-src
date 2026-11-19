//
// HomePages.h - the home page, the report gallery and command search
// (spec #8 4, 5.4, 5.5; plan S8-M5). The logic is HomeModels.h's.
//
//   Home (CM_HOME)              tiles of what needs doing, from the cores,
//                               refreshed by the data-changed broadcast;
//                               recent pages and favourite reports
//   Custom Report (CM_REPORT_LIST)  the report gallery: built-in reports by
//                               menu, then user reports; search, favourites
//                               and recent (GL_UISTATE); Spec #7's New,
//                               Modify, Copy and Delete behind the Report
//                               Writer right. The old list stays as
//                               "Custom Report (classic)".
//   Ctrl+K                      command search: commands (greyed ones left
//                               out), accounts, customers, vendors, vouchers
//
#pragma once

#include "RecordPage.h"   // ClassicCommand
#include "win32fx.h"

#include <functional>
#include <utility>
#include <vector>

HWND OpenHomePage(fx::PageHost& host);
HWND OpenReportGallery(fx::PageHost& host);

// Ctrl+K: the search box over the frame. `enabled` says which commands may run.
void ShowCommandSearch(fx::PageHost& host, std::function<bool(WORD)> enabled);

// The pages opened lately, by command, for the home page (the frame keeps them in GL_UISTATE).
void NoteRecentPage(WORD command);
const std::vector<WORD>& RecentPages();
void SetRecentPages(std::vector<WORD> commands);

const std::vector<std::pair<int, unsigned>>& HomePageAnchors();
const std::vector<std::pair<int, unsigned>>& GalleryPageAnchors();

// /stress: the home page refreshed `refreshes` times; the gallery opened and searched.
int StressHomePages(int refreshes);
