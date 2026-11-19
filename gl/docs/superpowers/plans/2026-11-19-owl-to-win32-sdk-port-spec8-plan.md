# Spec #8 — Implementation Plan: A Modern Workspace

**Spec reference:** `gl/docs/superpowers/specs/2026-11-19-owl-to-win32-sdk-port-spec8-design.md`
**Date:** 2026-11-19
**Plan status:** In progress (S8-M0 to M3 built)

**Paths:** relative to the repository root. The shell today is `gl/port/ManilaGL.{h,cpp}` on `win32fx/include/MdiFrame.h`, with the menu, toolbar and status bar from `gl/res/gl.rc` (made by `gl/res/tools/prune_rc.py`) and `win32fx/include/Toolbar.h` / `StatusBar.h`. Screens are `MainDialog` subclasses in `gl/port/`. New framework code goes in `win32fx/` (`Workspace`, `Page`, `Layout`, the controls), the command table in `gl/port/Commands.{h,cpp}`, pages in `gl/port/pages/`, and page templates in `gl/res/port.rc` with their IDs in `gl/port/PortIds.h`.

This plan carries out the Spec #8 design. If a milestone finds that the spec is wrong, **stop, fix the spec, then resume**.

**Read first, as in Specs #2–#7.** Each milestone starts by re-reading what it replaces: the dialogs and their `MainDialog` code for the screens it moves, and the cores those screens call. A page does what its dialog did, through the same core functions, in the same order. Where the dialog checks something in its own code, the check moves to the page's validators unchanged, or into the core with a test.

**Open questions.** Q1–Q8 (spec §18) stay as the spec assumes until answered:
- the menu bar stays (Q1);
- the finance team sees the mockup before M1 (Q2);
- Enter moves to the next field in forms and grids; Ctrl+Enter saves (Q3);
- old dialogs stay reachable until their milestone's acceptance (Q4);
- the home page shows spec §5.5's list (Q5);
- the database moves to WAL mode (Q6);
- Windows 10 is supported, without Mica (Q7);
- one company at a time (Q8).

Each milestone notes where an answer would change it.

**Where it runs.** As Specs #5–#7: Windows 11 with Visual Studio 18 (v145), each milestone built, tested and driven in the app as it is written. New portable files join `tools/linux/build_tests.sh`.

**Split each part in two.** Everything that can be decided without a window goes in a portable model with `gl_tests` cases: the command table and rights, anchor arithmetic, validators, lookup matching, the entry grid's cells and keys, UI state, job state. The Win32 half is thin: it shows the model and turns messages into model calls. Spec #3 did the same with the report engine, and it is what kept seven specs testable.

**Reused, not rewritten.** The cores (`Vouchers`, `Accounts`, `Ap`, `Ar`, `Assets`, `MasterTables`, `ReportWriter`, the report engine) are called as the dialogs call them. `fx::Grid`, `fx::CommandBar`, `fx::StatusBar`, the Fluent icons and the UI refresh's DPI code are built on, not replaced.

---

## Milestone summary

| # | Milestone | Size | Depends on | Critical path? |
|---|---|---|---|---|
| 0 | The workspace replacing MDI; the command table; rights by command; old screens modal over it; previews and the voucher list as tabs | L | none | yes |
| 1 | The page framework, layout, controls, information bar, task dialogs, `GL_UISTATE`; the Customers page | L | M0 | yes |
| 2 | The other list-and-detail pages; User Access | M | M1 | no |
| 3 | The entry grid; the GL voucher page, then AP, AR and receipts | L | M1 | yes |
| 4 | WAL, background jobs, data-changed refresh; the task pages | L | M1 | yes |
| 5 | The report gallery, options panels, preview and designer pages; the home page; command search | M | M1, M4 | no |
| 6 | Windows 11 touches, accessibility, keyboard audit; legacy removal | M | M2–M5 | yes |
| 7 | Acceptance sheet, redesign columns, walkthrough, sign-off | S | all | yes |

S is less than a day, M is 1–3 days, L is 3–6 days. **Parallel work:** M2, M3 and M4 each need only M1 and touch different screens. M2 can run beside M3. M4's task pages can start beside them, but its WAL and job runner (tasks 2–4) should land before M3's posting from the voucher page. M5 needs M4's refresh broadcast for the home page.

**The gate before M1** (Q2): the finance team sees the mockup and M0's build. Their answers to Q1, Q3 and Q5 are folded into the spec before M1 starts.

---

## Milestone 0 — The workspace and the command table

