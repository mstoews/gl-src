//
// CustomersPage.h - Accounts > Sales Customers as a page (spec #8 5.1;
// plan S8-M1 task 6). The first list-and-detail page: the customers in a
// list you filter as you type, the selected one's fields beside it, and its
// open items. The logic is CustomersModel's; this shows it.
//
// The classic dialog (ArCustomers.h, 1052) stays reachable as "Sales
// Customers (classic)" until the redesign's acceptance (spec #8 Q4).
//
#pragma once

#include "win32fx.h"

#include <string>
#include <utility>
#include <vector>

// Opens the Customers page in `host`, or brings the open one forward.
HWND OpenCustomersPage(fx::PageHost& host);

// The Customers page shows customer `number` (command search).
void ShowCustomer(HWND page, const std::string& number);

// The form's anchors, for /checkres (every control says how it moves).
const std::vector<std::pair<int, unsigned>>& CustomersPageAnchors();

// /stress: the page opened in a hidden workspace, filtered, a customer
// selected, and closed, `times` times. GDI + USER object growth, or -1.
int StressCustomersPage(int times);
