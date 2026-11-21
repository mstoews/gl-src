# Spec #10 — Implementation Plan: the Four Remaining Dialogs as Pages

**Spec reference:** `gl/docs/superpowers/specs/2026-11-19-owl-to-win32-sdk-port-spec8-design.md`, §5.1 (list and detail) and §5.3 (task pages), as Spec #9 applied them. This plan adds no design of its own beyond the decisions in [Decisions](#decisions) below.
**Gap:** the Spec #9 sheet's K4 (Transaction Inquiry is still a dialog), and the three other menu commands that still open a dialog rather than a page: Fixed Assets > Disposal, Accounts > Statement Layout and the right-click menu's Delete Accounts.
**Date:** 2026-11-21
**Plan status:** Draft, for review. Q1–Q5 are open; the plan assumes the answer given with each.

**Paths:** relative to the repository root. The screens today:

| Screen | Command | Code | Template |
|---|---|---|---|
| Transaction Inquiry | `ID_GLTRANSINQ` 10072 | `gl/port/TransactionInquiry.{h,cpp}`; the lookup is `AccountTransactions` (`Vouchers.h`) | 1029 |
| Disposal | `ID_DISPOSAL` 6035; the Fixed Assets page's **Dispose...** | `gl/port/FixedAssetDisposal.{h,cpp}`; `DisposeAsset`, `GainOnSale`, `DeleteAsset` (`Assets.h`) | 1001 |
| Statement Layout | `CM_STATEMENT_LAYOUT` 20502 | `gl/port/reports/StatementLayoutDialog.{h,cpp}`; the core is `StatementLayout.h` | `IDD_STMT_LAYOUT` (`port.rc`) |
| Delete Accounts | `ID_ACCTDEL` 1006 (GLSUB, the right-click menu) | `gl/port/ChartOfAccounts.cpp`, `Mode::Delete` | 1010 |

New pages go in `gl/port/pages/`, their models in portable files beside them, templates in `gl/res/port.rc` or `make_pages.py`'s generated `pages.rc`, and IDs in `gl/port/PortIds.h`, as in Specs #8 and #9.

**After this plan, every menu command opens a page,** except the login, About and Reindex Databases (a message: an SQLite file needs no reindexing), which stay as they are, and the "(classic)" entries until their sheets are signed off.

If a milestone finds the design wrong for one of these screens, **stop, fix the Spec #8 design (or this plan's decisions), then resume.**

**Read first, as in Specs #2–#9.** Each milestone starts by re-reading the dialog it replaces and the core it calls. A page does what its dialog did, through the same core functions, in the same order. A check the dialog makes in its own code moves to the page's validators unchanged, or into the core with a test.

