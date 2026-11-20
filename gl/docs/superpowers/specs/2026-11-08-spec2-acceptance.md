# Spec #2 acceptance sign-off

The record of plan Milestone 7 (`gl/docs/superpowers/plans/2026-11-08-owl-to-win32-sdk-port-spec2-plan.md`, "Milestone 7"). It covers the 14 manual checks in spec §8 (`2026-11-08-owl-to-win32-sdk-port-spec2-design.md`).

Fill in every blank (`____`) and set each **Result** to `PASS`, `FAIL` or `PASS with known gap`. A `FAIL` needs a triage line in [Gaps and triage](#gaps-and-triage): either fixed in Spec #2, or filed against a later spec.

The criteria build on each other: run them in order on one copy of `dev.db`, and keep an untouched copy so the walkthrough can be repeated. Each check says what state it expects.

## Run details

| Item | Value |
|---|---|
| Commit tested (`git rev-parse --short HEAD`) | ____ |
| Build machine | ____ |
| Test machine (fresh install, not the dev box) | ____ |
| Windows edition and build (`winver`) | ____ (target: Windows 11 23H2 x64; Windows 10 22H2 x64 if 11 is unavailable) |
| Display scaling | ____ (target: 100% / 96 DPI) |
| Tester | ____ |
| Witness | ____ |
| Date | ____ |

## Before the walkthrough

### P1. Release build and hardening (plan M7 task 1)

1. Build `gl.sln` `Release|x64` with `/W4 /WX`. It should build with no warnings.
2. Confirm the hardening options from `common.props`, as in the Spec #1 sheet (P1): compiler `/sdl`, `/guard:cf`; linker `/DYNAMICBASE`, `/NXCOMPAT`, `/GUARD:CF`. Or run `dumpbin /headers x64\Release\gl.exe`: "DLL characteristics" lists "Dynamic base", "NX compatible" and "Control Flow Guard".

| Check | Result | Notes |
|---|---|---|
| Release build, no warnings | ____ | |
| `/sdl` and `/guard:cf` (compiler) | ____ | |
| `/DYNAMICBASE`, `/NXCOMPAT`, `/GUARD:CF` (linker) | ____ | |

### P2. Automated checks (dev machine)

Run `gl\test\dev-fixture\setup.cmd` for each configuration first (`Debug`, then `Release`). From `cmd`, run each check with `start /wait ... /quiet` where it takes `/quiet`, then `echo %errorlevel%`. Each one should print 0.

| Check | Debug | Release | Notes |
|---|---|---|---|
| `x64\<Config>\cb4sqlite_tests.exe` (69 test cases pass) | ____ | ____ | |
| `x64\<Config>\gl_tests.exe` (41 test cases pass) | ____ | ____ | |
| `gl.exe /checkres /quiet` (resource failures; now includes 1006, 1007, 1013, 1015, 1017, 1023, 1029, 1040, 1043, 1061) | ____ | ____ | |
| `gl.exe /stress /quiet` (GDI + USER growth; now also the master data, voucher, posting and inquiry screens) | ____ | ____ | |
| `trivial_dialog.exe /selftest /quiet` (failures; covers `fx::Grid` editing) | ____ | ____ | |
| Debug `gl.exe` under the debugger: open and close Voucher Entry and Post, then exit. The Output window shows no "Detected memory leaks!" | ____ | n/a | |

### P3. Fresh-machine install

As in the Spec #1 sheet (P3): a folder with `gl.exe` and `dbf2sqlite.exe` from `x64\Release\`, `GL.INI` from `gl\test\dev-fixture\`, a `GLDR.INI` with `[GL_DIR]` `DIR=dev.db`, and `dev.db` migrated with `dbf2sqlite.exe` or copied from the dev machine. Use a `sqlite3.exe` (the command-line shell from sqlite.org) for the external checks.

The database's columns are named as in the `.dbf` files (`ACCOUNT`, `CURR_BAL`, ...; SQLite ignores their case). Character values are padded with spaces, so the queries use `trim()` or `like`.

| Check | Result | Notes |
|---|---|---|
| `gl.exe` starts with no Visual C++ runtime installed (static CRT) | ____ | |
| `dbf2sqlite.exe` migration exits 0 (if used) | ____ | |
| Login as `ADMIN` / `ADMIN` works | ____ | |
| `sqlite3 dev.db "select sum(curr_bal) from glaccts"` returns 0 (the fixture's books balance) | ____ | |

## The fixture

- **Accounts:** 11, as in the Spec #1 sheet. 340000 is the only type-R account. 410001 has depts 10 and 20, both income (type I). *(Since Spec #3 S3-M2: 25 accounts, and 12 more posted journals with `gl_dist` lines on the new ones (voucher numbers 1–12, no `gl_btch` header). The checks below name only the original accounts and vouchers, and still hold; 101000's and 102101's `curr_bal` now include voucher 100: 250.00 and 12,750.00.)*
- **Periods:** 1–6 (Apr 26 – Sep 26) are closed and 7–12 (Oct 26 – Mar 27) are open; the current period is "Oct 26".
- **Vouchers:**
  - 100 is posted: 102101 Dr 250.00 / 101000 Cr 250.00 in period 7, with its two `gl_dist` rows;
  - 101 is open and balanced: 111101 Dr 1,150.00 / 410001-10 Cr 600.00 / 410001-20 Cr 550.00;
  - 102 is open and out of balance: 148000 Dr 100.00 / 260007 Cr 90.00;
  - 103 is an open adjustment (mode 3): 144910 Dr 75.00 / 111101 Cr 75.00.
  - The next new voucher is 104.
- **Master data:** departments 10 and 20; medical reps MR001–MR003; doctors DR001–DR003; products P100, P200, P300.

## The 14 criteria (spec §8)

Run these on the test machine, logged in as `ADMIN`.

### 1. Master data

- [ ] **Accounts > Department Codes** (1023): add dept `30` `TEST DEPT`, change its description to `TEST DEPT EDITED`, then delete it. After each step, `sqlite3 dev.db "select * from gldept where trim(depart) = '30'"` shows the change (no row after the delete).
- [ ] **Medical Reps** (1006): add `MR009` with a name and territory, change its name, then delete it. Check each step in `gl_medrp` (`trim(repcode) = 'MR009'`). The territory is saved (D10).
- [ ] **Doctor Codes** (1040): the same with `DR009`, in `gl_drcod` (`trim(drcode) = 'DR009'`).
- [ ] **Product Codes** (1007): the same with `P900`, in `gl_itemm` (`trim(fpcode) = 'P900'`). Add/Mod with a blank code is refused with 4072. Modifying P100 leaves its `flotsize` (10) unchanged (D10).
- [ ] Each screen's list (Search, or its Med Reps / Doctor / Products button) shows the records, and picking a row shows that record.

**Result:** ____ **Notes:** ____

### 2. Voucher entry

- [ ] **Transactions > General Ledger Voucher** (1013) opens with voucher number 104, the Distribution List below it, and the period combo listing only Oct 26 to Mar 27, with Oct 26 selected.
- [ ] Enter a description `ACCEPTANCE 104`, a date, and three lines (Reference and Description are required on each):
  - 102101, Debit 300.00;
  - 410001 dept 10, Credit 200.00, medical rep MR001, product P100, doctor DR001;
  - 410001 dept 20, Credit 100.00, medical rep MR002, product P200, doctor DR002.

  After each Add/Mod the Distribution List shows the line, then "Total" with 300.00 / 300.00 once balanced.
- [ ] `sqlite3 dev.db "select batchno, mode, reference, debit, credit, entryno from gl_btch where batchno = 104"` returns `104|1|ACCEPTANCE 104|300.0|300.0|3`.
- [ ] `sqlite3 dev.db "select journal, trim(account), trim(dept), trim(medrep), trim(prod), trim(drcode), debit, credit from gl_btchd where batchno = 104 order by journal"` returns the three lines with their codes.
- [ ] An unknown account (`999999`), or a known account with an unknown dept (`410001` dept `99`), is refused: a beep, the account list opens at the nearest account, and nothing is saved.

**Result:** ____ **Notes:** ____

### 3. Voucher edit

- [ ] Close Voucher Entry and open it again. Click **Voucher** to show the voucher list, and pick 104.
- [ ] In the Distribution List, double-click the 410001 dept 10 line: its account, amounts, medical rep, product and doctor fill the fields. Change the credit to 250.00 and click **Add/Mod**. The total row shows "Voucher Out Of Balance" in white on red.
- [ ] Pick the 410001 dept 20 line and click **Delete**, answering **Yes**. Then add a line 410001 dept 20, Credit 50.00. The totals balance again at 300.00.
- [ ] `gl_btchd` for 104 has three lines: 300.00 Dr, 250.00 Cr (MR001 / P100 / DR001) and 50.00 Cr. `gl_btch` shows 300.0 / 300.0.
- [ ] Start a new voucher (**New**), add one line, then click **Close** and answer **No** to "Do you want to save the current transaction?" (1056): voucher 105 is not in `gl_btch`.

**Result:** ____ **Notes:** ____

### 4. Prior Period Entry

- [ ] **Transactions > Prior Period Entry** opens the same dialog, captioned "Prior Period Adjustment Entry".
- [ ] Choose voucher 101 (a normal voucher) from the voucher list or by navigating: "Only prior period adjustment entries may be modified in this program." It isn't shown, and `gl_btchd` for 101 is unchanged.
- [ ] Voucher 103 (an adjustment) opens normally.

**Result:** ____ **Notes:** ____

### 5. Post all

Record first: `sqlite3 dev.db "select trim(account), trim(dept), period7, curr_bal, openbal from glaccts"` and `select count(*) from gl_dist`.

- [ ] **Transactions > Post Voucher > General Ledger Voucher** (1061), captioned "Post General Ledger Voucher", lists 101, 102, 103 and 104.
- [ ] Click **Post**. You get "Voucher number 102 is out of balance and can not be posted." (8116), and 102 stays listed. The others leave the list.
- [ ] `gl_btch`: 101 and 104 have mode `0`, 103 has mode `2`, 102 still `1`. `gl_btchd` lines of 101 and 104 have mode `0`.
- [ ] `gl_dist` gained 6 rows: three for 101 and three for 104, with their medrep / prod / drcode. None for 103.
- [ ] `glaccts`, compared with the record:
  - 410001-10: `period7` and `curr_bal` down 850.00 (600 from 101, 250 from 104);
  - 102101: up 300.00;
  - 144910 (adjustment 103): `openbal` up 75.00, `curr_bal` 75.00, `period7` unchanged;
  - 111101: `openbal` down 75.00.
- [ ] `select sum(curr_bal) from glaccts` is still 0.

**Result:** ____ **Notes:** ____

### 6. Atomic posting

- [ ] Copy `dev.db` aside now (after #5), then in Voucher Entry add a voucher 105 with two balanced lines (for example 148000 Dr 10.00 / 260007 Cr 10.00), and close it, keeping it.
- [ ] Record `select * from glaccts`, `select * from gl_dist`, `select * from gl_btch`, `select * from gl_btchd` into files.
- [ ] Exit, and start `gl.exe /failpost 1`. Post: you get "Posting failed, and nothing was changed." naming the `/failpost` fault.
- [ ] The four tables match the recorded files exactly.
- [ ] Exit, start `gl.exe` normally and Post again: 105 posts (102 is refused again).

**Result:** ____ **Notes:** ____

### 7. Recover

Record `glaccts` and `gl_dist` for 101's accounts (111101, 410001-10, 410001-20) first.

- [ ] **Transactions > Recover General Ledger Voucher** (1015) lists the posted vouchers 100, 101, 104 and 105 (not the posted adjustment 103).
- [ ] Double-click 101 and answer **Yes**. It leaves the list.
- [ ] `glaccts`: 111101's `period7` and `curr_bal` are 1,150.00 lower, and 410001-10 and -20 are 600.00 and 550.00 higher: their values before 101 was posted.
- [ ] `gl_dist` has no rows for 101. `gl_btch` 101 has mode `1`, and its lines have a blank mode.
- [ ] Voucher 101 opens in Voucher Entry again, and **Post** posts it again.

**Result:** ____ **Notes:** ____

### 8. Cancel

- [ ] **Transactions > Cancel Voucher > General Ledger Voucher** (1043), captioned "Cancel General Ledger Voucher", lists the unposted vouchers (102).
- [ ] Double-click 102 and answer **Yes** to "Are you sure you want to delete this Voucher?" (8099).
- [ ] `select count(*) from gl_btch where batchno = 102` and the same on `gl_btchd` both return 0.

**Result:** ____ **Notes:** ____

### 9. Transaction Inquiry

- [ ] **Transactions > Voucher Reports > General Ledger Voucher Inquiry** (1029). Type `102101`, blank dept, click **Set Data**.
- [ ] Row 1 shows "Opening Account Balance" 12,500.00 in Debit. From row 4: voucher 100's 250.00 and voucher 104's 300.00 debits, period 7. Then "Account Balance" 13,050.00.
- [ ] `410001` dept `10`: only that dept's posted lines (101 and 104). `410001` with a blank dept: "Unable to find GL Account" (3081) (D13).
- [ ] **Search** shows and hides the account list; picking an account there lists its lines.

**Result:** ____ **Notes:** ____

### 10. Budget Data

- [ ] **Accounts > Budget Data** (1017), captioned "General Ledger - Budget Update". Pick 410001 dept 10 in the account list: the grid shows periods 1–12, with Current Year period 7 at −850.00.
- [ ] Edit Budget for periods 1, 6 and 12 (Enter, F2 or a double-click; Enter commits). Click **Add**, answer **Yes** to "Update the current budget information?" (8098).
- [ ] Close, reopen, pick 410001 dept 10: the three amounts are there. `select budget1, budget6, budget12 from glaccts where trim(account) = '410001' and trim(dept) = '10'` matches.
- [ ] No other account's budget changed.

**Result:** ____ **Notes:** ____

### 11. Year end

- [ ] Copy `dev.db` aside (year end can't be undone, except by Restore). Record `select sum(curr_bal) from glaccts` (0).
- [ ] In the Chart of Accounts set 101000's type to **Retained earnings**. (The Chart refuses a second R account on Add, but Modify allows it, as in the real data.) **Maintenance > Year End Consolidation**, **Yes** to 8111: it refuses and names 101000 and 340000. Nothing changed.
- [ ] Set 101000 back to **Balance Sheet** and run it again: it reports the year closed and 340000's new balance.
- [ ] `glaccts`: 410001-10 and -20 have `curr_bal` 0; 340000's `curr_bal` is its old balance plus theirs; every account's `period1`–`period12` are 0 and `previous1`–`previous12` hold the old periods; balance-sheet accounts keep their `curr_bal`. *(Since Spec #3 S3-M2, D16: every income account has `curr_bal` 0 and 340000 gets all of them; every account's `openbal` equals its new `curr_bal`; `gl_dist` is empty and `gl_distb` holds its lines; voucher 100's `mode` is 9.)*
- [ ] `select sum(curr_bal) from glaccts` is still 0.

**Result:** ____ **Notes:** ____

### 12. Backup and Restore

- [ ] **Maintenance > Backup Databases**: the Save As dialog suggests `dev-<today>.db` beside `dev.db`. Save it. A message names the file.
- [ ] Add account `150002` `AFTER BACKUP` in the Chart of Accounts.
- [ ] **Maintenance > Restore Databases**, choose the backup, answer **Yes**. A message says it was restored.
- [ ] 150002 is gone from the Chart of Accounts; `dev.db.bak` exists and has it (`sqlite3 dev.db.bak "select count(*) from glaccts where trim(account) = '150002'"` returns 1); `gl.exe` keeps running, and the module bar shows the company and period.
- [ ] Restore a text file renamed to `.db`, and a copy of `GL.INI`: each is refused with a reason, and the company stays open.
- [ ] **Maintenance > Reindex Databases** shows the "doesn't need reindexing" message (D6).

**Result:** ____ **Notes:** ____

### 13. Grid

- [ ] The account list (Chart of Accounts) behaves as Spec #1 §6.7 #7 describes (the Spec #1 sheet's #7), now as a grid.
- [ ] **Maintenance > User Access**, then the Menu button (1160): the menus show with PLUS / MINUS pictures; toggling one and clicking OK saves it, Close doesn't (D9).
- [ ] **File > Company Profile**, **Accounts** (1033), then **Cash** (1034): the Cash Accounts list (1011) beside it lists the cash accounts (102101 and 101000), and picking one fills the dialog.
- [ ] Right-click the Chart of Accounts' account list, set the query to `ACCOUNT = '4'`, OK: the list shows only the two 410001 rows. Set it back to `.NOT. DELETED()`.

**Result:** ____ **Notes:** ____

### 14. Regression and resources

- [ ] The Spec #1 sheet's criteria #1–#9 still pass (on a fresh copy of `dev.db`).
- [ ] `/checkres /quiet` and `/stress /quiet` exit 0 (P2).
- [ ] No leaks in Debug (P2).

**Result:** ____ **Notes:** ____

## Known gaps accepted before the walkthrough

| # | Gap | Why accepted | Follow-up |
|---|---|---|---|
| G1 | High DPI is not tested. The walkthrough runs at 96 DPI. | As Spec #1 G1. | The UI refresh (U4) made the ported screens DPI-aware; its acceptance sheet (`2026-11-13-ui-refresh-acceptance.md`, criteria 6 and 7) tests them. Closed for the ported screens when those pass: ____ (date). |
| G2 | Voucher Entry's Print, and every report, show the "not yet ported" message. | Reports are Spec #3. | Spec #3. |
| G3 | Month close (1069) is not ported. Periods are closed in Company Profile > Periods. | D4: the original had retired it. | None. |
| G4 | Maintain External Vouchers stays a stub. | D7. | When its purpose is known. |
| G5 | After a year end, Transaction Inquiry's balance and adjustment posting use `openbal`, which year end leaves alone, as the original did. | Spec §12 Q6; the real data's year end has never run. | Settle before the first real year end. |
| G6 | The `querySet` comparison rules follow `dbf2sqlite`'s translation, not CodeBase 4's documented behaviour. | Spec §12 Q5; no CodeBase source in the tree. | Confirm if the original build or its docs turn up. |

## Redesign (Spec #8)

The figures and checks above stay as they are. Spec #8 (`2026-11-19-owl-to-win32-sdk-port-spec8-design.md`, §15 criterion 2) moved many screens into pages in the main window; this table says where each step whose screen moved is done in the redesigned program. Until Spec #8's acceptance the old dialog of each moved screen is still reachable as a "(classic)" entry in the navigation pane, so each step can be done either way. Transaction Inquiry (#9) is still a dialog. Since Spec #9 (`plans/2026-11-20-owl-to-win32-sdk-port-spec9-plan.md`), Budget Data (#10) is the Chart of Accounts page's Budget tab (S9-M1), Company Profile is a page with Company, Periods, Accounts and Banks tabs (S9-M2), and Restore Databases is a task page (S9-M4). Their dialogs are "Budget Data (classic)", "Company Profile (classic)" and "Restore Databases (classic)" until Spec #9's acceptance.

| Criterion and step | Where it's done now | What differs |
|---|---|---|
| 1. Department Codes: add, change and delete dept `30` | Department Codes page | Save, not Add/Mod. The code can't be changed once saved. The delete asks in a task dialog with named buttons (for example "Delete department" / "Keep it"). |
| 1. Medical Reps: add, change and delete `MR009` | Medical Reps page | As for Department Codes. |
| 1. Doctor Codes: the same with `DR009` | Doctor Codes page | As for Department Codes. |
| 1. Product Codes: the same with `P900`; a blank code; modifying P100 | Product Codes page | A blank code is marked as required and the information bar says why, instead of 4072. |
| 1. Each screen's list shows the records, and picking a row shows it | The list on the left of each page | The list is always shown, filtered as you type. There is no Search or Med Reps / Doctor / Products button. |
| 2. General Ledger Voucher opens with number 104, the list and the period combo | GL voucher page | A new voucher has no number until it's first saved; it gets 104 then. The lines are a grid on the page, not a separate Distribution List. |
| 2. Enter the description, date and three lines | GL voucher page: the header, then the lines in the grid, then Save | The whole voucher is saved at once with Save; no Add/Mod per line and no line number field. The line's reference goes in the Document column, and the account name fills in. Totals and "Balanced." show live. |
| 2. An unknown account, or a known account with dept `99` | GL voucher page, the grid | The cell is marked and the information bar says why; Save is refused, so nothing is saved. No beep and no account list. |
| 3. Close and reopen; click Voucher and pick 104 | Transactions > Voucher List page: select 104, Open | Open shows voucher 104 on the GL voucher page. |
| 3. Change the 410001 dept 10 line's credit to 250.00 | GL voucher page, the grid | Edit the cell, then Save. The totals show out of balance live, not as a red total row. |
| 3. Delete the 410001 dept 20 line, add a 50.00 line | GL voucher page, the grid | Delete the row in the grid (no Yes / No question), add the new row, then Save. |
| 3. New, one line, Close, answer No to 1056 | GL voucher page: New, one line, close the page | The unsaved-changes question is a task dialog with named buttons; choose the one that doesn't save. No number is assigned, so 105 is not used. |
| 4. Prior Period Entry opens, captioned "Prior Period Adjustment Entry" | GL voucher page in adjustment mode (tab "Adjustment") | Same page as the GL voucher, in adjustment mode. |
| 4. Choose voucher 101 (a normal voucher) | Prior Period Entry (classic) | The adjustment page moves only between adjustment vouchers, and the Voucher List opens 101 on the normal GL voucher page, so the page has no path to the refusal. |
| 4. Voucher 103 opens normally | Voucher List page: Open 103, or the adjustment page's navigation | Open takes an adjustment to the GL voucher page in adjustment mode. |
| 5. Post Voucher > General Ledger lists 101–104 | Post General Ledger task page | A list with check boxes, a summary and a Post button. 102 shows "Out of balance by 10.00." and has no check box. |
| 5. Click Post; 102 is refused with 8116 | Post General Ledger task page: Post, then confirm in the task dialog | No 8116 message: 102 can't be checked and stays listed with its reason. The posting runs in the background and a result line says what was posted. |
| 6. Add voucher 105 and keep it | GL voucher page: the lines, then Save | As in #2. |
| 6. Under `/failpost 1`, Post | Post General Ledger task page | The failure shows in the information bar ("It failed: ...") naming the `/failpost` fault, not in a message box. |
| 6. Start normally and Post again | Post General Ledger task page | 105 posts; 102 again has no check box. |
| 7. Recover General Ledger Voucher lists 100, 101, 104 and 105 | Recover General Ledger task page | Nothing is checked to start. |
| 7. Double-click 101 and answer Yes | Recover General Ledger task page: check 101, click Recover, confirm | The confirmation is a task dialog; a result line names what was recovered. |
| 7. 101 opens in Voucher Entry again, and Post posts it | Voucher List page, Open; then the Post General Ledger task page | As in #3 and #5. |
| 8. Cancel Voucher > General Ledger lists 102 | Cancel General Ledger task page | Nothing is checked to start. |
| 8. Double-click 102 and answer Yes to 8099 | Cancel General Ledger task page: check 102, click Cancel vouchers, confirm | The confirmation is a task dialog with named buttons instead of 8099. |
| 10. Accounts > Budget Data; pick 410001 dept 10 | Chart of Accounts page, Budget tab (Budget Data opens the page on it); select 410001 dept 10 in the list | The grid is on the account's page, not a separate screen. Period 7's Current Year shows the same figure. |
| 10. Edit periods 1, 6 and 12; Add, Yes to 8098 | Chart of Accounts page, Budget tab: edit the three cells, then Save | Save, not Add; no 8098 question. Enter moves down the periods. A budget that didn't change isn't written. |
| 10. Close, reopen, pick 410001 dept 10: the three amounts are there | Chart of Accounts page, Budget tab | None: the `select` and "no other account's budget changed" checks are the same. |
| 11. Set 101000's type to Retained earnings, then back to Balance Sheet | Chart of Accounts page: select 101000, change the type, Save | Save, not Add/Mod. |
| 11. Year End Consolidation, Yes to 8111; refused, then run again | Year end task page: Close the year, then confirm | The confirmation is a task dialog instead of 8111. The list shows both type-R accounts with a note; the refusal says the chart needs exactly one retained earnings account and how many it has, without naming them. The result line reports the amount to retained earnings, not 340000's new balance. |
| 12. Backup Databases suggests `dev-<today>.db`; save it | Backup task page: Back up, then confirm | No Save As dialog: the page shows the copy's name, `dev-<today>.db` beside `dev.db`, and the result line names the file. |
| 12. Add account `150002` | Chart of Accounts page | Save, not Add/Mod. |
| 12. Restore Databases, choose the backup, Yes | Restore task page: select `dev-<today>.db` in the list (the backups beside `dev.db`, newest first), **Restore**, then **Restore backup** | No file dialog: the list is the backups beside the company file, and **Browse...** adds one from elsewhere. Any other page closes first; one with unsaved changes asks. The result is on the information bar: "The company database was restored. The previous one is dev.db.bak." |
| 12. 150002 is gone after the restore | Chart of Accounts page, the list | The page was closed by the restore: open it again. |
| 12. Restore a text file renamed to `.db`, and a copy of `GL.INI` | Restore task page: **Browse...** to each (the list itself shows only `.db` and `.bak` files beside `dev.db`, so a text file named `.db` there is listed too) | They are listed with no restore: the note says "Can't be restored: ..." with the reason, and **Restore** on one says so. |
| 13. The account list behaves as the Spec #1 sheet's #7 | Chart of Accounts page | The list is a filtered list, not a grid. See the Spec #1 sheet's redesign table for #7: the query, saved position and close/Search steps are done in Chart of Accounts (classic). |
| 13. User Access, the Menu button: toggle a menu, OK saves, Close doesn't | User Access page | The selected user's level's commands are check boxes grouped by menu (checked = allowed), not PLUS / MINUS pictures. Save saves; leaving the user or closing the page with changes asks about them. |
| 13. Right-click the account list and set the query `ACCOUNT = '4'` | Chart of Accounts (classic) | The page's list has no query. |
| 13. File > Company Profile, Accounts (1033), then Cash (1034): the list (1011) shows 102101 and 101000 | Company Profile page: the Accounts tab, then the Banks tab | One page with tabs, not three dialogs. The Banks tab is a grid of the banks (101000 Petty Cash, 102101 Checking Account, in account order); a row is edited in place, added in the blank row and removed with Ctrl+Delete, and Save writes them. There is no "Already added, modify?" (6022) or delete question (6024). |
| 14. The Spec #1 sheet's criteria still pass | As in the Spec #1 sheet's redesign table | None. |

## Gaps and triage

Add one row for each `FAIL` or unexpected behaviour found during the walkthrough.

| # | Criterion | What happened | Decision (fix in Spec #2 / file for Spec #N) | Fixed in commit / issue |
|---|---|---|---|---|
| | | | | |

## Sign-off

| Criterion | Result |
|---|---|
| 1. Master data | ____ |
| 2. Voucher entry | ____ |
| 3. Voucher edit | ____ |
| 4. Prior Period Entry | ____ |
| 5. Post all | ____ |
| 6. Atomic posting | ____ |
| 7. Recover | ____ |
| 8. Cancel | ____ |
| 9. Transaction Inquiry | ____ |
| 10. Budget Data | ____ |
| 11. Year end | ____ |
| 12. Backup and Restore | ____ |
| 13. Grid | ____ |
| 14. Regression and resources | ____ |

All 14 criteria pass, or pass with the known gaps recorded above:

- Tester: ____ Date: ____
- Witness: ____ Date: ____
