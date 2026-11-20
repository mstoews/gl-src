//
// TaskPages.h - the batch screens as task pages (spec #8 5.3; plan S8-M4
// task 6), each run as a background job (fx::Job).
//
// A task page lists what it will act on, with check boxes; sums it up; and
// has one named action, with progress and Stop. The work runs on a worker
// with its own connection (WAL); the list's rows get the refusals and the
// result names what was made. When it ends, NotifyDataChanged tells the open
// pages (DataChanged.h).
//
//   command                                   task
//   Post Voucher > General Ledger (10121)     Post GL vouchers
//   ... > Purchase / Cash / Sales (1048, 1088, 4049)   Post AP and AR vouchers
//   Cancel Voucher > ... (3054, 3064, 1085, 4053)       Cancel
//   Recover GL / Purchase / Sales (10122, CM_AP_RECOVER, CM_AR_RECOVER)
//   Update AR and AP (1028), Create Voucher [depreciation] (6003),
//   Year End (1068), Purge Paid Payables (1066), Purge Received Receivables
//   (4062), Create Cheques (1057), Backup (1072)
//
// Each classic dialog stays as a "(classic)" entry (spec #8 Q4).
//
#pragma once

#include "RecordPage.h"   // ClassicCommand
#include "win32fx.h"

#include <utility>
#include <vector>

bool IsTaskPageCommand(WORD command);
HWND OpenTaskPage(fx::PageHost& host, WORD command);
const std::vector<ClassicCommand>& TaskClassicCommands();

// The commands that write to the books: disabled while a writing job runs.
bool IsWritingCommand(WORD command);

// /checkres
const std::vector<std::pair<int, unsigned>>& TaskPageAnchors();

// /stress: each task page opened and closed, and a dry job (posting no
// vouchers) started and stopped, `times` times. Object growth, or -1.
int StressTaskPages(int times);
