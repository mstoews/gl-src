# Spec #8 — A Modern Workspace: the Redesign on the Windows SDK

**Status:** Draft for review (2026-11-19). Q1–Q8 open (§18). The plan comes after review.
**Date:** 2026-11-19
**Scope:** Eighth spec of the migration, and the first that changes how people work. Specs #1–#7 ported the 1992–95 program faithfully. This one redesigns its user interface:
- one main window with a navigation pane and tabs, in place of the MDI frame and its modal screens;
- three page patterns that every screen follows: list and detail, document entry, and task;
- standard Windows controls in place of the original's custom ones;
- problems shown where they are, decisions asked in plain words;
- one table of commands behind the menu, toolbar, navigation and search;
- long work in the background, and a home page that shows what needs doing.

It stays in **C++ and the Windows SDK** (user32, comctl32 v6, DWM, UI Automation). No MFC, ATL, WinUI, XAML or .NET (D1). The books, the database and every business rule stay as they are: the portable cores Specs #2–#7 built (`Vouchers`, `Accounts`, `Ap`, `Ar`, `Assets`, `ReportWriter`, the report engine) are not changed (D2).

---

## 1. Goals & Non-Goals

### 1.1 Goals (Spec #8 deliverables)

1. **The workspace** (§4): a single top-level window with a navigation pane, a tab strip of open pages, a page area, and a status bar. Several screens can be open at once and moved between without closing any.
2. **Pages** (§5): every screen becomes a page of one of three kinds. Lists you can filter replace typing a code and pressing Enter; Top, Next and Find go.
3. **Controls** (§6): the Windows date picker, edits with cue banners and balloon tips, type-ahead lookups for accounts and the other codes, and an entry grid for voucher lines.
4. **Feedback** (§7): inline validation and an information bar. `TaskDialogIndirect`, with buttons that say what they do, for the decisions that remain.
5. **Commands and access** (§8): one command table, from which the menu bar, toolbar, navigation pane and a Ctrl+K command search are built. Rights are looked up by command ID; the `accessm` and `accessl` tables keep their layout (D6).
6. **Layout** (§10): pages that resize with the window, per-monitor DPI kept, and window, tab and pane state remembered per user.
7. **Background work** (§9): posting, the GL transfer, depreciation, year end, purges and large reports run off the UI thread, with progress and Cancel where the core allows it.
8. **The home page** (§5.5): the period, what's waiting to post or transfer, and what's owed, each a link.
9. **Windows 11 touches** (§11): the Mica backdrop and a title bar that follows the system theme, and UI Automation names on every control.
10. **Coexistence** (§12): a screen not yet redesigned still opens, as today's dialog, from the new window. The program is usable after every milestone.

### 1.2 Non-Goals

