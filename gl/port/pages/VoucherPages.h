//
// VoucherPages.h - the voucher pages (spec #8 5.2; plan S8-M3 tasks 3-5):
// vouchers typed into an entry grid (fx::EntryGrid), with live totals.
//
//   General Ledger Voucher (10076, 1007)  the GL voucher page
//   Prior Period Entry (8081)             the same page, in adjustment mode
//   Voucher List (CM_VOUCHER_LIST)        a list page; Open opens a voucher's page
//   Purchase Invoice Vouchers (4044)      the AP voucher page
//   Cash Invoice Vouchers (1079)          the same page, for cash vouchers
//   Sales Invoice Vouchers (5045)         the AR invoice page, with its memo
//   Clear Receipts (4054)                 the receipts page
//
// The logic is GlVoucherModel's and SubledgerModels.h's; each classic dialog
// stays as a "(classic)" entry (spec #8 Q4).
//
#pragma once

#include "RecordPage.h"   // ClassicCommand
#include "win32fx.h"

#include <utility>
#include <vector>

// A GL voucher's page: `voucher` 0 for a new one. Brings an open page forward.
HWND OpenGlVoucherPage(fx::PageHost& host, bool adjustment, long voucher = 0);

bool IsVoucherPageCommand(WORD command);
HWND OpenVoucherPage(fx::PageHost& host, WORD command);
const std::vector<ClassicCommand>& VoucherClassicCommands();

// /checkres: each form and its anchors.
struct VoucherPageForm {
    int form;
    const wchar_t* what;
    const std::vector<std::pair<int, unsigned>>* anchors;
};
std::vector<VoucherPageForm> VoucherPageForms();

// /stress: each page opened in a hidden workspace, five lines typed through
// its model, and closed without saving, `times` times. Object growth, or -1.
int StressVoucherPages(int times);