**What the survey found** (2026-11-21, against `main` at `67535f3`):
- **Transaction Inquiry is a read-only view of one account.** It reads nothing but `AccountTransactions(account, dept)`: the opening balance, the posted lines (period, voucher, description, debit, credit) and the balance. Its Search button shows and hides the account list, which the Chart of Accounts page already has. Spec #2's D13 corrections (exact account and dept; a blank dept refused with 3081) are in the core.
- **Delete Accounts is the Chart of Accounts dialog with "&Delete" on OK.** Its rules are the page's Delete's: not found (1026), and "must be empty" (1027, `HasActivity`). The page's `AccountsModel::Remove` already makes both checks. Only the right-click menu (GLSUB) reaches 1006.
- **Disposal is per asset and doesn't post.** It shows the cost, accumulated depreciation and book value, takes a date and a sale price, shows the gain or loss (`GainOnSale`), and on **Dispose** (8107's question) calls `DisposeAsset`, then shows the entry to make in Voucher Entry (Spec #4 D7). Its **Delete** is the Fixed Assets page's Delete, with the same refusal for a depreciated asset. Neither the sale price nor the entry is stored.
- **The Fixed Assets page's Dispose... opens the dialog modally** and reloads the asset afterwards (`RecordPages.cpp`, `RecordPage::OnButton`).
- **Statement Layout is an editor of an in-memory copy** of a statement's lines: a picker (IS, BS, CF), a read-only grid of the lines, the selected line's fields under it with Add, Insert before, Delete and Apply, then Check, Preview, Reset to default and Save. The core (`LoadLayout`, `SaveLayout`, `ResetLayout`, `CheckLayout`, `ParseRanges`, `ParseFormula`) is portable and tested (`test_statements.cpp`); the dialog's own logic is choosing a blank line number and asking about unsaved changes.
- **`EntryGridModel` has no choice column** (`CellKind` is Text, Upper, Amount, Number, Lookup) and no read-only grid mode beyond read-only columns.
- **Rights:** `RightOf(CM_STATEMENT_LAYOUT)` is the Chart of Accounts'; 10072, 6035 and 1006 have their own menu rights.

**Where it runs.** As Specs #5–#9: Windows 11 with Visual Studio 18 (v145), each milestone built, tested and driven in the app as it is written.

**Split each part in two**, as Spec #8: the model is portable and tested in `gl_tests`; the Win32 page is thin.

---

## Decisions

| # | Decision | Why |
|---|---|---|
| D1 | **Transaction Inquiry is an "Activity" tab of the account** on the Chart of Accounts page: a read-only grid of `AccountTransactions`' lines for the selected account and dept (Pd, Voucher, Description, Debit, Credit), with the opening balance as its first row and the balance as its last (Q1). 10072 opens the Chart of Accounts page on that tab. Search goes: the page's list is the account list. | One account, one page (Spec #9 D1). The inquiry is that account's posted history. |
| D2 | **A record page's grid can be read-only:** a tab's grid that the model fills on `LoadGrids` with any number of rows, that takes no edits, keeps no blank row, never marks the page dirty and isn't saved. Moving through it with the keys works as in any grid. | The Activity tab needs it; Disposal's entry (D4) uses it too. |
| D3 | **Delete Accounts opens the Chart of Accounts page** (its Delete makes the dialog's checks). The GLSUB entry stays, opening the page (Q5). | The page's Delete is the same operation. A second screen for it is the dialog's only reason to exist. |
| D4 | **Disposal is a "Disposal" tab of the asset** on the Fixed Assets page: the cost, accumulated depreciation and book value (read-only), **Date disposed** (today by default) and **Sale price** (inputs, not saved with the asset), the gain or loss and the entry to make in Voucher Entry (read-only, recomputed when an input changes, through `GainOnSale` and the dialog's entry text moved into the core). The page's **Dispose...** button becomes **Dispose**, enabled on that tab: 8107's question, then `DisposeAsset`; the information bar says the asset is disposed of and points at the entry on the tab. A disposed asset shows its date and Dispose is refused, as today. 6035 opens the Fixed Assets page on that tab. | The dialog is one asset's action, and the page already has the asset selected. Showing the entry before Dispose, not after, lets the user check it first. |
| D5 | **A record page's tab can hold action inputs:** fields the model reads for a command but doesn't save, that don't mark the page dirty, and a model hook (`Computed(values)`) that fills read-only fields from them. | D4 needs it; nothing else in the record changes. |
| D6 | **Statement Layout is a page of its own** ("Statement Layout", `pages/LayoutPage.cpp`, model `pages/LayoutModel.{h,cpp}`): the statement picker at the top, the lines in an `fx::EntryGrid` edited in place (No., Kind, Caption, Indent, Sign, Formula, Ranges; Q4), and the commands **Check**, **Preview**, **Reset to default** and **Save**. Insert and Ctrl+Delete add and remove lines; a blank No. is chosen on commit as the dialog chooses it (10 after the last, halfway to the previous). Kind is an Upper cell refused unless one of H A T N O E S. **Check** lists `CheckLayout`'s errors and warnings in a list under the grid; **Preview** opens the statement's preview tab from the unsaved lines ("Layout: not saved (preview)"); **Save** refuses errors and asks on warnings, as today. Changing the statement or closing the page with changes asks, as every page does. | The screen edits a list of rows, which is what the entry grid is for; the fields-and-Apply panel was the dialog's way round having no editable grid. |
| D7 | **No core changes beyond moving code:** the line-number rule and Disposal's entry text move from the dialogs into the core (`StatementLayout.h`, `Assets.h`) with tests; what is read, computed and written doesn't change. | Spec #8 principle 1. |
| D8 | **Every write joins the job gate:** Dispose, the Fixed Assets and Chart of Accounts pages' Delete (already gated), and Statement Layout's Save and Reset stop while a job writes, with the job's name in the information bar. | Spec #9 D8. |
| D9 | **Each old dialog stays as a "(classic)" entry** in the navigation pane (Transaction Inquiry, Disposal, Statement Layout and Delete Accounts (classic)) until this plan's acceptance passes, then goes with Spec #8's removals. Each follows its page's right. | Spec #8 Q4, Spec #9 D9. |

---

## Questions (open)

Each has the answer the plan assumes.

- **Q1 The Activity tab's layout.** The dialog shows "Opening Account Balance" on row 1, two blank rows, the lines from row 4, a blank row, then "Account Balance", each balance in Debit when positive, else in Credit. Keep that, or a plainer grid: an "Opening balance" row, the lines, a "Totals" row (the lines' debits and credits) and a "Balance" row? **Assumed: the plainer grid.** The figures are the same; the Spec #2 sheet's #9 gains a redesign row saying where each is.
- **Q2 Opening a voucher from the Activity tab.** Should Enter on a line open that voucher (the GL Voucher page, read-only for a posted voucher)? **Assumed: no,** not in this plan: the dialog didn't, and it is new behaviour.
- **Q3 Disposal's entry.** Should Dispose also open a new GL voucher with the entry filled in, rather than showing it to type? **Assumed: no:** Spec #4 D7 (the screen posts nothing) stands.
- **Q4 Statement Layout editing.** Edit the lines in the grid (D6), or keep the dialog's fields under a read-only grid with Apply? **Assumed: in the grid.** The Spec #3 sheet's #8 steps change wording ("set Ranges in line 90's row" for "set Ranges, then Apply"), not figures.
- **Q5 Delete Accounts on the right-click menu.** Keep the GLSUB entry (opening the Chart of Accounts page), or remove it? **Assumed: keep it,** opening the page: removing a command is Spec #8's removals' business.

---

## Milestone summary

| # | Milestone | Size | Depends on |
|---|---|---|---|
| 0 | Read; read-only grid tabs; action inputs and computed fields on a tab | M | none |
| 1 | Transaction Inquiry as the account's Activity tab; Delete Accounts opens the page | S | M0 |
| 2 | Disposal as the asset's tab | S | M0 |
| 3 | The Statement Layout page | M | none |
| 4 | Acceptance: the sheet, the figures, sign-off | S | all |

S is less than a day, M is 1–3 days. **Parallel work:** M3 needs only Spec #8's pages and the entry grid, and can run beside M0–M2. M1 and M2 both touch `make_pages.py` and the record page, so they run one after the other.

---

## Milestone 0 — Read-only grids and action inputs on a record page

**Goal:** the two framework pieces M1 and M2 need.

### Tasks

1. **Read** `TransactionInquiry.cpp`, `FixedAssetDisposal.cpp`, `ChartOfAccounts.cpp` (`Mode::Delete`), `Vouchers.h`'s `AccountTransactions`, `Assets.h`'s disposal functions, `pages/RecordPage.h`, `RecordPages.cpp`, `RecordModel.h` and `make_pages.py`. Note every check and message the dialogs make (the M1 and M2 validators and notes).
2. **Read-only grids** (D2): a tab's grid declared read-only in `make_pages.py`; the model fills it in `LoadGrids`, with any number of rows. The page doesn't commit, dirty or save it. `gl_tests`: a read-only grid takes no edit and leaves the page clean; Save of a page with one is unchanged.
3. **Action inputs and computed fields** (D5): a field kind for inputs that aren't part of the record (not read from or written to it, not dirty), and `RecordModel::Computed(values)`, called when an input changes, filling read-only fields. `gl_tests`: an input changed leaves the page clean; computed fields follow it.
4. **The tab check page** (Spec #9 S9-M0) gains a read-only grid tab and an input with a computed field, so `/checkres`, `/checkaccess` and `/stress` cover both before a real page uses them.

### Exit criteria

- The suites pass; `/checkres`, `/checkaccess` and `/stress` exit 0, Debug and Release.
- The existing pages' forms and tests are unchanged.

---

## Milestone 1 — The account's Activity tab; Delete Accounts

**Goal:** 10072 and 1006 open the Chart of Accounts page (D1, D3).

### Tasks

1. **The Activity tab** (D1, Q1): `AccountsModel` loads `AccountTransactions` for the selected account and dept into a read-only grid. A new account shows an empty grid. The tab refreshes when a voucher is posted (`data_group` broadcast, as the Balances tab does).
2. **The commands:** 10072 opens the Chart of Accounts page on Activity (`ShowRecordTab`); 1006 opens it with the focus in the list. "Transaction Inquiry (classic)" and "Delete Accounts (classic)" (new `CM_*_CLASSIC` IDs) run the dialogs (D9), following the page's right.
3. **Tests:** `test_pages.cpp`: the Spec #2 sheet's #9 figures on the model (102101: opening 12,500.00, voucher 100's 250.00 and 104's 300.00, balance 13,050.00; 410001 dept 10: only 101 and 104).
4. **Checks:** `/checkaccess` reaches the Activity tab and names its grid ("Activity grid"); `/screenshots` captures it; `/stress` chooses it.

### Exit criteria

- The Spec #2 sheet's #9 passes on the page, with Q1's layout.
- The suites and the three checks pass, Debug and Release.

---

## Milestone 2 — Disposal as the asset's tab

**Goal:** 6035 and Dispose open the Fixed Assets page's Disposal tab (D4).

### Tasks

1. **The core:** the dialog's entry text (`EntryText`) moves to `Assets.h` (`DisposalEntry(asset, type, salePrice, date)`, the lines as data) with a test, and the dialog uses it.
2. **The tab:** cost, accumulated, book value, Date disposed, Sale price, Gain/loss and the entry (a read-only grid: Dr/Cr, account, what, amount). `Computed` refreshes the gain and the entry as Sale price changes.
3. **Dispose** (the page's button): refused off the tab or on a new or disposed asset; 8107's question; `DisposeAsset` through the job gate (D8); the information bar's note; the page reloads the asset (date sold shown) and broadcasts `kAssets`.
4. **The commands:** 6035 opens the Fixed Assets page on Disposal; "Disposal (classic)" runs the dialog.
5. **Tests:** the Spec #4 sheet's #6 on the model (E002: cost 5,000.00, accumulated 200.00, book value 4,800.00; sale 4,000.00 gives −800.00 and the four entry lines; disposed twice is refused; E001's Delete refused, E006's done).
6. **Checks:** as M1's task 4.

### Exit criteria

- The Spec #4 sheet's #6 passes on the page.
- The suites and the three checks pass, Debug and Release.

---

## Milestone 3 — The Statement Layout page

**Goal:** `CM_STATEMENT_LAYOUT` opens a page (D6).

### Tasks

1. **Read** `StatementLayoutDialog.cpp` and `StatementLayout.h`; note each message and question.
2. **The core:** the blank-line-number rule moves to `StatementLayout.h` (`ChooseLineNo(lines, index)`) with a test.
3. **The model** (`pages/LayoutModel.{h,cpp}`, portable): a statement's lines as an `EntryGridModel` (Kind validated, Ranges through `ParseRanges`, Formula through `ParseFormula`, Indent 0–3, Sign ±1); dirty when they differ from the saved lines; `Check`, `Save` (refuses errors, returns the warnings to ask about), `Reset`.
4. **The page:** the picker, the grid, the problems list, the four commands; Preview opens the statement's preview tab from the model's lines; the caption reads "Statement Layout (not saved)" while dirty, as today; the page's close and the picker ask about changes.
5. **`/checkres`, `/checkaccess`, `/stress`, `/screenshots`** cover the page; "Statement Layout (classic)" runs the dialog.
6. **Tests:** the Spec #3 sheet's #8 on the model (23 IS lines, 26 BS lines; line 90's ranges `170000-199999; 999000` saved and read back; Check's "No problems found."; Reset).

### Exit criteria

- The Spec #3 sheet's #8 passes on the page.
- The suites and the four checks pass, Debug and Release.

---

## Milestone 4 — Acceptance

1. **The sheets:** `gl/docs/superpowers/specs/<date>-spec10-acceptance.md` in the Spec #9 sheet's shape: a criterion per screen, writes during a job (D8), the keyboard, accessibility and 150 %, the earlier sheets. The Spec #2 (#9), #3 (#8) and #4 (#6) sheets' redesign tables gain each moved step; the Spec #9 sheet's K4 becomes "Fixed (Spec #10)".
2. **The figures:** `gl_tests/test_acceptance10.cpp` runs those steps through the page models in the sheets' order and checks every figure they quote.
3. Release build check, the walkthrough with a witness, triage, sign-off. Then the classic entries go with Spec #8's and #9's removals (D9).

---

## Cross-cutting

- **Per-milestone gates,** as Spec #9:
  - `/W4 /WX` clean on Debug and Release x64;
  - the milestone's tests pass, and every earlier suite;
  - `/checkres`, `/checkaccess` and `/stress` exit 0;
  - screenshots of the new pages and tabs at 100 % and 150 %;
  - an entry in `gl/docs/superpowers/plans/2026-05-10-progress.md`;
  - the milestone's "As built" note here.
- **What not to do:**
  - Don't change what a core computes or writes, or a table's layout.
  - Don't change a command's ID.
  - Don't remove a classic dialog before this plan's acceptance has passed.
  - Don't edit `gl/res/gl.rc` by hand.

## Not in this plan

- **The login, About and Reindex Databases** stay as they are.
- **Opening a voucher from the Activity tab** (Q2) and **posting a disposal** (Q3), unless the answers change.
- **Spec #8's and #9's removals** wait for their sign-offs.
- **What earlier specs left out on purpose** (the spreadsheet, month close, Maintain External Vouchers 1051, the reports with no data, the "zap" tool) stays out.

## Risks specific to the plan

| # | Severity | Risk | Mitigation |
|---|---|---|---|
| P1 | Medium | Inputs that aren't saved (D5) could be mistaken for the record's fields, so a typed sale price looks saved. | They sit on the Disposal tab only, under their own group ("Disposal, not saved"), and the page never marks them dirty. |
| P2 | Medium | Editing the layout in the grid (D6) changes how a line number is chosen, which totals' formulas refer to. | `ChooseLineNo` is the dialog's rule moved with a test; nothing renumbers existing lines. |
| P3 | Low | A long account history makes the Activity tab slow to fill on every selection. | M1 measures it on the deployed company's busiest account; if it's slow, the tab loads when it is chosen rather than on every Select. |
| P4 | Low | The read-only grid mode changes the record page's grid handling, which Budget and the Company Profile tabs use. | M0's tests run those pages' saves unchanged. |

## How to use this plan

M0 → M1 → M2, with M3 beside them → M4. If the plan and the Spec #8 design conflict, stop, edit the design (or this plan's decisions), recheck the plan, then continue.