- **Anything about the books.** No table, layout, posting rule, report figure or file format changes. `gl_tests` and the acceptance sheets' figures stay as they are.
- **New functions.** No module, report or entry kind the program doesn't already have. Where the redesign makes something easier (filter a list, reopen a recent report), that's presentation, not a new function.
- **Another UI framework** (D1).
- **Full dark mode.** The standard controls don't support it; only the title bar and backdrop follow the theme (§11).
- **Touch-first design.** Mouse and keyboard stay primary. Touch works where Windows' controls already support it.
- **Several users at once, or the database on a network drive.** That needs locking work of its own (Spec #1 R10) and is a later spec.
- **The report engine's output.** Previews and prints are Spec #3's, unchanged. Only where the preview is shown changes: a tab instead of an MDI child (§5.4).

---

## 2. The UI today

| Area | Today | Where |
|---|---|---|
| Frame | An MDI frame (`ManilaGL`, class `GLWindow`) with an `MDIClient`. Reports and the voucher list are MDI children; nearly everything else is modal. | `ManilaGL.cpp`, `win32fx/MdiFrame.h` |
| Screens | About 47 dialog templates from `gl.rc` (made by `prune_rc.py` from the original `.rc` files) and `port.rc`, run modally by `MainDialog` subclasses. | `gl/res/`, `gl/port/*.cpp` |
| Commands | The MANILA menu, 116 items; a 12-button toolbar; accelerators. The frame's `OnCommand` routes every ID. | `ManilaGL.cpp`, `gl.rc` |
| Rights | `ApplyMenuAccess` walks `accessm` in COUNT order beside the user's menu code `szMsg`, one character per row; '1' greys that row's command. | `Login.cpp` |
| Controls | `tbPic` (masked edit), `tbDate` (masked date), `tbSuperBtn` (picture button), `tbSpread` (`fx::Grid`, a ListView). | `win32fx/controls` |
| Records | Masters show one record at a time: type the code, press Enter (IDOK with the focus in that field); Top, Next, Previous and Find move. The masters and the entry screens work this way. | `ArCustomers.cpp`, `ApVendors.cpp`, `MasterData.cpp`, `FixedAssets.cpp`, `FixedAssetTypes.cpp`, `VoucherEntry.cpp`, `ApInvoiceEntry.cpp`, `ArInvoiceEntry.cpp`, … |
| Entry | A voucher line is typed into fields above the grid, then **Add**; a line is changed by double-clicking it. | `VoucherEntry.cpp`, `ApInvoiceEntry.cpp`, `ArInvoiceEntry.cpp` |
| Feedback | `MessageBoxW` with the program's caption for every check, warning and question; Yes/No buttons. | `MainDialog::Message` |
| Data | One global connection `cb` (`Code4`), on the UI thread. `MainDialog::Cancel` runs `cb.closeAll(); cb.init()`, as the original did, which assumes one screen at a time. 15 files call `closeAll`. Journal mode DELETE. | `Globals.h`, `MainDialog.cpp`, `cb4sqlite` |
| Look | Segoe UI, Fluent icons, themed toolbar and status bar, per-monitor DPI (the UI refresh). | `specs/2026-11-13-ui-refresh-design.md` |

The UI refresh changed how the program looks, not how it works. Its non-goals ("no change to how a user works") are this spec's goals.

---

## 3. Design principles

1. **Same functions, same figures.** Every command the program has keeps a home, and every figure on every acceptance sheet stays right.
2. **Three patterns, applied everywhere.** A user who has learnt Customers knows Vendors, Accounts and Fixed Assets. No screen gets a layout of its own unless its job needs one.
3. **Keyboard first.** The finance team types fast and has used this program for decades. Every action has a key, Tab order follows reading order, and Enter does what they expect (Q3).
4. **Show state, don't interrupt.** What's wrong is marked where it is. A message box is for a decision only.
5. **Native, not imitated.** Common controls v6, the system font and colours, the system's dialogs. No custom-drawn look.
6. **One screen at a time, one milestone at a time.** Each milestone ends with a working program and passing tests. Old and new screens live side by side until the last one moves.
7. **The cores don't move.** Pages call the same functions the dialogs call. If a page needs something a core doesn't offer, it's added to the core with a test, as before.

---

## 4. The workspace

```
+--------------------------------------------------------------------------+
| Company name                         [Search commands, accounts  Ctrl+K] |
+-------------+------------------------------------------------------------+
| Home        | [Home] [Customers x] [Voucher 104 *] [Trial balance]       |
| General     +------------------------------------------------------------+
|  ledger     | New  Save  Delete                              Export      |
| Payables    | ! C002 has 600.00 overdue since 11/07/26.                  |
| Receivables | +------------------+  Name        [Reyes, Ana Dra.     ]   |
| Fixed       | | Filter customers |  AR account  [111101  Accounts ...v]  |
|  assets     | | C001 City Gen... |  Terms       [30] days                |
| Reports     | | C002 Reyes, A... |  ...                                  |
| Maintenance | +------------------+                                       |
+-------------+------------------------------------------------------------+
| ADMIN | Period 7 (Oct 26), open | 3 vouchers waiting to post |   Saved   |
+--------------------------------------------------------------------------+
```

- **The window** (`fx::Workspace`, replacing `fx::MdiFrame`): one top-level window. It holds the menu bar (§8), a command search box, the navigation pane, the tab strip, the page area and the status bar.
- **Navigation pane:** the modules as groups (Home, General ledger, Payables, Receivables, Fixed assets, Reports, Maintenance), each listing its pages. A TreeView with `TVS_FULLROWSELECT`, built from the command table. It can be collapsed to icons (Ctrl+Shift+N, Q1).
- **Tabs:** a Tab control (`WC_TABCONTROL`, `TCS_FOCUSNEVER`) above the page area. Opening a page that's already open selects its tab. A page with unsaved changes shows "•" after its title. Ctrl+Tab and Ctrl+Shift+Tab move between tabs; Ctrl+W and Ctrl+F4 close one, asking first if it has changes. A tab's context menu offers Close, Close others, and Pin (a pinned tab survives a restart, §10).
- **Pages:** each page is a child window (§5) that the workspace shows and hides; a hidden page keeps its state.
- **Command search** (Ctrl+K): an edit with a drop-down list that matches commands by name, and accounts, customers, vendors and vouchers by code or name. Choosing one opens it. It is the same lookup control as §6.3, over the command table and the masters.
- **Status bar:** the user, the period and whether it's open, a live count of what's waiting (vouchers to post, journals to transfer), and the last thing done ("Saved", "Voucher 104 posted"). Each count is a link.
- **Closing the program** asks once, listing the pages with unsaved changes.

---

## 5. Pages

A page is a modeless child window (`fx::Page`, replacing `MainDialog` for redesigned screens). It owns its state and its open tables (§9), says whether it has unsaved changes (`IsDirty`), saves or reverts them, and can refuse to close (`CanClose`). Each page declares its commands; the workspace shows them in the page's command bar and enables them from the page's state.

Pages are laid out in `port.rc` as dialog templates with `WS_CHILD | DS_CONTROL`, plus an anchor table (§10). Resource tools, `/checkres` and the text-fit check keep working on them.

### 5.1 List and detail

**For:** Chart of Accounts, Departments, Customers, Vendors, Med reps, Products, Doctors, Fixed asset types, Fixed assets, Users, Company profile's accounts and banks.

- **Left:** a virtual ListView (`LVS_OWNERDATA | LVS_REPORT`) of the records, with a filter box above it that matches code and name as you type. Columns can be sorted and resized. A list of thousands of accounts stays instant.
- **Right:** the selected record's fields, in groups. **New** clears them for a new record; **Save** (Ctrl+S) writes through the core; **Delete** asks (§7.3).
- **Changes:** editing marks the page dirty. Moving to another record with unsaved changes asks to save them. The original asked "has already been added. Do you want to modify the entry?"; the page knows whether the record is new or existing, so it doesn't ask (D9).
- **What goes:** typing a code and pressing Enter, Top, Next, Previous, Find, and the List Window Parameters dialog (1035), which the filter box replaces.
- **A record's history:** for an account, customer or vendor, a tab under the fields lists its transactions (from the same report-writer sources as Spec #7), each opening its voucher.

### 5.2 Document entry

**For:** GL vouchers and adjustments, AP invoice and cash vouchers, AR sales invoices, receipts.

- **Header:** the voucher's fields (date, period, reference, customer or vendor and so on), with lookups (§6.3).
- **Lines:** an entry grid (§6.4) you type into: account, dept, description, debit, credit, and the codes (med rep, product, doctor). Tab moves across, Enter goes down a line, Insert and Ctrl+Delete add and remove lines.
- **Totals:** debit, credit and the difference, kept up to date under the grid. An AR or AP voucher shows the split the core computes (sales and VAT, expense, VAT and W/H) as lines are typed.
- **Actions:** **Save** keeps it open (as today); **Post** appears only when the voucher balances and the period is open, and posts through the core. **Memo** is a side panel, not a separate dialog.
- **Navigation:** a voucher list (the current Voucher List, a page of its own) opens vouchers in tabs. "Next voucher" and "Previous voucher" stay as keys (Ctrl+PgDn, Ctrl+PgUp).

### 5.3 Task pages

**For:** Post vouchers, Cancel, Recover, Update AR and AP, Depreciation run, Year end, Purge paid payables, Purge received receivables, Create cheques, Clear receipts, Print cheques.

- **A list** of what the task will act on, with check boxes, and a summary of what will happen ("2 vouchers, 10,700.00 each way").
- **One action button**, named for what it does ("Post 2 vouchers"). It runs in the background (§9) with a progress bar on the page; the page shows the result when it ends, with links to what was made ("GL voucher 105").
- **Refusals** (a closed period, an unbalanced voucher) show in the list, on the row, rather than as a message after the button.

### 5.4 Reports

- **The report gallery** (Reports in the navigation pane, and File > Print): every built-in and user report, grouped by module, with a search box, Favourites and Recent. User reports show who made them. Add, Modify, Copy, Delete, Export and Import (Spec #7) are its commands, with the Report Writer right as today.
- **Options:** a report's options (period, dates, accounts) open as a panel above its preview, not a dialog. Changing them and pressing **Refresh** rebuilds the preview in place. A report with questions (Spec #7 D6) shows them in the same panel.
- **The preview** is a tab (Spec #3's `rptPreview` view, hosted as a page), with Print, Export (CSV) and the page buttons in its command bar. Several reports can be open side by side.
- **The designer** (Spec #7) becomes a page, with the preview on the right, refreshed as the definition changes.

### 5.5 Home

The first tab, always open. It shows:
- the company, the current period and whether it's open;
- what's waiting: vouchers to post (GL, AP, AR), journals waiting for the GL transfer, cheques to print, depreciation not yet run for the period;
- what's owed: AR and AP totals, and what's overdue;
- recent pages and favourite reports.

Each figure is a link to the page that deals with it. The figures come from the cores' existing functions (`LoadArOpenItems`, `AmountOwed`, the journal and voucher loaders), refreshed when data changes (§9.3). The list of tiles is Q5.

---

## 6. Controls

### 6.1 The mapping

| Today | Redesign | Notes |
|---|---|---|
| `tbPic` text | `EDIT` with `EM_SETCUEBANNER` and `EM_LIMITTEXT` | The mask's job (length, case, allowed characters) moves to a validator (§6.2). |
| `tbPic` amount | `EDIT`, right-aligned, formatted on leaving the field | Typed "1250" or "1,250.00"; shown "1,250.00"; read through the cores' parsers. |
| `tbDate` | `DATETIMEPICK_CLASS` with `DTS_SHORTDATECENTURYFORMAT`, and `DTS_SHOWNONE` where a date may be blank | Typing a date still works; the calendar is the drop-down. |
| `tbSuperBtn` | `BUTTON` with `BCM_SETIMAGELIST` (the Fluent icons) | Command buttons move to the page's command bar. |
| `tbSpread`, read-only | `fx::Grid`, kept, made virtual | |
| `tbSpread`, entry | `fx::EntryGrid` (§6.4) | |
| Code fields (account, customer, …) | `fx::Lookup` (§6.3) | |
| Check boxes, radio buttons, combos | unchanged, standard | |

The `tb*` classes stay registered until the last dialog that uses them goes (§12).

### 6.2 Validation

Each field may have a validator: required, a length, a number range, an amount, a date in an open period, a code that exists. It runs when the field loses the focus and on Save. A failure marks the field (a balloon tip, `EM_SHOWBALLOONTIP`, and the field's label in the error colour) and Save stays possible to try, saying what's wrong. The rules are the ones the screens check today, moved into one place per page.

### 6.3 Lookups

`fx::Lookup` is an edit with a drop-down list (`WC_COMBOBOXEX` with a custom list, or an edit with a popup ListView). As you type, it lists matches by code and by name ("4100" or "sales"); Enter or a click picks one, and the field shows the code with the name beside it ("111101  Accounts receivable - trade"). F4 opens the full list. A code typed in full and Tab works as today, with no list. Sources: accounts (with depts), customers, vendors, med reps, products, doctors, banks, periods.

### 6.4 The entry grid

`fx::EntryGrid` is a ListView in report mode with in-place editors: an edit, a lookup or a date picker positioned over the cell being edited.
- Tab and Shift+Tab move across cells; Enter commits and moves down; Esc cancels the cell; F2 edits the cell in place.
- Insert adds a line; Ctrl+Delete removes one (with Undo, Ctrl+Z, for the last removal).
- Each cell has the column's validator; a bad cell is marked, and the line's total doesn't count it.
- It's built on `fx::Grid`, so the look, the DPI handling and `/stress` stay the same.

---

## 7. Feedback

### 7.1 Where a problem shows

| Kind | Where | Example |
|---|---|---|
| A field is wrong | On the field (§6.2) | "Not an account in the chart." |
| The page can't do something | The information bar at the top of the page, with a link to fix it where there is one | "Period 6 is closed. Open it in Company profile." |
| Something to know | The information bar, in the info colour; dismissible | "C002 has 600.00 overdue since 11/07/26." |
| Something done | The status bar's last-action field | "Voucher 104 posted." |
| A decision | A task dialog (§7.2) | "Post 2 vouchers?" |

The information bar is a static row with an icon, text and an optional link (`WC_LINK`), drawn in the system's info, warning or error colours.

### 7.2 Decisions

`TaskDialogIndirect`, owned by the workspace, with:
- a short main instruction ("Delete customer C004?");
- the consequence ("Only the customer is deleted. It can't be undone.");
- buttons named for the actions ("Delete customer", "Keep it"), the safe one the default;
- for choices between more than two things, command links.

`MessageBoxW` stays only in the parts not yet redesigned (§12).

### 7.3 Wording

Messages say what happened and what to do, in the program's words, as Specs #2–#7 already write them. The original's string resources that pages no longer show stay in `gl.rc` until their dialogs go.

---

## 8. Commands and access

- **The command table** (`gl/port/Commands.{h,cpp}`): one row per command: ID (today's, unchanged), label, icon, shortcut, help text, module, kind (page, task, report, action), and the function that runs it.
- **From it:** the menu bar, the toolbar, the navigation pane, the command search, and the accelerators are all built from the table. A command is added in one place.
- **The menu bar stays** (Q1): the same top-level menus and items as today, so a user who knows the menus loses nothing. `prune_rc.py` stops emitting the MANILA menu once the table builds it.
- **IDs stay:** every command keeps its ID, so the frame's routing, `/stress`, `/checkres` and Custom Report's "run as from the menu" keep working.
- **Rights** (D6): `accessm` and `accessl` keep their layout and meaning: a level's menu code is one character per `accessm` row in COUNT order. What changes is the lookup. The table maps each command to its `accessm` row by ID, so a right is checked by command (`MayRun(id)`), not by walking the rows in step with the code. A command without a row is allowed, as today. The menu, toolbar, navigation pane, search and page buttons all ask `MayRun`, so a right greys a command everywhere at once.
- **User Access** (1159, 1160) becomes a list-and-detail page: users on the left, and for the selected user's level, the commands grouped by module with a check box each, in place of the PLUS/MINUS grid.

---

## 9. Data and threading

### 9.1 One screen at a time no longer

Today's screens assume they're alone: `MainDialog::Cancel` closes every table (`cb.closeAll(); cb.init()`). With several pages open that would pull tables from under the others. So:
- **A page owns its tables:** it opens the `Data4` objects it needs and closes them itself; nothing closes another page's (D7).
- `closeAll` is removed from page code; it stays in the old dialogs until they go, which run modally and so still have the database to themselves (§12).
- **Writes are serialised.** `cb4sqlite::Transaction` isn't nestable; only one write runs at a time, on the UI thread or in the one background job (§9.2). A page saving while a job runs waits, with "Waiting for the posting to finish…" in its information bar.

### 9.2 Background jobs

- **What:** posting, Recover and Cancel (in bulk), the AR and AP transfer, the depreciation run, year end, the purges, Create cheques, backup and restore, and building a large report.
- **How:** a job runs on a worker thread with **its own connection** (`Code4` opened on the same file; SQLite connections aren't shared between threads). It reports progress with `PostMessage` to the page; the page shows a progress bar and, where the core can stop between steps, Cancel.
- **The database:** journal mode **WAL**, so pages can keep reading while a job writes (D8). `busy_timeout` is set on every connection. Backup checkpoints first, and copies one file, as today. Q6.
- **While a job runs,** commands that would conflict with it (another posting, the year end) are disabled with the reason in their tooltip; everything else stays usable.
- **The cores don't change** for this: a job calls the same function the dialog calls, on its own connection. Where a core loops over vouchers, a progress callback is added (optional; the dialogs pass none).

### 9.3 Keeping pages current

When a save or a job changes data, the workspace broadcasts which tables changed (`WM_APP_DATACHANGED`, with a bit set per table group). Open pages refresh what shows those tables: the voucher list after posting, a customer's owed figure after a receipt, the home page's counts. A page with unsaved changes isn't refreshed under the user; it shows "This record has changed since you opened it" and offers Reload.

---

## 10. Layout, DPI and state

- **Anchors:** `fx::Layout` keeps, for each control of a page, which edges follow the page's (left, top, right, bottom). On `WM_SIZE` it moves and sizes them with `DeferWindowPos`. Lists and grids take the extra room; fields keep their height. A page has a minimum size, and scrolls below it.
- **Splitters:** the list and detail split (§5.1) and the navigation pane's width can be dragged; both are remembered.
- **DPI:** per-monitor v2 as today. Pages are created from their templates at the monitor's DPI, and re-laid on `WM_DPICHANGED_AFTERPARENT`. Icons are reloaded at the new size (the UI refresh's code).
- **What's remembered, per user:** the window's placement, the navigation pane's width and whether it's collapsed, pinned tabs and the tab that was active, each list's column widths and sort, favourites and recent reports. They're stored in the company database, in a new port table `GL_UISTATE` (user, key, value), not in `GL.INI`, so they follow the user to another PC with the same company (D10). The `GL.INI` window positions the original kept are no longer read.

---

## 11. Windows 11 touches

- **Backdrop:** the workspace sets `DWMWA_SYSTEMBACKDROP_TYPE` to Mica on Windows 11 22H2 or later, behind the navigation pane and title bar. On earlier systems, or with transparency effects turned off, it's the plain window colour. The page area stays opaque, for legibility.
- **Title bar:** `DWMWA_USE_IMMERSIVE_DARK_MODE` follows the system's app theme. Rounded corners are Windows' default.
- **Accessibility:** every control has a UI Automation name (its label, through `SetWindowText` or a labelled-by relation), lists and grids expose their rows and cells (the ListView's own provider), and the information bar raises a notification event so screen readers announce it. High contrast uses system colours throughout.
- **Font:** the system message font, from `SystemParametersInfoForDpi`, as the UI refresh does.

---

## 12. Coexistence and migration

- **One window from the start.** M0 replaces the MDI frame with the workspace. Reports and the voucher list, today's MDI children, become tabs at once.
- **Old screens still open.** A command whose screen hasn't been redesigned runs its dialog, modally, over the workspace, as today. Its look, behaviour and tests are unchanged.
- **Screen by screen.** Each milestone moves a group of screens to pages. A moved screen's dialog template stays in `gl.rc` until the milestone's acceptance passes, then `prune_rc.py` drops it.
- **The order** is chosen so the most-used screens improve first, and each pattern is proved on one screen before it's applied to the rest (§16).
- **Nothing to migrate in the data:** the only additions are `GL_UISTATE` and WAL mode.

---

## 13. Decision record

| # | Decision | Why |
|---|---|---|
| D1 | C++ and the Windows SDK only: user32, comctl32 v6, DWM, UIA. No MFC, ATL, WinUI, XAML or .NET. | The user's direction. It keeps the build, `win32fx` and every test as they are, and adds no runtime. |
| D2 | The portable cores are unchanged; pages call the same functions the dialogs call. | Seven specs of tested behaviour; the acceptance sheets stay true. |
| D3 | One top-level window with a navigation pane and tabs, in place of MDI. | Several screens open at once, a familiar modern layout, and no MDI child-window quirks. |
| D4 | Three page patterns (list and detail, document entry, task) for every screen. | Learnt once, used everywhere; one implementation of each to test. |
| D5 | Standard controls in place of `tb*`: date picker, edit with validators, lookups, an entry grid on `fx::Grid`. | Native look and behaviour, accessibility and DPI for free; less custom code. |
| D6 | `accessm` and `accessl` keep their layout; rights are looked up by command ID. | No change to stored rights or to the original's level codes; one check used everywhere. |
| D7 | Each page owns its tables; `closeAll` leaves page code. | Several open pages must not close each other's tables. |
| D8 | WAL journal mode, `busy_timeout`, and a connection per background job. | Pages keep reading while a job writes; SQLite connections aren't shared across threads. |
| D9 | Pages know whether a record is new or existing; the "already added, modify?" question goes. | The original asked because a screen showed one record without knowing where it came from. |
| D10 | UI state per user in a port table, `GL_UISTATE`. | It follows the user, and `GL.INI` stays for settings that are the company's. |
| D11 | The menu bar stays, built from the command table (Q1). | Thirty years of habit; the navigation pane and search are added, not substituted. |

---

## 14. Development fixture

No new data. The fixture gains `GL_UISTATE` (empty) and is opened in WAL mode. Tests that check a page's figures use the Specs #2–#7 fixture as they are.

---

## 15. Acceptance criteria (outline)

1. **Every command has a home:** each of today's 116 menu items is in the menu, the navigation pane or a page's command bar, runs, and is greyed for a level without the right.
2. **The figures don't change:** the Spec #1–#7 sheets' figures are the same when their steps are done on the redesigned pages (each sheet gets a redesign column for the steps that move).
3. **Several pages at once:** open a customer, a voucher being typed, a report and the home page; move between them; nothing is lost and nothing closes another's tables.
4. **List and detail:** find a customer by typing part of the name; change and save it; the "already added" question doesn't appear.
5. **Entry:** type a 5-line GL voucher without the mouse; the totals follow; Post appears only when it balances.
6. **Feedback:** a wrong account marks the field; a closed period shows in the information bar; deleting asks in a task dialog with named buttons.
7. **Background work:** post a large set of vouchers while browsing the chart of accounts; the progress shows and the lists refresh when it ends.
8. **State:** close and reopen the program; the window, pinned tabs, column widths and favourites come back.
9. **Keyboard and accessibility:** every action without the mouse; Narrator reads each field's label and the information bar.
10. **DPI and look:** 100%, 150% and a move between monitors; Windows 10 and 11.
11. **Regression:** `/checkres` and `/stress` (now over pages too) exit 0; the test suites pass.

---

## 16. Milestones (the plan comes after review)

| Milestone | Delivers |
|---|---|
| S8-M0 | The workspace (§4) replacing MDI; the command table (§8) building the menu, toolbar and navigation; rights by command; old screens opening modally over it; reports and the voucher list as tabs. |
| S8-M1 | The page framework (`fx::Page`, `fx::Layout`), the controls (§6), the information bar and task dialogs (§7), `GL_UISTATE`. Proved on one list-and-detail page: **Customers**. |
| S8-M2 | The other list-and-detail pages: Vendors, Chart of Accounts, Departments, the master lists, fixed asset types and assets, Users and User Access. |
| S8-M3 | `fx::EntryGrid` and the document-entry pattern, proved on the **GL voucher**, then AP invoice and cash vouchers, AR invoices and receipts. |
| S8-M4 | WAL mode and background jobs (§9); the task pages: posting, transfer, depreciation, year end, purges, cheques, backup. |
| S8-M5 | The report gallery, options panels, preview tabs and the designer as a page (§5.4); the home page (§5.5); command search (Ctrl+K). |
| S8-M6 | Windows 11 touches and accessibility (§11); a keyboard audit; `prune_rc.py` drops the dialogs no longer used; the `tb*` classes go if nothing uses them. |
| S8-M7 | Acceptance: the sign-off sheet, the earlier sheets' redesign columns, the finance team's walkthrough. |

---

## 17. Risks

| # | Likelihood | Risk | Mitigation |
|---|---|---|---|
| R1 | High | The finance team is used to the old screens; a new layout slows them down at first, or meets resistance. | Show them the mockup before M1 (Q2); keep the menu bar and the keys they know; move the most-used screens first and ask after each. |
| R2 | Medium | Several pages open at once expose shared state the cores assumed was theirs alone (globals such as `szUser`, the current period, cached tables). | M0 lists every global the cores read; pages read them, never write them; the data-changed broadcast (§9.3) is the only way state moves. |
| R3 | Medium | WAL mode changes the files beside the database (`-wal`, `-shm`) and how a copy is taken. | Backup checkpoints and copies one file, as today; restore and Company Files are tested in WAL; a test that a copy taken mid-job opens clean. |
| R4 | Medium | The entry grid is the most complex control; editing, validation and keys in a ListView have many edge cases. | It's proved on the GL voucher alone first (M3), with its own test page under `/stress` and a keyboard-only acceptance step. |
| R5 | Low | A background job and a page both wanting to write. | Writes are serialised (§9.1); a page waits with a message; a test runs a save during a job. |
| R6 | Low | Mica and the dark title bar look wrong on some systems or with high contrast. | They're applied only where supported and turned off with high contrast or transparency effects off. |
| R7 | Medium | The redesign is long (eight milestones) and could stall halfway, with two styles in the program. | Each milestone ships a working program; the order puts the most-used screens first, so a stop at any point still leaves the program better. |

---

## 18. Still open

- **Q1 The menu bar:** keep it (D11), or rely on the navigation pane and search alone? *Assumed: keep it.*
- **Q2 The finance team's review:** they should see the mockup (and later a build) before M1. Who, and when? *Assumed: before the plan is written.*
- **Q3 Enter:** in the original, Enter in a code field looked the record up. In the redesign, should Enter in a form move to the next field (as many accounting programs do), or press the default button? *Assumed: Enter moves to the next field in forms and grids; Ctrl+Enter saves.*
- **Q4 How long old dialogs stay reachable** once their page exists: one milestone, or until acceptance? *Assumed: until the milestone's acceptance passes.*
- **Q5 The home page's tiles** (§5.5): which figures matter most to the finance team? *Assumed: the list in §5.5.*
- **Q6 WAL mode** (D8): acceptable for the deployed company? It's one more file beside the database while the program runs. *Assumed: yes.*
- **Q7 Windows 10:** still to be supported? The redesign works there without Mica. *Assumed: yes, without Mica.*
- **Q8 One company at a time:** the workspace still opens one company. Is comparing two companies side by side wanted? *Assumed: no; a later spec if so.*
