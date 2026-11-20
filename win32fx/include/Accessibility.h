//
// Accessibility.h - the Windows 11 frame touches and accessible names
// (spec #8 11, 12; plan S8-M6 tasks 1 and 2).
//
//   ApplyFrameLook        Mica and the title bar's dark mode on a top-level
//                         window, from the system's settings (model::FrameLook);
//                         call again on WM_SETTINGCHANGE / WM_THEMECHANGED
//   AccessibleName        what a screen reader reads for a control (MSAA's
//                         accName, which UI Automation's proxy uses too)
//   SetAccessibleName     an explicit name (Dynamic Annotation, IAccPropServices)
//   NameUnnamedControls   names every focusable control of a form that has no
//                         name: an edit or list after no label takes its cue
//                         banner, its group box's caption or the label of the
//                         field it belongs to ("Sales: dept"); see the .cpp
//   UnnamedControls,      /checkres: the focusable controls with no name; the
//   UnreachableControls,  ones Tab can't reach; and the pairs whose tab order
//   TabOrderProblems      doesn't follow reading order within a group
//
// COM must be initialised on the thread (gl.exe's wWinMain does it).
//
#pragma once

#include <Windows.h>

#include <string>
#include <vector>

namespace win32fx {

void ApplyFrameLook(HWND frame);

std::wstring AccessibleName(HWND control);
bool SetAccessibleName(HWND control, const std::wstring& name);
void NameUnnamedControls(HWND form);
// Removes the names NameUnnamedControls set (before the form goes).
void ClearAccessibleNames(HWND form);

std::vector<HWND> UnnamedControls(HWND form);
std::vector<HWND> UnreachableControls(HWND form);
// Each pair: a control and the one Tab goes to next, out of reading order.
std::vector<std::pair<HWND, HWND>> TabOrderProblems(HWND form);

// The information bar's announcement (spec #8 12): a UI Automation
// notification, read at once by Narrator and others.
void Announce(HWND hwnd, const std::wstring& text);

} // namespace win32fx
