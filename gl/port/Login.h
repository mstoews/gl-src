//
// Login.h - port of gl/src/gl_acces.cpp (plan M5 task 4).
//
//   Login               TGLAccess, dialog 1158: user name + password against
//                       access.psw; loads the user's menu-access string.
//   UserAdministration  TGLAdministration, dialog 1159 (Maintenance > User
//                       Access): browse/add/modify/delete access.psw users.
//   (ApplyMenuAccess, which greyed the menu items the access string turned
//   off, became cmd::Rights in spec #8 S8-M0: Commands.h.)
//   LoginRequired       GL.INI [ACCESS] PSW=T (TGlWindow::SetupWindow).
//
// TAccessMenus (dialog 1160, the menu-access editor behind 1159's Menu
// button) lists the accessm menus in an fx::Grid; double-click or Enter
// toggles one. The Menu button then saves the string as the menu code of the
// level shown in 1159 (UserAdministration::Menus).
//
#pragma once

#include <Windows.h>

// True if GL.INI [ACCESS] PSW is "T": the frame then asks for a login at
// startup. Otherwise there is no login and szUser is "NOLOGIN".
bool LoginRequired();

// Runs the login dialog over `frame`. IDOK: szUser, szAccessLevel and szMsg
// are set and bMenuSetup is FALSE. IDCANCEL: the user gave up; the original
// then closed the program (TGLAccess::Cancel sent CM_EXIT), which the
// caller does.
INT_PTR RunLogin(HWND frame);

// Maintenance > User Access.
void RunUserAdministration(HWND frame);

