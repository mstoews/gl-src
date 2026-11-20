# Spec #9 — Implementation Plan: the Last Dialogs as Pages (Spec #8 gap G3)

**Spec reference:** `gl/docs/superpowers/specs/2026-11-19-owl-to-win32-sdk-port-spec8-design.md`, §5.1 (list and detail) and §5.3 (task pages). This plan adds no design of its own beyond the decisions in [Decisions](#decisions) below.
**Gap:** G3 of `gl/docs/superpowers/specs/2026-11-19-spec8-acceptance.md` ("Budget Data, Company Profile's accounts and banks, Print Cheques, Confirm Printed Cheques, Restore and Bank Reconciliation are still dialogs." Deferred: a later spec).
**Date:** 2026-11-20
**Plan status:** Reviewed; Q1–Q4 answered (2026-11-20). S9-M0 built.

**Paths:** relative to the repository root. The screens today:

| Screen | Command | Code | Template |
|---|---|---|---|
| Budget Data | `ID_ACCTBUDGET` 10074 | `gl/port/ChartOfAccounts.cpp`, `Mode::Budget` | 1017 |
| Company Profile | `ID_COMPANYPROFILE` 6015 | `gl/port/CompanyProfile.{h,cpp}`: 1021, with Periods (1028), Account Setup (1033) and Cash Accounts (1034, list 1011) | 1021, 1028, 1033, 1034, 1011 |
| Print Cheques | 4091 | `gl/port/reports/ApReports.cpp`, `ApCheques`: a report, already a preview tab | none |
| Confirm Printed Cheques | 8074 | `gl/port/ApPayments.cpp`, `ConfirmDialog` | `IDD_AP_CONFIRM` (`port.rc`) |
| Bank Reconciliation | 6085 | `gl/port/ApPayments.cpp`, `BankRecDialog` and `ClearDialog` | 1046, 1051 |
| Restore Databases | `ID_RESTORE` 1074 | `gl/port/Backup.cpp`, `RunRestore`: a file dialog and message boxes | none |

New pages go in `gl/port/pages/`, their models in portable files beside them, templates in `gl/res/port.rc` or `gl/res/tools/make_pages.py`'s generated `pages.rc`, and IDs in `gl/port/PortIds.h`, as in Spec #8.

This plan finishes the Spec #8 design for the screens it didn't move. If a milestone finds the design wrong for one of them, **stop, fix the Spec #8 design (or this plan's decisions), then resume.**

**Read first, as in Specs #2–#8.** Each milestone starts by re-reading the dialog it replaces and the core it calls. A page does what its dialog did, through the same core functions, in the same order. A check the dialog makes in its own code moves to the page's validators unchanged, or into the core with a test.

**What the survey found** (2026-11-20, against `main` at `e491ee9`):
- **Print Cheques isn't a dialog.** 4091 runs `ApCheques` straight into a preview tab (`RunApReport`). It changes nothing: Confirm Printed Cheques does that. What G3 lacks for it is a place in the cheque cycle, not a page (D5).
- **Confirm Printed Cheques doesn't print.** The Spec #8 plan's M4 note ("which print") is wrong for it. It lists the made cheques, confirms a range and renumbers a spoiled cheque.
- **Company Profile is four nested modal dialogs over three tables:** GL_PROF (the company), GL_PDS (the periods) and GL_SETAT (the eight GL accounts, and the banks, TYPE="CA"). Its OK and Cancel run `cb.closeAll(); cb.init()`.
- **Budget Data is the account screen with a 12-row grid.** `fx::model::EntryGridModel` always keeps a blank row at the end and has no fixed row count.
- **Restore is the only one of the six that `IsWritingCommand` gates.** It closes and reopens `cb`, which would pull tables from under every open page.
- **No `gl_tests` case drives Company Profile's writes.**

**Where it runs.** As Specs #5–#8: Windows 11 with Visual Studio 18 (v145), each milestone built, tested and driven in the app as it is written.

**Split each part in two**, as Spec #8: the model is portable and tested in `gl_tests`; the Win32 page is thin.

---

## Decisions

| # | Decision | Why |
|---|---|---|
| D1 | **Budget Data is a tab of the account** on the Chart of Accounts page (Spec #8 plan M2 task 2): Period, Budget (edited), Current year and Previous year, 12 rows. Saving the account's page saves its budget, through `SaveBudget`. The 10074 command opens the Chart of Accounts page on that tab. | One account, one page; the budget is the account's. |
| D2 | **`EntryGridModel` gains a fixed-rows mode:** no blank row at the end, no Insert or Ctrl+Delete, every row's read-only columns kept. Budget Data, the periods and the eight accounts use it. | The three grids have a fixed number of rows. The model is shared, so the change is tested once. |
| D3 | **Company Profile is one page with four tabs:** Company (GL_PROF, the fiscal year shown, the data path, the `[WITHHOLDING] TAX` setting), Periods (GL_PDS, 12 rows: start, end, description, closed), Accounts (the eight GL_SETAT GL rows, each an account lookup) and Banks (a list and detail over GL_SETAT TYPE="CA"). **Save** writes the tabs that changed, each through the code its dialog uses. No `closeAll` (Spec #8 D7). | §5.1 named the accounts and banks; the company and periods share the screen today, and splitting them would scatter one screen over four commands. |
| D4 | **The rules don't change:** Periods saves without checks, as 1028 does; Accounts refuses an account that isn't in the chart (8121); Banks asks nothing on a change (Spec #8 D9) and asks before Delete; "Reset the depreciation journal flag" is a command on the Periods tab, asking as 6025 does. | Spec #8 principle 1. Any new period check is Q1. |
| D5 | **The cheque cycle is one task page,** "Cheques", with three steps in a row: **Print** (opens 4091's preview tab, as today), **Confirm printed** (Confirm Printed Cheques: the made cheques listed, the first and last that printed, one action through `ConfirmPrinted`), and **Renumber** (a row's command, through `RenumberCheque`). 4091 and 8074 both open it; 4091 keeps opening the preview too (Q3). | Print and Confirm are one job for the user, done one after the other. Print stays the report it is. |
| D6 | **Bank Reconciliation is a task page:** the cheques not cleared, with check boxes, a date, and **Clear**, which calls `ClearCheque` once per checked cheque with that date (each its own transaction, as Spec #8 M4's Post does). **Void** is a row's command, asking as today, through `VoidCheque`. Refusals (not confirmed printed; a closed period) go on the row. | Clearing a statement's worth of cheques one dialog at a time is the slowest part of today's screen; the core is called the same way, once per cheque. |
| D7 | **Restore is a task page,** beside Backup: the file (a Browse button and the file's checks from `CheckRestoreFile`, shown before the action), the consequence in plain words, and **Restore from <file>**. It runs on the UI thread, not as a job, because it closes `cb`. Before it runs, every other page is closed: pages with unsaved changes are listed and asked about once, as closing the program does; Home and the Restore page stay. After it, the workspace broadcasts every data group changed and reapplies rights, as the frame does today. | Restore replaces the database under everything; closing the other pages first is the only safe order. |
| D8 | **Every new write joins the job gate:** the budget save, Company Profile's Save, Confirm, Renumber, Clear and Void stop while a job writes, with the job's name in the information bar (Spec #8 M4's rule); Restore stays in `IsWritingCommand`. | Writes are serialised (Spec #8 §9.1). Today these dialogs aren't gated. |
| D9 | **Each old dialog stays as a "(classic)" entry** until this plan's acceptance passes, and then goes with Spec #8's removals (its M6 task 4, gap G4). | Spec #8 Q4. |

---

## Questions (answered 2026-11-20)

The user confirmed each assumption.

- **Q1 Period checks.** 1028 saves any dates, overlapping or out of order. Should the Periods tab refuse a period that ends before it starts, or overlaps the next? **No:** the rules don't change (D4).
- **Q2 Restore and open pages.** Close the other pages (D7), or refuse to restore until the user closes them? **Close them,** asking once about unsaved changes.
- **Q3 Print Cheques' command.** Should 4091 open the Cheques page (with Print as its first step), or keep opening the preview straight away? **The preview, as today;** the Cheques page has its own entry.
- **Q4 Bank reconciliation by statement.** One date for every cheque cleared at once (D6), or a date per cheque? **One date per Clear;** a cheque cleared on another date is cleared in its own run.

---

## Milestone summary

| # | Milestone | Size | Depends on |
|---|---|---|---|
| 0 | Read; `EntryGridModel` fixed rows; a record page's tabs | M | none |
| 1 | Budget Data as the account's tab | S | M0 |
| 2 | Company Profile's page: Company, Periods, Accounts, Banks | M | M0 |
| 3 | The Cheques page and the Bank Reconciliation page | M | none |
| 4 | Restore as a task page | S | none |
| 5 | Acceptance: the sheets, the figures, sign-off | S | all |

S is less than a day, M is 1–3 days. **Parallel work:** M3 and M4 need only Spec #8's task pages and can run beside M0–M2. M1 and M2 both touch `make_pages.py` and the record page, so they run one after the other.

---

## Milestone 0 — Fixed-row grids and tabs on a record page

> **As built (2026-11-20; built and run on Windows 11, VS 18, v145).**
> - **Fixed rows** (task 2, D2): `EntryGridModel::SetFixedRows(n)`:
>   - **The rows:** exactly `n`, with no blank row; `Clear` gives `n` blank rows, and `SetRows` pads or cuts to `n`.
>   - **The keys:** Insert, Ctrl+Delete and Ctrl+Z do nothing; Tab or Enter on the last row's last cell stays there.
>   - **Back to normal:** `SetFixedRows(0)` is the usual grid again.
>   - **The view:** `fx::EntryGrid` is unchanged, since it passes every key to the model.
> - **Tabs** (task 3; `make_pages.py`, `RecordPage.h` `TabDesc`, `RecordPages.cpp`):
>   - **Where they go:** a page's `tabs` sit below its groups, across both columns.
>   - **The strip** (21004) is a Tab control that is its row of tabs only, with no body. The tabs' controls are siblings below it, so nothing overlaps and nothing needs `WS_CLIPSIBLINGS` or a themed tab body.
>   - **A tab** holds its own groups, or one grid (21005-21009, an `fx::EntryGrid` over the model's `Grid(key)`).
>   - **Labels on a tab** get IDs (21400 on), so the page can hide them; the page's own labels stay -1.
>   - **On the page:** choosing a tab shows its controls and hides the others'. Save first commits a grid's edit. A field refused on a hidden tab, or a grid cell refused, brings its tab forward. A grid edit marks the page dirty.
> - **Grids in models:** `RecordModel::Grid(key)` (null by default) and `LoadGrids(key)`.
>   - **When `LoadGrids` runs:** from `New` and `Select` only, not from `Read`. `Save` calls `Read` to see whether a new record's key is taken, so loading grids in `Read` would have wiped what was typed before the write.
> - **The tab check page** (`IDD_PG_TABCHECK` 20029, `k_tabCheck`; task 4):
>   - **What it is:** a record kept in memory, with a Details tab (a note, an amount, an account lookup) and a Budget tab (Budget Data's 12-row grid). No command opens it.
>   - **Deviation:** the plan named "/stress's entry-grid test page", but there's no such page (the voucher pages are what `/stress` types into). The tab check page takes its place.
>   - **What `/checkres` checks:** its form loads, its captions fit, and every control on every tab has an anchor.
>   - **What `/stress` does:** it opens the page 25 times, chooses each tab, types a budget into the grid, saves, and moves to the other record.
> - **Tests** (task 2 and the grids; `gl_tests` 222, 2 new):
>   - **`test_grid.cpp`:** every key on a fixed grid; `SetRows` padding and cutting; totals; back to normal.
>   - **`test_pages.cpp`:** a model's grid loaded on New and Select, and kept through Save's key check, both when the key is free and when it's taken.
> - **The existing pages:** their forms are unchanged; their descriptors only gain an empty tab list.
> - **Checked:** Debug and Release `/W4 /WX` clean. `gl_tests` 222, `cb4sqlite_tests` 79 and `report_tests` 19 pass in both. `/checkres`, `/checkaccess` and `/stress` (3 runs) exit 0 in both.
> - **For M1 and M2:** `/checkaccess` opens pages from their commands, so it hasn't yet seen a tabbed page. Its Tab-reach check will meet hidden controls on M1's Chart of Accounts and M2's Company Profile, and must count only the chosen tab's.

**Goal:** the two framework pieces M1 and M2 need.

### Tasks

1. **Read** `ChartOfAccounts.cpp` (Budget mode), `CompanyProfile.cpp` (all four dialogs), `Accounts.h`'s `ReadBudget` / `SaveBudget`, `win32fx/include/model/EntryGridModel.h`, `pages/RecordPage.h`, `RecordPages.cpp` and `gl/res/tools/make_pages.py`. Note every check and message the dialogs make (the M1 and M2 validators).
2. **Fixed rows** (D2): `EntryGridModel` takes a row count; with one, it keeps no blank row, refuses Insert and Ctrl+Delete, and Enter on the last row stays there. `fx::EntryGrid` needs no change beyond not showing those keys' commands. `gl_tests` cases: the keys on a fixed grid; a voucher grid unchanged.
3. **Tabs on a record page:** `make_pages.py` learns a page's tabs (a Tab control under the record's key fields, each tab a group of fields or a grid). `RecordPage` shows the active tab's controls; the model reads and writes every tab. Anchors and `/checkres`'s checks cover each tab.
4. **`/checkres`, `/stress`:** a fixed grid in `/stress`'s entry-grid test page.

### Exit criteria

- The suites pass; `/checkres` and `/stress` exit 0, Debug and Release.
- The voucher pages' tests are unchanged.

---

## Milestone 1 — Budget Data as the account's tab

**Goal:** the budget is entered on the Chart of Accounts page (D1).

### Tasks

1. **The tab:** a "Budget" tab on the Chart of Accounts page, a fixed 12-row grid (Period and Current/Previous year read-only, Budget edited with 1017's amount rule).
2. **The model:** the chart's model reads `ReadBudget` with the account, and Save calls `SaveBudget` when the budget changed. 1017's checks move: no account, no budget save (3081's case can't arise on a page that shows a saved account; a new account saves its budget after it's added).
3. **The command:** 10074 opens the Chart of Accounts page on the Budget tab; "Budget Data (classic)" opens 1017 (D9).
4. **The gate** (D8): Save refuses while a job writes.
5. **Tests:** `gl_tests` through the model: an account's budget read, changed and saved, read back with SQL against `ReadBudget`; the Spec #2 sheet's Budget Data step's figures. `/stress`: the page opened on the tab.

### Exit criteria

- The Spec #2 sheet's Budget Data step passes on the page.
- 10074 doesn't open a modal dialog except through its classic entry.

---

## Milestone 2 — Company Profile's page

**Goal:** the company, its periods, its accounts and its banks on one page (D3, D4).

### Tasks

1. **The page:** a record page with no list (one record, the company) and four tabs:
   - **Company:** GL_PROF's fields; the fiscal year's first and last dates (read-only, from GL_PDS); the data path (read-only); the withholding check box (GL.INI, as 1021 writes it).
   - **Periods:** a fixed 12-row grid of start, end, description and closed. **Reset depreciation flag** is the tab's command (6025's question, then `gl_fa.mode` blanked as today).
   - **Accounts:** a fixed 8-row grid, an account lookup and dept per row, the name shown; the 8121 refusal on the row.
   - **Banks:** a list and detail over GL_SETAT TYPE="CA" (account, dept, name), with New, Save and Delete as the other record pages have; Delete asks in a task dialog (6024's question).
2. **The model** (`pages/CompanyModel.{h,cpp}`, portable): reads and writes the three tables through the code the dialogs use, moved out of `CompanyProfile.cpp` into a portable file where it's in the dialog today. Save writes only the tabs that changed, in one transaction. No `closeAll`.
3. **After saving:** the workspace broadcasts the masters and GL balances groups, and the status bar's period refreshes (today's `RefreshStatus`).
4. **The command:** 6015 opens the page; "Company Profile (classic)" opens 1021 (D9). The gate (D8).
5. **Tests:** `gl_tests` (new: no case drives these writes today): each tab saved and read back with SQL; an account not in the chart refused; a bank added, changed with nothing asked, and deleted; the depreciation flag reset; the classic code and the model compared table by table on two fixture copies. `/stress`: the page with each tab. `/checkaccess`: the tabs' names and Tab order.

### Exit criteria

- The Spec #2 sheet's Company Profile steps (1033, 1034, 1011) pass on the page.
- Saving leaves every other open page's tables open.

---

## Milestone 3 — The Cheques page and Bank Reconciliation

**Goal:** the AP cheque cycle on task pages (D5, D6).

### Tasks

1. **Read** `ApPayments.cpp` (`ConfirmDialog`, `BankRecDialog`, `ClearDialog`), `Ap.h`'s `ConfirmPrinted`, `RenumberCheque`, `ClearCheque`, `VoidCheque` and `LoadChequesIn`, and `reports/ApReports.cpp`'s `ApCheques`. Correct the Spec #8 plan's M4 note that Confirm Printed Cheques prints.
2. **The Cheques page** (`TaskModels`, a new task):
   - the made ("P") cheques listed: cheque, vendor and name, date, amount;
   - **Print cheques:** opens 4091's preview tab;
   - **Confirm printed:** From and To, filled with the first and last numbers as today; the action is named for the range ("Confirm cheques 1001 to 1004") and asks today's question, then `ConfirmPrinted` in one transaction (as today, not per cheque);
   - **Renumber:** on a selected row, with the new number; `RenumberCheque`'s messages on the information bar.
3. **The Bank Reconciliation page:** the cheques not cleared (Made and Printed), with check boxes and a date; **Clear N cheques** calls `ClearCheque` per checked cheque, with progress; a Made cheque has no check box, with why ("Not confirmed printed: confirm it on the Cheques page."); **Void** on a row asks as today and calls `VoidCheque`, its result and its closed-period refusal on the information bar.
4. **The commands:** 8074 and the Cheques page's own entry open the Cheques page; 6085 opens Bank Reconciliation; their "(classic)" entries open the dialogs (D9). Both pages' actions take the job gate (D8). The home page's Print Cheques tile opens the Cheques page.
5. **Tests:** `gl_tests` through the models, on the Spec #5 sheet's cheque steps: a range confirmed, a spoiled cheque renumbered, three cheques cleared in one run and one voided, each compared table by table with the classic calls on a second fixture copy. `/stress`: both pages opened and listed.

### Exit criteria

- The Spec #5 sheet's print, confirm, renumber, clear and void steps pass on the pages.
- 8074 and 6085 don't open a modal dialog except through their classic entries.

---

## Milestone 4 — Restore as a task page

**Goal:** Restore beside Backup, with the open pages handled first (D7).

### Tasks

1. **Read** `Backup.cpp`'s `RunRestore`, `CompanyFiles.h`'s `CheckRestoreFile` and `RestoreDatabase`, and `ManilaGL`'s after-restore steps (status, rights).
2. **The page:** a task page, "Restore Databases": the file (Browse..., then `CheckRestoreFile`'s result and the file's date and size shown), the consequence ("Everything entered since that backup will be lost. The current database is kept as <name>.bak."), and **Restore from <file>**, greyed until a file passes the checks.
3. **Running it:** the workspace closes every page but Home and this one, asking once about unsaved changes (Q2); then `RestoreDatabase` on the UI thread; then every data group broadcast, rights reapplied and the status bar refreshed. A failure says whether the old database was reopened, as today.
4. **The command:** 1074 opens the page; "Restore Databases (classic)" runs today's code (D9). It stays gated (D8).
5. **Tests:** `gl_tests` through the model: a file refused by its check; a restore swapping the files and keeping the `.bak`, with the database reopened and readable (the existing `test_yearend.cpp` cases' fixtures). Driven in the app: a restore with a dirty page open asks once and closes it.

### Exit criteria

- The Spec #2 sheet's Restore step passes on the page.
- After a restore, every page that's opened again shows the restored data.

---

## Milestone 5 — Acceptance

1. **The sheets:**
   - the Spec #8 sheet's G3 becomes "Fixed (Spec #9)", with this plan named;
   - the Spec #2 and #5 sheets' "Redesign (Spec #8)" sections gain the steps now done on pages (Budget Data, Company Profile, Restore; print, confirm, renumber, clear and void);
   - their figures don't change.
2. **The figures:** a `gl_tests` case runs those steps through the page models in the sheets' order and checks every figure they quote.
3. Release build check, the walkthrough with a witness, triage, sign-off. Then the classic entries go with Spec #8's removals (D9, gap G4).

---

## Cross-cutting

- **Per-milestone gates,** as Spec #8:
  - `/W4 /WX` clean on Debug and Release x64;
  - the milestone's tests pass, and every earlier suite;
  - `/checkres`, `/checkaccess` and `/stress` exit 0;
  - screenshots of the new pages at 100 % and 150 %;
  - an entry in `gl/docs/superpowers/plans/2026-05-10-progress.md`;
  - the milestone's "As built" note here.
- **What not to do:**
  - Don't change what a core computes or writes, or a table's layout.
  - Don't call `closeAll` in a page. Restore closes `cb` only through the workspace's close-everything step (D7).
  - Don't change a command's ID.
  - Don't remove a classic dialog before this plan's acceptance has passed.
  - Don't edit `gl/res/gl.rc` by hand.

## Not in this plan

- **Transaction Inquiry** (the Spec #2 sheet's #9) is also still a dialog but isn't in G3. It can join this plan as a milestone if wanted.
- **Spec #8's removals** (gap G4) wait for Spec #8's sign-off, then this plan's.
- **Spec #8's G5** (no progress within a one-transaction task) is unchanged here: Confirm printed is one transaction, as today.

## Risks specific to the plan

| # | Severity | Risk | Mitigation |
|---|---|---|---|
| P1 | Medium | Company Profile's writes have no tests today, so the page could save something different from the dialogs without anyone noticing. | M2 task 5 compares the model with the classic code table by table on two fixture copies before the page is driven. |
| P2 | Medium | Restore closes the database under pages that hold `Data4` objects on `cb`. | D7: every other page closes first; a test opens pages, restores, and checks that none is left holding a closed table. |
| P3 | Low | The fixed-rows mode changes `EntryGridModel`, which the voucher pages use. | M0 task 2's tests include the voucher grid's keys, unchanged. |
| P4 | Low | Clearing many cheques at once clears one by mistake. | Checked rows only, the action names the count, and Void stays a separate, asked, per-row command. |

## How to use this plan

M0 → M1 → M2, with M3 and M4 beside them → M5. If the plan and the Spec #8 design conflict, stop, edit the design (or this plan's decisions), recheck the plan, then continue.