> **As built (2026-11-19; built and run on Windows 11, VS 18, v145).**
> - **Read** (task 1):
>   - **What took the MDI frame:** `OpenPreview` and `PrintActivePreview`, `RunReport` and every `Run*` report function, `OpenVoucherBrowser`, `StressFrame`, and the example program.
>   - **The GLE menu swap was dead:** nothing calls `UseGleMenu`, since only the spreadsheet window (never ported) asked for it. It is removed rather than carried over.
>   - **The globals the cores read** (R2): `cb`, `szUser`, `szAccessLevel`, `szMsg`, `CurDir`, `bMenuSetup`. Pages must read them only; no page writes them yet.
> - **`win32fx::PageHost`** (task 2, `PageHost.{h,cpp}`):
>   - **The interface:** `hostWindow`, `CreatePage`, `activePage`, `ActivatePage`, `ClosePage`, `SetPageTitle` and `OnPageActivate`.
>   - **`HostedPage`** replaces `MdiChild`, which stays as an alias. Its default procedure is `DefMDIChildProc` only under an MDI host.
>   - **Who takes it:** the preview, `RunReport`, the report functions and the voucher list now take a `PageHost&`.
>   - **`MdiFrame` implements it too.** The modal preview window (`ShowPreviewWindow`) and the example program still use MDI, and both kept working through the change (P1).
> - **`win32fx::Workspace`** (task 3, `Workspace.{h,cpp}`, `Dock.h`):
>   - **The window:** the docked bars, a TreeView navigation pane (230 px at 96 DPI, Explorer theme), and a Tab control over the page area.
>   - **Pages** are its child windows: only the active one is shown, and the tab strip is hidden while no page is open. A page's tab goes when the page is destroyed, however that happens (`WM_PARENTNOTIFY`).
>   - **Keys** go through a new message-loop key filter (`SetKeyFilter`): Ctrl+Tab and Ctrl+Shift+Tab, Ctrl+W and Ctrl+F4, and Ctrl+Shift+N for the navigation pane.
>   - **The tab context menu:** Close and Close others.
>   - **The legacy Window menu:**
>     - it lists the open pages, as MDI listed children;
>     - Close Children closes them;
>     - Tile, Cascade and Arrange Icons have nothing to arrange and are greyed.
> - **The command table** (task 4):
>   - **Generated:** `gl/res/tools/make_commands.py` writes `gl/port/CommandsTable.inc` from MANILA in `gl.rc`: 165 rows, 116 menu items, 114 commands. The Customer and Vendor Inquiry Reports are also on the Reporting menu as Open Receivables and Payables Detail.
>   - **Portable `Commands.{h,cpp}`:** the rows, `PathOf`, `PlainText`, `RightOf` (the follow-on rules: Statement Layout, the Recover items, the Voucher List), and `cmd::Rights`. `Rights` reads `accessm` in the "menu" order with `Data4`, as `ApplyMenuAccess` did, deleted rows included.
>   - **The Win32 half, `CommandsMenu.{h,cpp}`:** builds the menu bar, and compares two menus item by item.
>   - **Deviation:** the CSV of hand-added fields (icon, module, kind) waits until a milestone needs them. The navigation pane uses the menu's popups as its groups.
> - **`ManilaGL`** (task 5): a `Workspace`.
>   - **The menu bar** is built from the table (`App.cpp`).
>   - **The navigation pane** holds every popup and command but the Window menu.
>   - **Rights:** `ApplyRights` greys the menu, the toolbar (new: `CommandBar::EnableCommand`) and the navigation pane through `MayRun`. A command the rights deny doesn't run even if posted or keyed, where the old frame only greyed the menu. Rights reload after Restore. `ApplyMenuAccess` is gone.
>   - **GLSUB:** a right-click on the empty page area opens it.
>   - **Unchanged:** `OnCommand`'s routing.
> - **`/checkres`** (task 6):
>   - the menu built from the table must equal MANILA loaded from the resources, item by item ("114 commands");
>   - a hidden workspace opens two pages, switches, fills the page area, lists the pages in the Window menu, and closes them.
>
>   `/stress`'s report step now runs on a hidden workspace, so every preview opens and closes as a tab.
> - **Tests** (task 7, `gl_tests/test_commands.cpp`, 3 cases):
>   - **The table against the menu:** its top-level menus, item and command counts, paths and depths.
>   - **The rights against the legacy walk:** the walk replayed (rows beside the code, then the follow-on rules), compared with `MayRun` for every command. It covers levels A and B, no login, all-off, a code that stops halfway, and one longer than the rows. B loses every Maintenance command.
>   - **Edge cases:** rights with no `accessm`, and a command with two rows.
>
>   `gl_tests` 182, `report_tests` 19, `cb4sqlite_tests` 77.
> - **Checked:**
>   - Debug and Release `/W4 /WX` clean; the three suites pass in both; `/checkres` 0 in both; `/stress` 0 (216 s).
>   - Driven in the app on a scratch copy, by messages to its own windows:
>     - **The workspace:** it opens with the navigation pane and no tabs. Enter on Transactions > Voucher List opened it as a tab. Trial Balance by Dept. and the Chart of Accounts report opened as tabs 2 and 3.
>     - **The Window menu** listed the three. Its first entry made tab 1 active, and Close Children closed all three.
>     - **Every command:** each of the 114 was posted from the frame, and whatever opened was closed (Exit skipped). Only the Window menu's four commands opened nothing, with no pages open.
>     - **The message boxes:** eight opened one, all the program's own: six "not part of Spec #1", Reindex's note, and Year End's question (answered No).
>     - **The clerk** (level B, logged in on the scratch copy): Maintenance greyed, Accounts not, and a posted Reindex ran nothing.
>
>     Screenshots checked the workspace empty, with tabs, and as the clerk.
> - **Found while checking:**
>   - Git Bash rewrote `/checkres /quiet` into paths, so the first bash-run results meant nothing; the checks were rerun from PowerShell.
>   - The first `/checkres` run failed its own switching check. `IsWindowVisible` is false for children of the hidden test workspace; the check now reads `WS_VISIBLE`.
>   - Smart App Control blocked the new unsigned builds until the user turned it off.
> - **Not done here, as planned:** pinned tabs and remembered window state (M1, with `GL_UISTATE`); the command search (M5).

**Goal:** the program runs in one window with a navigation pane and tabs; every command works as before; nothing about the screens has changed yet.

### Tasks

