//
// DateField.h - a date picker as a yyyymmdd field (spec #8 6.1; plan S8-M1
// task 4).
//
// The Windows date picker (DATETIMEPICK_CLASS, "SysDateTimePick32") with
// DTS_SHORTDATECENTURYFORMAT, and DTS_SHOWNONE where a date may be blank.
// The page reads and writes it as the cores keep dates: "yyyymmdd", "" for
// blank, as MainDialog::GetDate does for a tbDate.
//
#pragma once

#include <Windows.h>

#include <string>

namespace win32fx {

// "20261031"; "" when the picker shows none (DTS_SHOWNONE unticked).
std::string GetDateField(HWND picker);

// Sets the date; "" (or a date that isn't one) unticks a DTS_SHOWNONE
// picker, and is refused by one without. False if not set.
bool SetDateField(HWND picker, const std::string& yyyymmdd);

} // namespace win32fx
