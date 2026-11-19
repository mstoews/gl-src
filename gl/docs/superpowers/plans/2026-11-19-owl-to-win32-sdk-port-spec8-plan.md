# Spec #8 — Implementation Plan: A Modern Workspace

**Spec reference:** `gl/docs/superpowers/specs/2026-11-19-owl-to-win32-sdk-port-spec8-design.md`
**Date:** 2026-11-19
**Plan status:** In progress (S8-M0 built)

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