1. **Read:**
   - `ManilaGL.{h,cpp}`: `OnCommand`'s routing, `NotPorted`, `RunReportList`, `MenuText`, `WM_INITMENUPOPUP`'s greying (Statement Layout and the Recover items follow other items' rights), the GLE menu swapped in for an MDI child (`HGLExMenu`) and the GLSUB right-click popup.
   - `win32fx/MdiFrame.h`, `Window.h`, `Toolbar.h`, `StatusBar.h`.
   - The MDI dependents: `report/win32/Preview.h` (`OpenPreview(win32fx::MdiFrame&, …)`), `reports/RunReport.{h,cpp}` (`RunReport(fx::MdiFrame&, …)`, `StressFrame`), `VoucherBrowser.{h,cpp}`.
   - `Login.cpp` (`ApplyMenuAccess`, `AccessMenus`), and `prune_rc.py`'s MANILA menu.
   - The globals in `Globals.h`, and which cores read them (spec R2): write the list into this note.
2. **`win32fx::PageHost`:** a small interface (`OpenPage(std::unique_ptr<Page>)`, `Activate`, `Close`, `hwnd()`), which `Preview`, `RunReport` and `VoucherBrowser` take in place of `MdiFrame&`. `MdiFrame` implements it first, so the change is proved before the workspace exists. Until M1's `fx::Page`, a "page" here is any child window with a title.
3. **`win32fx::Workspace`** (`Workspace.{h,cpp}`), spec §4. A top-level window with:
   - the menu bar;
   - the navigation pane (TreeView), with collapse;
   - the tab strip (Tab control) over a page area;
   - the command bar (`fx::CommandBar`);
   - the status bar (`fx::StatusBar`, with the waiting counts left at zero until M4).

   It implements `PageHost`. Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+W and Ctrl+F4 move between and close tabs. The tab context menu gets Close and Close others; Pin comes in M1 with `GL_UISTATE`. Window placement is kept in `GL.INI` as today until M1.
4. **The command table** (`gl/port/Commands.{h,cpp}`), spec §8, in two halves:
   - **Portable:** the rows (ID, label, icon, shortcut, help string ID, module, kind, parent menu path) and the rights lookup. `RightsIndex(code)` maps each ID to its `accessm` row's place in COUNT order. `MayRun(index, menuCode, id)` returns '1' → false, no row or a short code → true, as `ApplyMenuAccess` does. The follow-on rules move in too: Statement Layout follows Account Add, and the Recover items follow Recover General Ledger Voucher.
   - **Generated:** `prune_rc.py` writes the rows from the MANILA menu (`gl/port/CommandsTable.inc`), so the table starts as exactly today's menu. Hand-added fields (icon, module, kind) come from a small CSV beside the script.
   - **Win32:** build the menu bar (`CreateMenu` and `AppendMenuW`, in the same order and with the same IDs), the toolbar's 12 buttons and the navigation pane, and grey every place through `MayRun` on `WM_INITMENUPOPUP` and on page activation.
5. **`ManilaGL` on the workspace:**
   - **Routing:** `OnCommand` is unchanged; only its base class and the menu's source change.
   - **Old screens:** `DoModal` gets the workspace as owner.
   - **The GLE menu:** its items become the active page's commands, if a voucher-list page asks for them; otherwise nothing.
   - **GLSUB:** stays the page area's right-click menu.
   - **Previews and the voucher list:** open as tabs through `PageHost`.
6. **`/checkres` and `/stress`:** the workspace is created and laid out at 100 % and 150 %. Stress opens and closes 20 preview tabs and the voucher-list tab, and cycles every tab, checking GDI and USER object counts. `StressFrame` becomes a hidden workspace.
7. **Tests** (`gl_tests/test_commands.cpp`):
   - **The table against the menu:** the same IDs, labels and order as `gl.rc`'s MANILA menu.
   - **The rights:** on the fixture, `MayRun` gives levels A and B exactly the greyed set `ApplyMenuAccess` gives today, for every ID. A code shorter than the rows, and `accessm` missing, grant everything.
   - **The follow-on rules.**
8. **The drive scripts** (scratch helpers): pages are found as children of the workspace by their tab text, not as top-level windows. Each milestone's drive uses these helpers.

### Exit criteria

- Every one of the 116 menu commands runs from the menu as before (a drive posts each non-destructive one and checks the window that opens; the destructive ones are checked to open their confirmation and cancelled).
- The Spec #1–#7 sheets' steps that open screens still work, the screens now modal over the workspace.
- `/checkres` and `/stress` exit 0. Rights tests pass.

**If Q1 says no menu bar,** task 4 builds the navigation pane and search only, and keeps accelerators for every menu shortcut.

---

## Milestone 1 — Pages, layout and controls; the Customers page

> **As built (2026-11-19; built and run on Windows 11, VS 18, v145).** Started before the finance team's review (the user's choice); Q1, Q3 and Q5 stay as the spec assumes.
> - **Portable models** (`win32fx/include/model`, `win32fx/src/model`, in namespace `win32fx::model`, aliased `fx::model`), compiled into `gl_tests` too:
>   - `Anchors.h`: `Place` and `Clamp`, so a page is never laid out below its template's size;
>   - `Validate.{h,cpp}`: `Required`, `MaxLength` (code points), `WholeNumber`, `Amount`, `Date` and `Exists`, plus `ParseAmount` and `FormatAmount`;
>   - `LookupModel.{h,cpp}`: matches the code exactly, then a code prefix, then a word of the name, then anywhere in it, with ties in order.
> - **`fx::Page`** (task 2):
>   - **The window:** a `HostedPage` holding an `InfoBar` and its form. The form is a `WS_CHILD | DS_CONTROL` template made with `CreateDialogParam` and registered with the message loop, so Tab and access keys work.
>   - **Layout:** laid out by anchors, scrolling below the template's size.
>   - **Keys:** Enter moves to the next field, Esc does nothing, Ctrl+S or Ctrl+Enter saves, and Ctrl+N starts a new record. The workspace's key filter passes them on.
>   - **Changes:** `EN_CHANGE` marks the page dirty (not while it is `Filling`), and its tab shows "•". `CanClose` asks Save, Don't save or Cancel in a task dialog, and the workspace asks before closing a tab, closing all pages, or the program.
>   - **Field checks:** validators run on `EN_KILLFOCUS`. `MarkField` shows a balloon tip and turns the label red. The field helpers use the same win32fx string functions as `MainDialog`.
> - **The controls** (task 4): `fx::Layout` (with `Unanchored` for `/checkres`), `fx::Lookup` (a never-activated popup ListView under the edit: Down, Up, Enter, Esc and F4, picked without taking the focus), `fx::InfoBar` (information, success, warning and error; it raises a live-region event), `fx::Ask` (`TaskDialogIndirect`), and `fx::DateField` (yyyymmdd over the date picker). `CommandBar::EnableCommand` came in M0.
> - **`GL_UISTATE`** (task 5, `UiState.{h,cpp}`): user, key and a memo value, with NOLOGIN stored blank.
>   - **What's kept:** `ManilaGL` saves the window placement, the navigation pane's visibility, the pinned tabs (by command) and the active tab when it closes, and restores them after the login.
>   - **Pin** is on the tab's context menu, and needs the page to know its command (`HostedPage::commandId`).
> - **The Customers page** (task 6, `pages/CustomersPage.{h,cpp}`, `IDD_PG_CUSTOMERS`):
>   - **The logic** is the portable `pages/CustomersModel.{h,cpp}`, through Ar.h's functions as the dialog uses them. A new customer gets the default accounts (spec #6 D12); Save checks with `CheckCustomer` and refuses a taken number; Delete refuses a customer in use.
>   - **The list:** virtual, with number, name and owed, filtered as you type.
>   - **The fields:** customer, address, ship-to, terms and VAT (contact and VAT No. among them), and the accounts as lookups over the chart, with the dept and the account's name beside each.
>   - **Open items:** the customer's, in a list.
>   - **What changed:** no "already added, modify?" (D9), and a customer's number can't change once saved.
>   - **Where it opens:** Accounts > Sales Customers (4059) opens the page; the old dialog stays as "Sales Customers (classic)" (`CM_AR_CUSTOMERS_CLASSIC`, following 4059's right) in the navigation pane.
> - **Deviations:**
>   - The page's New, Save and Delete are buttons on its form, not in the workspace's command bar.
>   - The list's width is fixed (no splitter yet), and the open items stretch.
>   - The history is the open items only, not a sales-lines tab.
>   - Closing the program asks each dirty page in turn rather than listing them once.
>   - `genproj.py` now picks up `gl/port/pages`.
> - **`/checkres`, `/stress`** (task 7):
>   - `/checkres` loads each page form as a child, checks its text fit and that every control has an anchor. Text fit runs at the system DPI, as for the dialogs.
>   - `/stress` opens the Customers page in a hidden workspace 25 times: filter, select two customers, close.
> - **Tests** (task 8, `gl_tests/test_ui.cpp`, 5 cases):
>   - anchors; validators and amounts; lookup ranking; `GL_UISTATE`;
>   - **criterion 1 of the Spec #6 sheet** through `CustomersModel`: C004 refused for its sales account, saved with dept 20, VAT 10 and terms 15, changed to terms 45 with no question; a taken number refused; C001 refused with "sales voucher 1 is theirs"; C004 deleted; C999 not found.
>
>   `gl_tests` 187.
> - **Checked:**
>   - Debug and Release `/W4 /WX` clean; the suites pass in both; `/checkres` 0 in both; `/stress` 0 (209 s).
>   - Driven in the app on a scratch copy, by messages to its own windows:
>     - **Start:** the pinned Customers page opened by itself.
>     - **The list:** 3 customers; "reyes" filtered it to 1.
>     - **C004:** refused with "Not an account in the chart." in the information bar and Sales marked. Dept 20 named the account (SALES REVENUE - GENERAL) and it saved (`ar_cust`: 410001-20, terms 15, VAT 10). Terms 45 saved with nothing asked.
>     - **Delete:** asked in a task dialog and deleted C004. C001 showed its open item S-1003 and was refused with the sheet's wording.
>     - **Unsaved changes:** an edited name marked the tab "•"; Close Children asked, and Don't save closed the page with `ar_cust` unchanged.
>     - **Classic:** "Sales Customers (classic)" opened dialog 1052.
>     - **State, over three runs:** the program created `GL_UISTATE` and saved the window maximized. A pin added between runs reopened the page maximized, and `active` and `pins` were saved.
>
>     Screenshots checked the refused save (the marked label, the bar) and C001's refusal with its open items.
> - **Not driven:**
>   - typing into a lookup to open its list, and the keys Enter-to-next-field and Ctrl+S (the drive sends no keystrokes);
>   - Pin from the tab menu (the restore was checked by seeding the pin);
>   - `DateField`, which no M1 page uses.
> - **Seen once, not reproduced:** in the first full run a box titled "Takeda Chemical Industries, Ltd." (the program's About box, dialog 1008) was on screen after the first refused save. Two reruns of the same steps, one starting exactly as that run did, opened no window. The screenshot copies the screen, so it may not have been this process's.

**Goal:** the page pattern exists, and one list-and-detail screen, Customers, uses it end to end.

### Tasks

1. **Read:**
   - `MainDialog.{h,cpp}` (the field helpers, `Cancel`'s `closeAll`) and `ArCustomers.cpp`, the Spec #6 screen with its Options dialog, its checks and its messages.
   - `Ar.h`'s customer functions (`LoadCustomers`, `FindCustomer`, `CheckCustomer`, `SaveCustomer`, `DeleteCustomer`).
   - The Spec #6 sheet's criterion 1.
2. **`fx::Page`** (`win32fx/Page.{h,cpp}`), spec §5:
   - **What it is:** a modeless child created from a `WS_CHILD | DS_CONTROL` template, with `IsDirty`, `Save`, `Revert`, `CanClose`, `Commands()` (for the command bar) and `Title()`.
   - **Field helpers:** the same UTF-8 `GetField` and `SetField` as `MainDialog` (shared code, not copied).
   - **Rules:** no `closeAll`. A page's `Data4` objects are members, closed in its destructor (D7).
   - **The workspace:** asks `CanClose` before closing a tab or the program, and lists the dirty pages once on exit.
3. **`fx::Layout`** (`win32fx/Layout.{h,cpp}`), spec §10:
   - **Portable:** the anchor arithmetic, `Place(original, anchors, oldParent, newParent) → rect`.
   - **Win32:** `DeferWindowPos` on `WM_SIZE`, a page's minimum size and scrolling below it, and a splitter control for list and detail.
   - **The anchors:** a table per page in its `.cpp`, keyed by control ID.
4. **Controls** (spec §6), each with a portable model where there is logic:
   - **Validators** (`fx::Validator`): required, length, number range, amount, date in an open period, a code that exists. A validator runs on leaving the field and on Save, and marks the field with `EM_SHOWBALLOONTIP` and its label. The model is portable; the marking is Win32.
   - **Amount fields:** parse and format with the cores' parsers (`rpt::ToCents`, `FormatAmount`), right-aligned, formatted on leaving.
   - **`fx::DateField`:** `DATETIMEPICK_CLASS` with `DTS_SHORTDATECENTURYFORMAT`, and `DTS_SHOWNONE` for dates that can be blank. Its `Get` and `Set` use yyyymmdd, as `GetDate` does.
   - **`fx::Lookup`:**
     - **The model:** portable matching, with codes by prefix, names by word prefix, then substring, ranked, and a limit.
     - **The control:** an edit with a popup ListView. F4 shows all; a code typed in full and Tab works with no list. It shows the code with its name beside it.
     - **Sources:** accounts with depts, customers, vendors, med reps, products, doctors, banks, periods. Each is a function over the cores' loaders.
   - **`fx::InfoBar`:** a row with an icon, text and an optional `WC_LINK`, in the system's info, warning and error colours.
   - **`fx::Ask`:** `TaskDialogIndirect` with a main instruction, content, named buttons (the safe one the default) and command links.
   - **Keys** (Q3): Enter moves to the next field (`WM_NEXTDLGCTL`) in a form; Ctrl+Enter saves. The keys are one switch, so Q3's answer is a one-place change.
5. **`GL_UISTATE`** (spec §10, D10), portable:
   - **The table:** `EnsureUiStateTable`, with user, key (40) and value (memo).
   - **The functions:** `GetUiState` and `SetUiState`. NOLOGIN is stored as the blank user, as `GL_RPTDEF` does.
   - **The workspace moves to it:** window placement, the navigation pane's width and collapse, pinned tabs (tab Pin arrives here), and the active tab. `GL.INI`'s window positions are no longer read.
6. **The Customers page** (`gl/port/pages/CustomersPage.{h,cpp}`, `IDD_PG_CUSTOMERS`), spec §5.1:
   - **The list:** virtual (`LVS_OWNERDATA`), with code, short name, name and owed, and a filter box.
   - **The detail:** groups for name and address, ship-to, accounts (AR, sales, VAT as lookups), and terms and VAT. The Options dialog's fields join the form.
   - **The commands:** New, Save and Delete.
   - **The checks:** `CheckCustomer`'s problems mark their fields. A delete refused because the customer is in use shows in the information bar. Delete asks through `fx::Ask`.
   - **The history tab:** the customer's sales lines and open items, from Spec #7's sources.
   - **Where it lives:** the command (Accounts > Sales Customers) opens the page. The old dialog stays reachable from the command search as "Sales Customers (classic)" until M7 (Q4).
7. **`/checkres`, `/stress`, text fit:**
   - `/checkres` creates every page template as a child at 100 % and 150 %, runs the text-fit check, and checks that every control has an anchor.
   - `/stress` opens and closes the Customers page 25 times, typing into the filter and selecting rows.
8. **Tests:**
   - **Portable:** the anchor arithmetic, each validator, lookup matching and ranking, `GL_UISTATE` round trips per user.
   - **The page's flow:** a test drives the page's model (not the window) through the Spec #6 sheet's criterion 1 and checks the same `sqlite3` results. C004 added with Options' accounts, changed, refused as in use for C001, and deleted.

### Exit criteria

- The Spec #6 sheet's criterion 1, done on the Customers page, gives its results with no "already added, modify?" question (D9).
- `/checkres`, text fit at 100 % and 150 %, and `/stress` pass. The drive covers the filter, keyboard-only entry and resizing.

---

## Milestone 2 — The other list-and-detail pages

> **As built (2026-11-19; built and run on Windows 11, VS 18, v145).**
> - **Generated forms** (`gl/res/tools/make_pages.py`): one table of pages writes their templates (`gl/res/pages.rc`, #included by `port.rc`) and their C++ descriptors (`gl/port/pages/PageDescs.inc`): the list's columns, each field's control, key, kind and width, and every control's anchor.
>   - **The layout** is fixed: New, Save and Delete (and a page's own buttons) along the top; the filter and the list on the left; the field groups in two columns.
>   - **IDs:** `IDD_PG_DEPARTMENTS` to `IDD_PG_USERS` (20009 to 20017). The controls are numbered per form, 21000 to 21999 (`PortIds.h`).
> - **One page class** (`pages/RecordPage.h`, `RecordPages.cpp`) shows any of them: the Customers page's pattern, driven by a descriptor and a model.
>   - **Field kinds:** text, upper case, whole number, decimal, amount, date (the date picker), choice (a drop-down list), account (a lookup over the chart, with its dept and name), password, and read-only.
>   - **A record's key** is read-only once saved.
>   - **A read-only field** is worked out by the model as you type, such as an asset's monthly amount and book value.
> - **Portable models** (`pages/RecordModel.{h,cpp}`, `pages/RecordModels.{h,cpp}`):
>   - **The base** holds the rows and the record as text by field key. It refuses a new record whose key is taken, so nothing asks "modify?" (D9).
>   - **Each model** reads and writes through the core its classic dialog uses, with that dialog's rules:
>     - **Departments:** a code and a description.
>     - **Medical reps and doctors**, and **products:** a code. Products' cost and unit price take three decimals, as their fields do.
>     - **Vendors:** `CheckVendor`, with GL_SETAT's default accounts; a vendor in use isn't deleted.
>     - **The chart:** an account and a description. A new account needs a type, and never a second retained earnings account. One with balances isn't deleted.
>     - **Asset types:** three accounts in the chart; a type in use isn't deleted.
>     - **Fixed assets:** `CheckAsset`; a disposed asset isn't changed; a depreciated one keeps its run's figures (spec #4 D6) and isn't deleted.
>     - **Users:** a user ID and a level.
> - **The pages:** Chart of Accounts (1004), Department Codes (10061), Purchase Vendors (4045), Medical Reps (6042), Product Codes (6005), Doctor Codes (6040), Fixed Assets > Add/Modify (6000), Asset Types (6036) and User Access (4078) open pages.
>   - **Classic entries:** each old dialog stays as a "(classic)" entry beside its command in the navigation pane (`CM_*_CLASSIC`, 20509 to 20517), following the page's right.
>   - **Fixed Assets** has Dispose..., which opens Fixed Assets > Disposal; Disposal stays a command of its own.
> - **User Access** (task 3) is one page: the users on the left; the user's fields; and the level's commands as check boxes, grouped by menu (accessm's POPUP), checked for allowed.
>   - **Saving** writes `accessl.MENUCODE` as `AccessMenus` did, a character per accessm row in its COUNT order, but only the characters of the rows changed. An unchanged level isn't written, and a code is never shortened or padded beyond the row changed.
>   - **A level accessl doesn't have** shows the first level's code (as the login uses it). It is added only when its commands are changed.
>   - **Reset** (string 8152: the session takes the level's code at once) isn't on the page. Rights are read at the next login.
> - **List Window Parameters** (1035, task 4): no page has it, since the filter box is the query. It stays in the classic dialogs' lists (`TableList`) until they go (S8-M7).
> - **Deferred, with the reason:**
>   - **Budget Data** as a tab of the account: the Budget Data command (10074) still opens the dialog. The budget grid needs M3's entry grid.
>   - **Company Profile's accounts and banks:** Company Profile is one dialog over GL_SETAT and GL_PROF. Its page needs the grid too, so it waits for M3.
>   - The Customers page stays as built in M1, not moved onto the generic page.
> - **`/checkres`, `/stress`** (task 5): `/checkres` checks every generated form (it loads as a child, its captions fit, every control has an anchor). `/stress` opens each page 25 times, with a filter and two selections.
> - **Tests** (task 6, `gl_tests/test_pages.cpp`, 8 cases): each model through its screen's rules on the dev fixture, with the results read back with SQL. They include the refusals, a taken key, saving with nothing asked, and deleting.
>   - **User Access:** every fixture level is written back through the model and compared byte for byte, a row is grayed and allowed again, and a level accessl lacks is shown and not added.
>
>   `gl_tests` 195.
> - **Checked:**
>   - Debug and Release `/W4 /WX` clean; the suites pass in both; `/checkres` 0 in both; `/stress` 0 in both.
>   - Driven in the app on a scratch copy, by messages to its own windows:
>     - **Every page** opened from its command, each in its own tab with its list: 2 departments, 3 medical reps, 3 doctors, 3 products, 3 vendors, 25 accounts, 2 asset types, 4 assets and 2 users. Purchase Vendors again brought its tab forward.
>     - **Departments:** 30 with no description was refused with its field marked. With "Sales" it saved ("Department 30 saved.", `gldept` read back).
>     - **Chart of Accounts:** 998000 with no type was refused ("Choose the account's type."). As a balance sheet account it saved (`glaccts` read back).
>     - **Vendors:** V001's AP account showed its name from the chart. Delete asked in a task dialog and was refused: "invoice voucher 1 is theirs".
>     - **Fixed Assets:** E004 showed disposed on 08/15/2026 with a book value of 0.00, and a change was refused. Switching to E001 asked about the unsaved change; Don't save showed E001's monthly amount, 500.00.
>     - **User Access:** CLERK showed level B's 117 commands. A click on File > New's check box marked the tab "•". Saving wrote level B's code with a "1" in that place and nothing else changed; checking it again and saving gave back the same bytes.
>     - **Classic:** "Department Codes (classic)" opened dialog 1023 with its list.
>
>     Screenshots checked the Vendors and Users pages.
> - **Not driven:** typing into a lookup, Enter and Ctrl+S (the drive sends no keystrokes); the Dispose... button; Products', Doctors' and Asset Types' saves (the tests cover them).

**Goal:** every master screen is a page like Customers.

### Tasks

1. **Read** each screen and its core:
   - `ApVendors.cpp` with `Ap.h`'s vendor functions;
   - `ChartOfAccounts.cpp`, `AccountList.cpp` and `Accounts.h`;
   - `MasterData.cpp` (departments, med reps, products, doctors) and `MasterTables.h`;
   - `FixedAssetTypes.cpp` and `FixedAssets.cpp` with `Assets.h`;
   - `CompanyProfile.cpp` (its accounts and banks; GL_SETAT);
   - `Login.cpp`'s `UserAdministration` and `AccessMenus`.

   Each sheet's master criteria (Specs #1, #2, #4, #5, #6) are the checks.
2. **Pages**, each on M1's pattern: Vendors, Chart of Accounts (with Budget Data as a tab of the account), Departments, Med reps, Products, Doctors, Fixed asset types, Fixed assets (with Disposal as a command), and Company profile's accounts and banks.
3. **User Access** (spec §8): a page with the users on the left. The selected user's fields and level are on the right, with the level's commands grouped by module, a check box each.
   - **Saving** writes the same `accessl.MENUCODE` string `AccessMenus` writes: one character per `accessm` row in COUNT order.
   - **A test** loads every fixture level, saves it unchanged through the page's model, and compares the string byte for byte.
4. **List Window Parameters** (1035) goes: the filter box replaces it. Its command (the right-click on a list) opens the filter instead.
5. **`/checkres`, `/stress`:** every new template; each page opened and closed, with a filter and a selection.
6. **Tests:** each page model through its sheet's master criterion, with the same `sqlite3` results. User Access round trips its codes.

### Exit criteria

- Each master criterion of the Spec #1, #2, #4, #5 and #6 sheets passes on its page.
- No master screen opens as a modal dialog except through its "classic" entry.

---

## Milestone 3 — The entry grid and the voucher pages

> **As built (2026-11-19; built and run on Windows 11, VS 18, v145).**
> - **The entry grid** (task 2):
>   - **`model::EntryGridModel`** (`win32fx/include/model/EntryGridModel.h`, portable): rows of typed text, a blank row always at the end, and the current cell. It owns the key table: Tab and Enter (Q3) move on; Shift+Tab back; Up and Down; F2 edits keeping the text; a typed key edits from empty; Esc cancels an edit and otherwise does nothing; Insert, Ctrl+Delete and Ctrl+Z (undoing removed rows, the last first). It also checks cells on commit (by kind, then the column's validator), keeps totals in cents, says whether a voucher balances, and notifies the page (`OnCommit`, `OnChanged`).
>   - **`fx::EntryGrid`** (`win32fx/include/EntryGrid.h`): a report ListView on the form, owner-data, showing the model.
>     - **Editing:** an edit is made over the cell for each edit, with an `fx::Lookup` when the column has one. Clicking a cell selects it; a double-click edits it.
>     - **Drawing:** the current cell and row are highlighted (custom draw).
>     - **Focus:** focus moving to another control commits the edit (or drops a refused one); another application taking the focus leaves it as it is.
>     - **Deviation:** it is a plain ListView, not `fx::Grid`, whose tbSpread editing would have fought the model's keys.
>   - **No separate test page:** `/stress` types through the GL voucher page's grid with `EntryGrid::Type` and `Press` instead, covering every key.
> - **The GL voucher page** (task 3; `pages/GlVoucherModel.{h,cpp}`, `pages/VoucherPages.cpp`, `IDD_PG_GLVOUCHER`):
>   - **The form:** the header (date, period, cheque, description) and the grid: account, dept, document, the account's name (filled from the chart), debit, credit, med rep, product and doctor, with lookups. Below are the totals, the difference and "Balanced." or "Out of balance by ...".
>   - **Save** checks what Voucher Entry checks: a description, a date, an open period, and per line a document, an account and dept in the chart, and a debit or a credit but not both. It then writes through `SaveLine` and `DeleteLine`, the calls Voucher Entry makes.
>   - **Post** is shown only while the voucher balances in an open period. It saves first, asks in a task dialog, then calls `PostVoucher`.
>   - **Navigation:** Previous and Next (Ctrl+PgUp and Ctrl+PgDn) move between the open vouchers.
>   - **Prior Period Entry** opens the same page in adjustment mode (mode "3"; only adjustments open).
>   - **No memo:** GL vouchers have no memo field. The memo is on the sales voucher page.
> - **The voucher list** (task 4, `IDD_PG_VOUCHERLIST`): open and adjustment vouchers, and posted ones with a check box, filtered as you type. Open (or a double-click) opens the voucher's page in its own tab. It replaces the M0 browser for Transactions > Voucher List; `VoucherBrowser.cpp` is no longer reached.
> - **AP and AR** (task 5; `pages/SubledgerModels.{h,cpp}`):
>   - **The purchase and cash voucher page** (`IDD_PG_APVOUCHER`): expense lines in the grid. The vendor (a lookup) fills its accounts, rates, terms and type. The voucher's lines as they will be saved (expense, VAT, W/H and the AP or bank line, `ComputeVoucher`) show under the grid with the totals. Save runs Invoice Entry's checks in order, the duplicate invoice among them (in the information bar), and writes through `SaveExpenseLine`. Cash vouchers show the bank and cheque instead of the AP account, due date and terms.
>   - **The sales voucher page** (`IDD_PG_ARINVOICE`): product lines with a quantity and price, or an amount. The customer fills the accounts, VAT and terms; the split shows through `ComputeSale`. It saves through `SaveSaleLine`, with the memo beside the lines (`SaveArMemo`).
>   - **The receipts page** (`IDD_PG_RECEIPTS`): the open invoices on the left, filtered. On the right, the receipt: date, bank, amount, and "Discount the rest". It records through `RecordReceipt`.
> - **Commands:** General Ledger Voucher (10076, 1007), Prior Period Entry (8081), Voucher List, Purchase Invoice (4044), Cash Invoice (1079), Sales Invoice (5045) and Clear Receipts (4054) open pages. Each dialog stays as a "(classic)" entry (`CM_*_CLASSIC`, 20518 to 20523), following the page's right.
> - **`/checkres`, `/stress`** (task 6): `/checkres` checks the five forms. `/stress` opens each page 25 times, types five lines (on the GL page through the grid's keys and editor: Tab, F2, Esc, Insert, Ctrl+Delete, Ctrl+Z), and closes it without saving.
> - **Tests** (task 7): `gl_tests` 203.
>   - `test_grid.cpp`, 4 cases: every key in every column kind, validation, totals in cents, balancing, undo.
>   - `test_voucher_pages.cpp`, 4 cases, each compared table by table with the classic screen's calls on a second fixture copy:
>     - **Spec #2:** a five-line voucher typed through the grid's keys, saved and posted. `gl_btch`, `gl_btchd`, `gl_dist` and `glaccts` are the same. The refusals, editing (a removed line), the modes and navigation are covered too.
>     - **Spec #5:** V001's invoice with VAT 12 and W/H 1. The split is VAT 180.00, W/H 15.00 and AP 1,665.00; the duplicate is refused; `ap_btch`, `ap_btchd`, `ap_linex`, `ap_dist` and `ap_jsum` are the same.
>     - **Spec #6:** voucher 7, 3 x 1,100.00 (Cr 3,000.00, Cr 300.00, Dr 3,300.00), posted, then its receipts: 1,000.00, then 2,200.00 with a discount of 100.00. The `ar_*` tables are the same.
> - **Checked:**
>   - Debug and Release `/W4 /WX` clean; the suites pass in both; `/checkres` 0 in both; `/stress` 0 in both.
>   - Driven in the app on a scratch copy, by messages to its own windows (characters and keys posted to the grid and its editor, no global keystrokes):
>     - **The exit criterion:** a five-line GL voucher typed with keys only. The totals moved with each line and Post appeared once it balanced. A cell with 999999 was refused ("Not an account in the chart.") and Esc dropped it. Saved as voucher 104 (`gl_btchd` read back with the chart's names) and posted: `gl_dist` has 5 rows, 350.00 each side.
>     - **Navigation:** Previous showed voucher 102 out of balance by 10.00, with Post hidden.
>     - **The voucher list** opened voucher 101 in its own tab.
>     - **Purchase voucher:** V001 filled VAT 12 and W/H 1; a line of 1,120.00 showed VAT 120.00, W/H 10.00 and owed 1,110.00, and saved as voucher 7.
>     - **Sales voucher 7:** C001 with three lines of 1,100.00 showed 3,300.00 each side and VAT 300.00, and was saved with its memo and posted.
>     - **Receipts:** 1,000.00 recorded against S-1007, leaving 2,300.00 owed.
>
>     Screenshots checked the GL, AP and AR pages.
> - **Fixed while driving:**
>   - Typing a vendor or customer copied it into the model on each key, so leaving the field never loaded its details.
>   - The receipts list and the voucher list didn't refresh when their tab came back: the workspace shows a page with `SWP_SHOWWINDOW`, which sends no `WM_SHOWWINDOW`.
> - **Not driven:**
>   - a lookup's list, and Ctrl+PgUp itself (the drive pressed Previous);
>   - a cash voucher's save, and AP and AR posting (the tests cover them);
>   - Delete on the voucher pages.

**Goal:** vouchers are typed into a grid, with live totals, on pages.

### Tasks

1. **Read:**
   - `VoucherEntry.cpp` (GL and adjustments, the tool box, Memo) with `Vouchers.h`;
   - `ApInvoiceEntry.cpp` with `Ap.h`'s `ComputedVoucher` and `ExpenseEntry`;
   - `ArInvoiceEntry.cpp` with `Ar.h`'s `ComputedSale` and `SaleEntry`;
   - `ArReceipts.cpp`;
   - `VoucherBrowser.cpp`.

   The entry criteria of the Spec #2, #5 and #6 sheets are the checks.
2. **`fx::EntryGrid`** (spec §6.4), in two halves:
   - **Portable model** (`EntryGridModel`): the rows and cells (typed values), each column's validator, and the current cell. It decides key handling: what Tab, Shift+Tab, Enter, Esc, F2, Insert, Ctrl+Delete and Ctrl+Z do. It also holds the totals, the "balances" state, and change notifications.
   - **Win32 view:** `fx::Grid` in report mode, showing the model. Editing a cell puts an edit, a `fx::Lookup` or a `fx::DateField` over it. Keys are forwarded to the model.
   - **A test page** (`/stress` and the drives only) exercises every key.
3. **The GL voucher page** (`IDD_PG_GLVOUCHER`), spec §5.2:
   - **Layout:** the header (date, period, reference, the voucher's kind), the grid (account, dept, description, debit, credit, med rep, product, doctor), the totals and the difference.
   - **Save:** goes through `Vouchers.h` as Voucher Entry does.
   - **Post:** shown only when the voucher balances and its period is open.
   - **Memo** is a side panel.
   - **Navigation:** Ctrl+PgUp and Ctrl+PgDn move to the previous and next voucher. Adjustments open the same page in adjustment mode.
4. **The voucher list** becomes a page of M1's kind; opening a voucher opens its page in a tab.
5. **AP and AR:**
   - **AP invoice and cash vouchers:** the split (expense, VAT, W/H) shown as lines are typed, through `ComputedVoucher`. The duplicate-invoice refusal goes in the information bar.
   - **AR invoices:** quantity, unit price or amount per line, with the split through `ComputedSale`.
   - **Receipts:** the invoice list on the left, and the receipt (bank, date, amount, Partial or Discount) on the right, through `ArReceipts`' core calls.
6. **`/checkres`, `/stress`:** each page is opened, five lines typed through the model, and the page closed with no save; object counts are checked.
7. **Tests:**
   - **The model:** every key in every column kind; validation; totals in cents; balancing; undo of a removed line.
   - **Each page's model** through its sheet's entry criteria:
     - Spec #2's GL voucher;
     - Spec #5's invoice with VAT and W/H;
     - Spec #6's voucher 7, 3 × 1,100.00 (Cr 3,000.00, Cr 300.00, Dr 3,300.00), and its receipts.

     Each gives the same `sqlite3` results.

### Exit criteria

- A five-line GL voucher typed with the keyboard only, saved and posted, gives the same `gl_btchd` and `gl_dist` rows as through Voucher Entry.
- The Spec #2, #5 and #6 sheets' entry criteria pass on the pages.

**If Q3 says Enter presses the default button,** only the model's key table changes.

---

## Milestone 4 — Background jobs and the task pages

**Goal:** batch work runs off the UI thread, pages stay current, and every batch screen is a task page.

### Tasks

1. **Read:**
   - `cb4sqlite`'s `Code4::open` (`journal_mode = DELETE`) and its backup (`Code4.cpp`);
   - `Backup.cpp` and `CompanyFiles.cpp`;
   - each batch screen and its core: `VoucherPosting.cpp`, `ApPosting.cpp`, `ArPosting.cpp`, `ApTransfer.cpp`, `DepreciationRun.cpp`, `YearEnd.cpp`, `ApPayments.cpp` (Create Cheques, printing, and Purge Paid Payables), and `ArReceipts.cpp` (Clear Receipts and Purge Received Receivables).
2. **WAL** (D8, Q6):
   - **Opening:** `Code4::open` sets `journal_mode = WAL` and `busy_timeout`.
   - **Backup and Company Files:** they checkpoint (`wal_checkpoint(TRUNCATE)`) before copying, so a copy is one file.
   - **`dbf2sqlite`'s output** is left in DELETE mode, since it's a one-shot tool.
   - **Tests:** a second connection reads while the first writes; a backup taken while a job is writing opens and passes `integrity_check` (R3).
3. **`fx::Job`** (`win32fx/Job.{h,cpp}`), spec §9.2:
   - **Running:** a worker thread with its own `Code4` on the company file. It runs a function, reports progress by `PostMessage` to its page, and honours a cancel flag between steps.
   - **One writing job at a time.** While it runs, conflicting commands are disabled, with the reason in their tooltip.
   - **A save on a page** waits, with a message in its information bar, while a job is writing (§9.1, R5).
   - **The state machine** (queued, running, cancelling, done, failed) is portable and tested.
4. **Progress in the cores**: an optional `std::function<bool(int done, int of)>` on the long loops:
   - posting several vouchers (GL, AP, AR);
   - the transfer;
   - the depreciation run;
   - year end;
   - the purges;
   - Create Cheques.

   It returns false to stop at the next voucher boundary. The dialogs pass none, so their behaviour and tests are unchanged.
5. **The data-changed broadcast** (spec §9.3):
   - **Notifying:** `NotifyDataChanged(groups)` after a save or a job, with a bit per table group (GL vouchers, GL balances, AP, AR, assets, masters, reports).
   - **Refreshing:** open pages refresh what they show. A dirty page shows "This record has changed since you opened it", with Reload.
   - **The status bar's waiting counts** come from it.
6. **Task pages** (spec §5.3), each with a list with check boxes, a summary, one named action and progress:
   - Post vouchers (GL, AP, AR);
   - Cancel and Recover (each kind);
   - Update AR and AP;
   - Depreciation run;
   - Year end;
   - Purge paid payables and Purge received receivables;
   - Create cheques and Print cheques;
   - Clear receipts;
   - Backup and Restore.

   Refusals go on the list's rows. The result links to what was made ("GL voucher 105").
7. **`/stress`:** each task page opened and closed, and a dry job (the posting of zero vouchers) started and cancelled 25 times, checking that threads and objects are released.
8. **Tests:** WAL and backup; the job state machine; cancel at a boundary leaves the books consistent (a posting of three vouchers cancelled after one posts exactly one); serialised writes; each task page's model through its sheet's criteria:
   - Spec #2's posting, recover and year end;
   - Spec #4's depreciation run;
   - Spec #5's posting, cheques and purge;
   - Spec #6's transfer and purge.

   Each gives the same `sqlite3` results.

### Exit criteria

- Posting the fixture's open vouchers while browsing the Chart of Accounts: progress shows, the browsing doesn't freeze, and the voucher list and home counts refresh at the end.
- Every batch criterion of the Spec #2, #4, #5 and #6 sheets passes on its task page.

**If Q6 says no WAL,** jobs still run on a worker, but pages are read-only while a job writes (their commands disabled, with the reason), and task 2 is dropped.

---

## Milestone 5 — Reports, the home page, command search

**Goal:** reports open in tabs with their options beside them; the home page shows what needs doing; anything can be found with Ctrl+K.

### Tasks

1. **Read:**
   - `ReportList.cpp` (the entries, Spec #7's buttons and right);
   - `ReportOptions.cpp` (each built-in report's options dialog);
   - `ReportPrompt.cpp` and `ReportDesigner.cpp`;
   - `RunReport.cpp`'s rebuild hook;
   - `Preview.cpp`'s view.
2. **The report gallery page:** built-in reports grouped by module, then user reports, with search, Favourites and Recent (in `GL_UISTATE`). Spec #7's commands are on its command bar, behind the Report Writer right.
3. **Options panels:** each options dialog's template becomes a child panel template (`IDD_PNL_*`), shown above its preview. **Refresh** rebuilds through the same builder and options struct. Spec #7's questions are a panel too, from `ReportPrompt`'s model.
4. **The preview page:** Spec #3's view hosted as a page, with Print, Export (CSV), the page buttons and zoom on its command bar. Several can be open.
5. **The designer page:** Spec #7's designer as a page, with the preview on the right, rebuilt (debounced) as the definition changes.
6. **The home page** (spec §5.5, Q5):
   - **Tiles:** the period and its state; vouchers waiting (GL, AP, AR); journals waiting for transfer; cheques to print; depreciation not run for the period; AR and AP owed and overdue; recent pages; and favourite reports.
   - **Sources:** each figure comes from a core function and is refreshed by M4's broadcast. Each tile links to its page.
7. **Command search** (Ctrl+K, spec §4): `fx::Lookup` over the command table (by label, with greyed commands left out) and the masters (accounts, customers, vendors, vouchers by number). Choosing an entry opens it.
8. **`/checkres`, `/stress`:** every panel template; 20 previews opened, refreshed and closed; the home page refreshed 100 times.
9. **Tests:**
   - **The home figures** against the reports that cover them, on the fixture:
     - AR owed = Open Receivables Detail;
     - AP owed = Open Payables Detail;
     - waiting vouchers = the posting lists;
     - waiting journals = the transfer's count.
   - **The gallery:** search, favourites and recent.
   - **The search's ranking** over commands and masters.

### Exit criteria

- The Spec #3, #5, #6 and #7 sheets' report criteria pass from the gallery, with options in panels.
- The home page's figures agree with the reports on the fixture.

---

## Milestone 6 — Polish, accessibility, and removing the old

**Goal:** the redesign looks and reads like a current Windows program, and what it replaced is gone.

### Tasks

1. **Windows 11 touches** (spec §11):
   - **Mica:** `DWMWA_SYSTEMBACKDROP_TYPE` set to Mica on 22H2 and later. It's off with high contrast or with transparency effects turned off (`SPI_GETHIGHCONTRAST`, `UISettings`' transparency read from the registry key Windows uses).
   - **Title bar:** `DWMWA_USE_IMMERSIVE_DARK_MODE` follows the app theme.
   - **Windows 10** (Q7): the same program without the backdrop.
2. **Accessibility:**
   - **Names:** every control gets a UI Automation name. A label's text names its field, using the dialog manager's label-before-control rule or an explicit relation.
   - **Announcements:** the information bar raises `UiaRaiseNotificationEvent`.
   - **`/checkres` checks names:** it creates each page and lists any focusable control without an accessible name (`IAccessible::get_accName`, or UIA), as the text-fit check lists clipped text.
3. **The keyboard audit:** `/checkres` also walks each page's tab order, and reports controls that can't be reached and orders that don't follow reading order (top to bottom, then left to right, within groups). Every command has a shortcut or an access key.
4. **Removing the old** (Q4), once each milestone's acceptance has passed:
   - **Dialogs:** `prune_rc.py` stops emitting the dialogs pages replaced, and the classic entries leave the command search.
   - **MainDialog:** goes if nothing uses it.
   - **Controls:** `tbPic`, `tbDate` and `tbSuperBtn` go if nothing uses them (`tbSpread` stays as `fx::Grid`).
   - **`closeAll`:** leaves the code.
   - **MDI:** `MdiFrame` leaves win32fx, and the `trivial_dialog` example moves to the workspace.
   - **The menu:** the MANILA menu leaves `gl.rc` (the command table builds it).
5. **Screenshots:** `/screenshots` covers pages and panels. A before and after set at 100 % and 150 % for each page.

### Exit criteria

- `/checkres` reports no clipped text, no unnamed control and no unreachable control at 100 % and 150 %.
- The only modal windows left are decisions (`fx::Ask`), Login, and the system's own dialogs (file, print, printer setup).

---

## Milestone 7 — Acceptance

1. **`gl/docs/superpowers/specs/2026-11-19-spec8-acceptance.md`**, in the Spec #7 sheet's shape, with spec §15's checks. Each of the Spec #1–#7 sheets gets a "redesign" column for the steps whose screens moved, saying where the step is now done; the expected figures don't change. A `gl_tests` case runs the moved steps through the page models and checks every figure.
2. Release build check, the finance team's walkthrough with a witness, triage, sign-off.

---

## Cross-cutting

- **Per-port checklist** from the Spec #1 plan for every new file. Each page's header lists the dialog it replaces and the decisions it applies.
- **Per-milestone gates:**
  - `/W4 /WX` clean on Debug and Release x64;
  - the milestone's tests pass, and every earlier suite;
  - `/checkres` (with text fit, and from M6 names and tab order) and `/stress` exit 0;
  - screenshots before and after at 100 % and 150 %;
  - a spec drift check;
  - an entry in `gl/docs/superpowers/plans/2026-05-10-progress.md`;
  - the milestone's "As built" note here.
- **What not to do:**
  - Don't change what a core computes or writes. The only core changes are optional progress callbacks (M4) and checks moved from a dialog into its core, each with a test.
  - Don't change a table's layout. The only additions are `GL_UISTATE` and WAL mode.
  - Don't change a command's ID: routing, `/stress`, rights and Custom Report depend on them.
  - Don't call `closeAll` in a page, or close a table a page didn't open.
  - Don't write to the database from a second thread except through `fx::Job`'s own connection.
  - Don't remove an old dialog before its page's acceptance has passed (Q4).
  - Don't edit `gl/res/gl.rc` by hand: page templates go in `port.rc`, and menu and dialog removal goes in `prune_rc.py`.
  - Don't add a runtime dependency: user32, comctl32 v6, dwmapi, uxtheme, oleacc and uiautomationcore only, all part of Windows.

## Risks specific to the plan

| # | Severity | Risk | Mitigation |
|---|---|---|---|
| P1 | High | Removing MDI breaks the preview, the voucher list or `/stress`, which take an `MdiFrame&`. | M0 task 2 puts `PageHost` in front of them while MDI still runs, so the change is proved before the workspace replaces it. |
| P2 | Medium | The command table drifts from the menu: a command lost, or a right misplaced. | The table is generated from the MANILA menu (M0), with tests of the menu's IDs and order and of every level's rights against `ApplyMenuAccess`. |
| P3 | Medium | Page helpers drift from `MainDialog`'s (UTF-8, trimming, dates), so a page saves text differently from its dialog. | The helpers are shared code, not copies. Each page's tests compare the same `sqlite3` results as its dialog's sheet. |
| P4 | High | The entry grid's in-place editing has focus and key edge cases (IME, lost focus mid-edit, scrolling while editing). | The model decides every key and is tested alone. The view is thin. A test page in `/stress` and a keyboard-only drive run every key. |
| P5 | Medium | WAL on the deployed company: antivirus or backup software that copies the folder while the program runs takes an incomplete copy. | Checkpoint on close as well as before backup. The go-live note says to back up through the program. Q6 can turn it off (M4's fallback). |
| P6 | Medium | Drive scripts break once screens are pages, not top-level dialogs. | M0 task 8 makes the helpers find pages in the workspace by tab text. Each milestone updates its drive before moving screens. |
| P7 | Medium | The finance team asks for changes after M1 that ripple through M2–M5. | The gate before M1 (Q2). The patterns are shared code, so a change to the list-and-detail page or the grid changes every page that uses it. |

## How to use this plan

M0 → (the finance team's review) → M1 → M2, M3 and M4 side by side, with M4's WAL and jobs landing before M3's posting from the voucher page → M5 → M6 → M7. If the plan and the spec conflict, stop, edit the spec, recheck the plan, then continue.
