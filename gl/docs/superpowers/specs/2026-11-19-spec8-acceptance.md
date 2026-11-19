# Spec #8 acceptance sign-off

This sheet records plan Milestone 7 (`gl/docs/superpowers/plans/2026-11-19-owl-to-win32-sdk-port-spec8-plan.md`, "Milestone 7"). It covers the acceptance criteria in spec §15 (`2026-11-19-owl-to-win32-sdk-port-spec8-design.md`): the redesigned workspace, its pages and their behaviour.

Fill in every blank (`____`) and set each **Result** to `PASS`, `FAIL` or `PASS with known gap`. A `FAIL` needs a triage line in [Gaps and triage](#gaps-and-triage): either fixed in Spec #8, or filed against a later spec.

**Which copy of `dev.db`:** the criteria run in order on one fresh copy (`gl\test\dev-fixture\setup.cmd Release`), each building on the one before, except #8 to #10, which need no particular data. Keep an untouched copy so the walkthrough can be repeated.

**The figures** below come from `gl_tests` ("The Spec #8 acceptance walkthrough's figures" in `gl_tests/test_acceptance8.cpp`). It runs the same steps, in the same order, through the models the redesigned pages use, and checks every figure quoted here. If a figure on screen differs, the test says which one is right.

**The earlier sheets:** each of the Spec #1 to #7 sheets now has a "Redesign (Spec #8)" section, saying where each step whose screen moved is done. Their figures don't change. Criterion 2 below asks for those sheets to be walked again on the pages.

**The classic screens:** until this sheet is signed off (spec Q4), every screen that became a page also has its old dialog, as a "(classic)" entry in the navigation pane beside the page's command. They follow the page's right.

Run the `sqlite3` queries from `x64\Release`, against `dev.db`, with the program closed or between screens.

## Run details

| Item | Value |
|---|---|
| Commit tested (`git rev-parse --short HEAD`) | ____ |
| Build machine | ____ |
| Test machine (fresh install, not the dev box) | ____ |
| Windows edition and build (`winver`) | ____ (targets: Windows 11 23H2 or later, x64; and Windows 10 22H2 for #10) |
| Display scaling | ____ (100% / 96 DPI, and 150% for #10) |
| A second monitor at another scale (for #10) | ____ |
| Narrator available (for #9) | ____ |
| Tester | ____ |
| Witness | ____ |
| Date | ____ |

## Before the walkthrough

### P1. Release build and hardening

As in the Spec #3 sheet (P1): build `gl.sln` `Release|x64` with `/W4 /WX`, then confirm the hardening options from `common.props`, or check them with `dumpbin /headers x64\Release\gl.exe`. The program now also links `dwmapi`, `oleacc` and `uiautomationcore`, all part of Windows.

| Check | Result | Notes |
|---|---|---|
| Release build, no warnings | ____ | |
| `/sdl`, `/guard:cf`; `/DYNAMICBASE`, `/NXCOMPAT`, `/GUARD:CF` | ____ | |

### P2. Automated checks (dev machine)

Run `gl\test\dev-fixture\setup.cmd` for each configuration first. From `cmd`, run each check with `start /wait ... /quiet` where it takes `/quiet`, then `echo %errorlevel%`. Each should print 0.

| Check | Debug | Release | Notes |
|---|---|---|---|
| `x64\<Config>\cb4sqlite_tests.exe` (79 test cases pass; WAL readers and writers, a backup during writes) | ____ | ____ | |
| `x64\<Config>\gl_tests.exe` (216 test cases pass; `[acceptance8]` is this sheet's figures) | ____ | ____ | |
| `x64\<Config>\report_tests.exe` (19 test cases pass) | ____ | ____ | |
| `gl.exe /checkres /quiet` (every page form: text fit, anchors) | ____ | ____ | |
| `gl.exe /checkaccess /quiet` (every page opened: names, Tab reach, tab order, access keys) | ____ | ____ | |
| `gl.exe /stress /quiet` (now also every page, the entry grid's keys, the task pages with a dry job, the home page and the gallery) | ____ | ____ | |
| `gl.exe /screenshots shots /quiet` saves the frame, the dialogs and the 37 pages; look at a few | ____ | ____ | |

### P3. Fresh-machine install

As in the Spec #3 sheet (P3), with the current `dev.db`.

| Check | Result | Notes |
|---|---|---|
| `gl.exe` starts with no Visual C++ runtime installed; login as `ADMIN` / `ADMIN` works | ____ | |
| With nothing pinned, the program opens on the **Home** page | ____ | |
| `sqlite3 dev.db "pragma journal_mode"` after the program has run returns `wal` (spec D8) | ____ | |

## The fixture

The Spec #6 company, current period 7 ("Oct 26").

- **GL vouchers:** 100 posted; 101 and 103 (an adjustment) open and balanced; 102 open, out of balance by 10.00.
- **AP:** purchase vouchers 1 and 2 open (V001), cash voucher 1 open.
- **AR:** sales vouchers 1 and 2 open; open items S-1003 2,200.00 (C001) and S-1004 600.00 (C002), 2,800.00 in all.
- **Journal rows waiting for Update AR and AP:** 19.
- **Users:** `ADMIN` / `ADMIN` (level A); `CLERK` / `clerk` (level B: the Maintenance menu greyed).

## The criteria (spec §15)

Log in as `ADMIN`. The window is the workspace: the navigation pane on the left (the menu's commands as a tree, Home first, the "(classic)" entries beside the commands they replace), the pages as tabs, the menu and toolbar as before.

### Home, before anything

- [ ] The **Home** page shows: "Period 7: Oct 26"; "3 GL vouchers to post"; "3 purchase and cash vouchers to post"; "2 sales vouchers to post"; "19 journal rows to transfer"; "Owed to us: 2,800.00"; "We owe: 4,900.00".
- [ ] Each tile opens its page (try "3 GL vouchers to post": the Post GL vouchers page).

| Result | Notes |
|---|---|
| ____ | |

### 1. Every command has a home

- [ ] Each of the menu's items runs from the menu and from the navigation pane (a sample of at least ten, across the menus, is enough; `gl_tests` `[commands]` checks all 116 against the menu).
- [ ] Log out and in as `CLERK`: the Maintenance commands are greyed in the menu and the navigation pane, and Ctrl+K doesn't offer them.
- [ ] A page's own commands (New, Save, Delete, Post, the task's action) are on its page.

| Result | Notes |
|---|---|
| ____ | |

### 2. The figures don't change

- [ ] Walk the Spec #1 to #7 sheets again, doing each moved step where its "Redesign (Spec #8)" section says. Every figure is the same.
- [ ] In particular (Spec #6's voucher 7): **Sales Invoice Vouchers**, customer C001, invoice S-1007, document DR-7, three lines of 1,100.00 on the customer's sales account. The voucher's lines show Sales 1,000.00 three times, VAT 300.00 and AR 3,300.00; Save makes sales voucher **7**; Post posts it. **Clear Receipts**: S-1007 owes 3,300.00; record 1,000.00; it owes **2,300.00**.

| Sheet | Result | Notes |
|---|---|---|
| Spec #1 | ____ | |
| Spec #2 | ____ | |
| Spec #3 | ____ | |
| Spec #4 | ____ | |
| Spec #5 | ____ | |
| Spec #6 | ____ | |
| Spec #7 | ____ | |

### 3. Several pages at once

- [ ] Open the Customers page on C001, a GL voucher being typed (two lines, not saved), a report's preview (Trial Balance) and the Home page. Move between them with the tabs and Ctrl+Tab.
- [ ] Nothing is lost: the voucher's two lines and the customer are still there.
- [ ] Closing one page doesn't close another's tables: close the preview, then save the customer (terms 30): it saves.
- [ ] Closing the voucher's tab asks about its unsaved lines (Save, Don't save, Cancel).

| Result | Notes |
|---|---|
| ____ | |

### 4. List and detail

- [ ] **Sales Customers**: New; customer C004, short name EASTCLINIC, name East Clinic, sales dept 20, terms 15, VAT 10; Save. "Customer C004 saved."
- [ ] Type "east" in the filter: the list shows C004 alone. Select it, change the terms to 45, Save: it saves, and no "already added, do you want to modify?" question appears. `sqlite3 dev.db "select terms from ar_cust where trim(vendorno)='C004'"` returns `45`.
- [ ] The customer's number can't be changed once saved.

| Result | Notes |
|---|---|
| ____ | |

### 5. Entry

- [ ] **General Ledger Voucher**, description "Acceptance". Without the mouse, type five lines into the grid (Tab moves on; Enter does the same):

  | Account | Dept | Document | Debit | Credit |
  |---|---|---|---|---|
  | 101000 | | ACC-1 | 100.00 | |
  | 102101 | | ACC-1 | 200.50 | |
  | 111101 | | ACC-1 | 49.50 | |
  | 410001 | 10 | ACC-1 | | 300.00 |
  | 410001 | 20 | ACC-1 | | 50.00 |

- [ ] The account names fill in; the totals follow each line; after four lines it reads "Out of balance by 50.00." and **Post** isn't shown; after the fifth, "Balanced." (350.00 each side) and Post appears.
- [ ] Ctrl+S saves it as voucher **104**. Post asks in a task dialog and posts it: `sqlite3 dev.db "select count(*) from gl_dist where batchno=104"` returns `5`.
- [ ] F2 edits a cell keeping its text, Esc drops the edit, Insert adds a line, Ctrl+Delete removes one, Ctrl+Z puts it back.

| Result | Notes |
|---|---|
| ____ | |

### 6. Feedback

- [ ] On a new GL voucher, type 999999 as an account: the cell is refused, and the information bar says "Not an account in the chart."; Esc drops it.
- [ ] Choose a closed period (6, if listed; or type a line and save with no open period chosen): Save marks the period and the bar says "Choose an open period."
- [ ] Delete a voucher or a record: a task dialog asks, with named buttons ("Delete voucher" / "Keep it"), not Yes and No.

| Result | Notes |
|---|---|
| ____ | |

### 7. Background work

- [ ] **Post Voucher > General Ledger**: the task page lists 101, 102 and 103. 102 has no check box and says "Out of balance by 10.00."; the summary reads "3 GL vouchers listed, 2 checked."
- [ ] **Post**; the task dialog asks "Post 2 GL vouchers?"; it runs in the background and ends with "2 GL vouchers done."
- [ ] With a larger set (the plan's drive added 300 vouchers to a scratch copy): progress shows "Working: n of m...", the Chart of Accounts can be browsed meanwhile, Stop ends after the voucher in hand, and the voucher list and the Home page's counts refresh at the end.
- [ ] While a job writes, a page's Save says "... is writing to the books: save again when it has finished." and writes nothing.
- [ ] **Update AR and AP**: "Made GL voucher 105 (AP, period 7), GL voucher 106 (AR, period 7)." (after #2's voucher 7 and receipt).
- [ ] Home now shows "3 GL vouchers to post" (102, 105, 106), "0 journal rows to transfer" and "Owed to us: 5,100.00".

| Result | Notes |
|---|---|
| ____ | |

### 8. State

- [ ] Pin the Customers page (its tab's menu, Pin), maximise the window, mark Trial Balance as a favourite in the report gallery, close the program.
- [ ] Open it again: the window is maximised, the Customers page is open, the Home page's favourites list Trial Balance and its recent pages are the ones used.

| Result | Notes |
|---|---|
| ____ | |

### 9. Keyboard and accessibility

- [ ] Every step of #4 and #5 can be done without the mouse (access keys, Tab, Enter, Ctrl+S, Ctrl+N; Ctrl+K finds commands, accounts, customers, vendors and vouchers).
- [ ] With Narrator on: moving through the Customers page and the GL voucher page reads each field's label ("Number", "Name", "Sales", "Sales dept"; "Date", "Period", "Description", "Voucher lines"), and an information bar message is read when it appears.

| Result | Notes |
|---|---|
| ____ | |

### 10. DPI and look

- [ ] At 100% and at 150%: the pages, the grid and the task pages are drawn at the scale, no text clipped (`/checkres` checks the forms at the system scale).
- [ ] Moving the window to a monitor at another scale redraws it at that scale.
- [ ] Windows 11 22H2 or later: the title bar has the Mica backdrop and follows the dark or light app theme; with transparency effects off, or with high contrast, there's no Mica. Windows 10: the same program, no backdrop.

| Result | Notes |
|---|---|
| ____ | |

### 11. Regression

- [ ] P2's checks pass (the test suites; `/checkres`, `/checkaccess` and `/stress` exit 0).

| Result | Notes |
|---|---|
| ____ | |

## Gaps and triage

Known before the walkthrough:

| # | Gap | Triage |
|---|---|---|
| G1 | The report options are dialogs before the preview (and again from its Refresh), not panels above it; the Report Designer is a dialog, not a page (plan M5 tasks 3 and 5). | Fixed (S8-M7): the options and a user report's questions are a row above the preview, with **Refresh**; the gallery opens the designer as a page with a live preview. The Spec #3 and #7 redesign tables are updated. |
| G2 | The report gallery has no Export... or Import... of report definitions (`.gldef`); Spec #7's #7, #8 and #9 export and import through "Custom Report (classic)". | Fixed (S8-M7): **Export...** and **Import...** on the gallery, the list's own code; Spec #7's redesign table updated. |
| G3 | Budget Data, Company Profile's accounts and banks, Print Cheques, Confirm Printed Cheques, Restore and Bank Reconciliation are still dialogs. | Deferred: a later spec. |
| G4 | Removing the classic dialogs, `MainDialog`, the old controls, `MdiFrame` and the MANILA menu resource (plan M6 task 4) waits for this sign-off. | After sign-off. |
| G5 | Long one-transaction tasks (Update AR and AP, the depreciation run, year end, Purge Paid Payables, Create Cheques) show no progress within the step; Stop works before they start. | Accepted / filed. ____ |
| G6 | `/screenshots` and `/checkres` run at the system scale only; the 150% set needs the display set to 150%. | Done by hand in #10. |
| G8 | The Create Cheques task page dates the cheques today and draws on GL_SETAT's first bank; neither can be chosen, and Hold / Force stay in the classic dialog. Spec #5's #6 uses "Create Cheques (classic)" for its pay date and bank. | Fixed (S8-M7): "Pay on:", "Bank:" and "First cheque:" on the page; the check boxes hold, release and force. Spec #5's redesign table updated. |
| G9 | The receipts page can't reverse a receipt; Spec #6's #6 uses "Clear Receipts (classic)". | Fixed (S8-M7): **Reverse last receipt**, dated by the page's Date. Spec #6's redesign table updated. |
| G7 | The GL voucher page has no Print: a voucher's slip prints from "General Ledger Voucher (classic)" or Reporting > Voucher Slips (Spec #3's #10). File > Print with no preview active still opens the old report list (1022); only Custom Report opens the gallery. | Fixed (S8-M7): **Print slip** on the GL voucher page (a preview tab), and File > Print with no preview opens the gallery; 1022 stays as Custom Report (classic). The Spec #3 redesign table is updated. |

Found in the walkthrough:

| # | Criterion | What happened | Triage |
|---|---|---|---|
| ____ | ____ | ____ | ____ |

## Sign-off

| Role | Name | Signature | Date |
|---|---|---|---|
| Tester | ____ | ____ | ____ |
| Witness | ____ | ____ | ____ |
| Finance team | ____ | ____ | ____ |

With this signed off, the classic entries can go (plan M6 task 4).
