//
// CommandsMenu.h - the Win32 half of the command table (spec #8 8; plan
// S8-M0 task 4): the menu bar built from cmd::MenuRows(), and a comparison
// /checkres uses to prove it is the MANILA menu, item by item.
//
#pragma once

#include <Windows.h>

#include <string>

// The menu bar, from the command table. The caller destroys it.
HMENU BuildCommandMenu();

// True when the two menus have the same items in the same order: text, ID,
// separators, popups (recursively) and grey/check state. Otherwise `why`
// says where they first differ.
bool SameMenus(HMENU a, HMENU b, std::wstring& why);
