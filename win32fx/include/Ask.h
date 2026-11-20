//
// Ask.h - a decision in a task dialog (spec #8 7.2; plan S8-M1 task 4).
//
// A short main instruction ("Delete customer C004?"), what it means ("Only
// the customer is deleted. It can't be undone."), and buttons named for what
// they do ("Delete customer", "Keep it"), the safe one the default. Esc and
// the close box give `cancelId` (give the safe button's ID). Returns the
// chosen button's ID.
//
#pragma once

#include <Windows.h>

#include <string>
#include <vector>

namespace win32fx {

struct AskButton {
    int id;
    std::wstring text;
};

enum class AskIcon { None, Information, Warning, Error, Question };

int Ask(HWND owner, const std::wstring& title, const std::wstring& instruction, const std::wstring& content,
        const std::vector<AskButton>& buttons, int defaultId, int cancelId, AskIcon icon = AskIcon::Warning);

} // namespace win32fx
