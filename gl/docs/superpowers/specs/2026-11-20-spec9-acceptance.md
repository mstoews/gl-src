# Spec #9 acceptance sign-off

This sheet records plan Milestone 5 (`gl/docs/superpowers/plans/2026-11-20-owl-to-win32-sdk-port-spec9-plan.md`, "Milestone 5"). Spec #9 is the Spec #8 design (§5.1, §5.3) applied to the six screens Spec #8 left as dialogs, the Spec #8 sheet's gap G3: Budget Data, Company Profile, Print Cheques, Confirm Printed Cheques, Bank Reconciliation and Restore Databases. The plan's decisions are D1 to D9; its questions Q1 to Q4 were answered as assumed (2026-11-20).

Fill in every blank (`____`) and set each **Result** to `PASS`, `FAIL` or `PASS with known gap`. A `FAIL` needs a triage line in [Gaps and triage](#gaps-and-triage).

**The steps are the earlier sheets':** most criteria below are steps of the Spec #2 and Spec #5 sheets, done on the new pages. Each of those sheets' "Redesign (Spec #8)" table says where each step is now done and what differs; their figures and checks don't change.

**The figures** come from `gl_tests`:
- "The Spec #9 acceptance figures" (`[acceptance9]`, `gl_tests/test_acceptance9.cpp`) runs the Spec #2 sheet's #10, #12 and #13 on the pages, in the sheet's order on one fixture copy.
- The section "7 and 8 on the Cheques and Bank Reconciliation pages" of "The Spec #5 acceptance walkthrough's figures" (`gl_tests/test_ap.cpp`) runs the Spec #5 sheet's #7 and #8 on the pages, after that sheet's own steps 1 to 6. The other section runs them through the classic calls, and both lead to the same tables.

**The classic screens:** until this sheet is signed off (D9), each moved screen's dialog is a "(classic)" entry in the navigation pane: Budget Data, Company Profile, Confirm Printed Cheques, Bank Reconciliation and Restore Databases (classic). They follow the page's right.

## Run details

| Item | Value |
|---|---|
| Commit tested (`git rev-parse --short HEAD`) | ____ |
| Build machine | ____ |
| Test machine (fresh install, not the dev box) | ____ |
| Windows edition and build (`winver`) | ____ |
| Display scaling | ____ (100% / 96 DPI; and 150% for #7) |
| Tester | ____ |
| Witness | ____ |
| Date | ____ |

## Before the walkthrough

### P1. Release build

| Check | Result | Notes |
|---|---|---|
| `gl.sln` `Release|x64`, `/W4 /WX`, no warnings | ____ | |

### P2. Automated checks (dev machine)

Run `gl\test\dev-fixture\setup.cmd` for each configuration first. From `cmd`, run each check with `start /wait ... /quiet` where it takes `/quiet`, then `echo %errorlevel%`. Each should print 0. Close any running `gl.exe` first: a second one hands off to it and exits 0.

| Check | Debug | Release | Notes |
|---|---|---|---|
| `x64\<Config>\gl_tests.exe` (239 test cases pass; `[acceptance9]` is this sheet's figures) | ____ | ____ | |
| `x64\<Config>\cb4sqlite_tests.exe` (79 test cases pass) | ____ | ____ | |
| `x64\<Config>\report_tests.exe` (19 test cases pass) | ____ | ____ | |
| `gl.exe /checkres /quiet` (every page form, the tab check page and the Company Profile form included) | ____ | ____ | |
| `gl.exe /checkaccess /quiet` (every page, and every tab of a page with tabs) | ____ | ____ | |
| `gl.exe /stress /quiet` (every record page's tabs; the Cheques, Bank Reconciliation and Restore pages opened). A Debug run takes about 5 minutes. | ____ | ____ | |
| `gl.exe /screenshots shots /quiet`: also `page-1004-tab2` (Budget) and `page-6015-tab2` to `-tab4`; look at them | ____ | ____ | |

## The fixture

The dev fixture (`gl\test\dev-fixture\setup.cmd Release`), as the Spec #2 and #5 sheets describe it. Keep an untouched copy of `dev.db`: #6 replaces it.

## The criteria

### 1. Budget Data (Spec #2 sheet #10; D1, D2)

- [ ] **Accounts > Budget Data** opens the Chart of Accounts page on its **Budget** tab. Pick 410001 dept 10 in the list: 12 periods, with Current Year period 7 at −850.00 (after the Spec #2 sheet's #2 to #5).
- [ ] Edit Budget for periods 1, 6 and 12, then **Save**: no "Update the current budget information?" question. Choose another account and back: the three amounts are there. The Spec #2 sheet's `select` matches; no other account's budget changed.
- [ ] The grid has exactly 12 rows: Insert and Ctrl+Delete do nothing, and Enter on period 12 stays there.
- [ ] **Budget Data (classic)** opens the old dialog 1017.

**Result:** ____ **Notes:** ____

### 2. Company Profile (Spec #2 sheet #13; D3, D4, Q1)

- [ ] **File > Company Profile** opens one page with Company, Periods, Accounts and Banks tabs. The Company tab shows the fixture company, the year's first and last days (04/01/2026, 03/31/2027), the database and W/H tax.
- [ ] Change the phone and **Save**: "Company profile saved." The status bar shows the company and period again.
- [ ] **Periods:** change period 8's description and close it (Y), Save; `sqlite3 dev.db "select trim(pd_desc), status from gl_pds where period = 8"` shows them. A date like 13/45/2026 is refused, nothing written. Dates out of order are saved, as the original allowed (Q1).
- [ ] **Accounts:** an account not in the chart (999999) is refused: "Unable to locate the account number: 999999 (Accounts tab, ...)." Nothing is written.
- [ ] **Banks:** the list shows 101000 Petty Cash and 102101 Checking Account. Add 111101 in the blank row with a name, rename one, remove one with Ctrl+Delete, Save: `gl_setat`'s TYPE = 'CA' rows match. A bank listed twice, or not in the chart, is refused.
- [ ] **Reset depreciation flag...** asks, then says how many fixed assets it reset.
- [ ] With a Chart of Accounts page open in another tab, Save here: that page keeps working (no table closed under it).
- [ ] **Company Profile (classic)** opens the old dialog 1021.

**Result:** ____ **Notes:** ____

### 3. Cheques (Spec #5 sheet #7; D5, Q3)

- [ ] After the Spec #5 sheet's #1 to #6: **Receipts/Payments > Confirm Printed Cheques** opens the **Cheques** page, listing 5002 (2,220.00).
- [ ] **Print cheques** opens the cheques' preview, as Receipts/Payments > Print Cheques still does.
- [ ] Select 5002, New number 5001, **Renumber**: "Cheque number 5001 has already been used." New number 5003, **Renumber**: "Cheque 5002 is now cheque 5003. Print it again."
- [ ] 5003 checked, **Confirm printed**, confirm: its note says "Confirmed printed." and it leaves the list.
- [ ] The home page's "cheques to print" tile opens the Cheques page.
- [ ] **Confirm Printed Cheques (classic)** opens the old dialog.

**Result:** ____ **Notes:** ____

### 4. Bank Reconciliation (Spec #5 sheet #8; D6, Q4)

- [ ] **Receipts/Payments > Bank Reconciliation** lists 5001 and 5003 with no boxes checked. (A cheque made but not confirmed printed is listed with no check box and why.)
- [ ] Date 10/31/2026, check 5003, **Clear**, confirm: "Cleared 10/31/2026."; on Refresh it's gone.
- [ ] Select 5001, **Void**, **Void cheque**: "Cheque 5001 voided: 1 invoice(s) open again; 0 journal row(s) removed, 2 reversed." The Spec #5 sheet's `ap_jsum` query returns its four rows.
- [ ] Changing the Date doesn't clear the check boxes.
- [ ] **Bank Reconciliation (classic)** opens the old dialog.

**Result:** ____ **Notes:** ____

### 5. Writes wait for a running job (D8)

- [ ] Start a long job (Post GL vouchers on many vouchers, or the Spec #8 sheet's #7). Meanwhile: Budget, Company Profile's Save, Renumber, Void and Restore each say which job is writing and do nothing; Confirm Printed Cheques, Bank Reconciliation and Restore Databases are greyed in the menu.

**Result:** ____ **Notes:** ____

### 6. Restore (Spec #2 sheet #12; D7, Q2)

- [ ] **Maintenance > Backup Databases**: back up to `dev-<today>.db`. Add account 150002 on the Chart of Accounts page, and type into a new account's description without saving.
- [ ] **Maintenance > Restore Databases** opens the **Restore** page: the backups beside `dev.db`, newest first, with when each was saved and its size.
- [ ] Select `dev-<today>.db`, **Restore**, **Restore backup**. The unsaved Chart of Accounts page asks about its changes; choose **Don't save**. The page closes, the restore runs: "The company database was restored. The previous one is dev.db.bak."
- [ ] 150002 is gone from the Chart of Accounts (open it again); `dev.db.bak` has it. The status bar shows the company and period, and the menu the user's rights.
- [ ] Choose **Cancel** at the unsaved page's question instead: "Nothing was restored: a page was kept open."
- [ ] **Browse...** to a text file renamed `.db`, and to a copy of `GL.INI`: each is listed with "Can't be restored: ...", and Restore says so. The company stays open.
- [ ] **Restore Databases (classic)** runs the old file dialog.

**Result:** ____ **Notes:** ____

### 7. Keyboard, accessibility and look

- [ ] Every new page and tab without the mouse: Ctrl+Tab between page tabs; Tab reaches the tab strip, and the arrow keys change tab; Tab into each grid.
- [ ] Narrator reads the tabs' names, each grid's name ("Budget grid", "Periods grid", ...), and the information bar's messages.
- [ ] At 150%, the Company Profile tabs, the Cheques and Restore pages lay out without clipped text.

**Result:** ____ **Notes:** ____

### 8. The earlier sheets

- [ ] The Spec #2 sheet's #10, #12 and #13 and the Spec #5 sheet's #7 and #8 pass on the pages, as their redesign tables say.
- [ ] The rest of the Spec #1 to #8 sheets still pass.

**Result:** ____ **Notes:** ____

## Gaps and triage

Known before the walkthrough:

| # | Gap | Triage |
|---|---|---|
| K1 | **Changed from the plan:** the Banks tab is a grid, not a list with its own New, Save and Delete (D3, D4 corrected); Confirm printed uses check boxes, not From and To (D5 corrected); the Restore page shows each file's check in its list rather than in a panel. | As built; the plan's notes say why. |
| K2 | A Debug `/stress` run takes about 5 minutes: the report step's first reading is −1 (a USER object lost to OLE), and `stressStep` runs it again. Not Spec #9's. | Accepted. ____ |
| K3 | One Release `/checkres` run in S9-M1 failed one check that didn't recur in 44 runs, and wasn't caught. | Watch for it in P2. ____ |
| K4 | Transaction Inquiry (the Spec #2 sheet's #9) is still a dialog; it wasn't part of G3. | A later spec, if wanted. ____ |

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

With this and the Spec #8 sheet signed off, the classic entries go with Spec #8's removals (its plan's M6 task 4, gap G4; this plan's D9).
