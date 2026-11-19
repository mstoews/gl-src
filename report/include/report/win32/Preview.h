#pragma once
//
// Preview.h - the report preview (spec #3 4.5; plan S3-M1 task 3).
//
// An MDI child per report run, titled with the report's title. Under a
// button strip it shows one page at a time on a grey desk:
//
//   [Print...]  [|<] [<]  3 of 12  [>] [>|]  [Fit width]  [Options...]  [Close]
//
//   - Zoom cycles Fit width (the default) -> Fit page -> 100 %.
//   - Keys: PgUp / PgDn the previous / next page, Home / End the first / last,
//     the arrows scroll, Ctrl+P prints.
//   - The wheel scrolls; when the page fits the window it turns pages.
//   - Options... asks the caller for a rebuilt report (its options dialog);
//     without a rebuild function the button isn't there.
//   - Spec #8 G1: with an options panel instead, the panel sits between the
//     strip and the page, and the button is Refresh (as is Enter in the panel).
//
// The pages are laid out for the session printer (PrinterSetup.h) in the
// report's orientation, and laid out again when that printer changes. Print
// lays out again for the printer chosen in the Print dialog (Print.h).
//
// The window owns itself: it is deleted when its MDI child is destroyed.
//
#include "report/Report.h"

#include "PageHost.h"   // win32fx

#include <functional>
#include <memory>
#include <optional>

namespace rpt::win32 {

// Shows the report's options again and builds it: nullopt when cancelled
// (or when building failed and the error was shown).
using Rebuild = std::function<std::optional<Report>(HWND owner)>;

// A report's options as a panel above its page (spec #8 G1). The preview owns
// it: CreatePanel makes the child window (its height is kept, its width
// follows the preview's); Rebuild reads the panel and builds, nullopt when
// it refused or failed (and said why). A panel that sends WM_COMMAND IDOK
// to its parent (Enter in it) refreshes.
class OptionsPanel {
public:
    virtual ~OptionsPanel() = default;
    virtual HWND CreatePanel(HWND parent) = 0;
    virtual std::optional<Report> Rebuild(HWND owner) = 0;
};

// Opens a preview of `report` in `frame`. Null if the window couldn't be created.
HWND OpenPreview(win32fx::PageHost& host, Report report, Rebuild rebuild = {});
HWND OpenPreview(win32fx::PageHost& host, Report report, std::unique_ptr<OptionsPanel> panel);

// Spec #8 G1: a preview inside another window (the report designer's page):
// a plain child with control ID `id`, the strip without Close. Null if it
// couldn't be created. SetPreviewReport shows another report in it (or in
// any preview), keeping the page number where it can; false if `hwnd` isn't
// a preview.
HWND CreatePreviewChild(HWND parent, int id, Report report);
bool SetPreviewReport(HWND hwnd, Report report);

// For tests and /stress: presses Refresh (an options panel's) or Options...;
// false if `hwnd` isn't a preview or has neither. PreviewPanel: the panel
// window, or null.
bool RefreshPreview(HWND hwnd);
HWND PreviewPanel(HWND hwnd) noexcept;

// A preview for a modal dialog (spec #3 D7, Voucher Entry's Print), where an
// MDI child of the main frame couldn't be used: a frame window of its own,
// owned by `owner`, holding one maximized preview. `owner` is disabled while
// it is open, and the call returns when it is closed (its Close button, or
// the window's). False if it couldn't be created.
bool ShowPreviewWindow(HWND owner, Report report);

// True if `hwnd` is a preview window.
bool IsPreview(HWND hwnd) noexcept;

// If the frame's active MDI child is a preview, prints it (as its Print...
// button does) and returns true. File > Print and the toolbar Print button
// (spec #3 D9).
bool PrintActivePreview(win32fx::PageHost& host);

// For tests and /stress: the page shown (0-based) and the page count of a
// preview window; -1 if `hwnd` isn't one.
int PreviewPage(HWND hwnd) noexcept;
int PreviewPageCount(HWND hwnd) noexcept;
// Presses the Zoom button: Fit width -> Fit page -> 100 % -> Fit width. False if not a preview.
bool CyclePreviewZoom(HWND hwnd);

} // namespace rpt::win32
