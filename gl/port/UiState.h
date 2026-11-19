//
// UiState.h - what the workspace remembers per user (spec #8 10, D10; plan
// S8-M1 task 5). Portable.
//
// GL_UISTATE, a port table: USERNAME (10), UIKEY (40), UIVALUE (memo). One
// row per user and key. Without logins (GL.INI [ACCESS] PSW=F) the user is
// NOLOGIN, stored blank, as GL_RPTDEF does. It holds the window's placement,
// the navigation pane's visibility, the pinned tabs and the active one, and
// later each list's columns and the favourites; GL.INI's window positions are
// no longer read.
//
#pragma once

#include "cb4sqlite.h"

#include <optional>
#include <string>

// Creates GL_UISTATE when the company lacks it.
void EnsureUiStateTable(cb4sqlite::Code4& code);

// The user's value for `key`, or nothing.
std::optional<std::string> GetUiState(cb4sqlite::Code4& code, const std::string& user, const std::string& key);

// Sets (or, with an empty value, removes) the user's value for `key`.
void SetUiState(cb4sqlite::Code4& code, const std::string& user, const std::string& key, const std::string& value);

// "NOLOGIN" -> "", anything else trimmed: the user as GL_UISTATE keeps it.
std::string UiStateUser(const std::string& szUser);
