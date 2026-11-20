//
// DateField.cpp - see DateField.h.
//
#include "DateField.h"

#include <CommCtrl.h>

#include <cstdio>
#include <cstdlib>

namespace win32fx {

std::string GetDateField(HWND picker)
{
    SYSTEMTIME st{};
    if (SendMessageW(picker, DTM_GETSYSTEMTIME, 0, reinterpret_cast<LPARAM>(&st)) != GDT_VALID) return {};
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04u%02u%02u", st.wYear, st.wMonth, st.wDay);
    return buf;
}

bool SetDateField(HWND picker, const std::string& yyyymmdd)
{
    if (yyyymmdd.size() != 8) return SendMessageW(picker, DTM_SETSYSTEMTIME, GDT_NONE, 0) != 0;
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(std::atoi(yyyymmdd.substr(0, 4).c_str()));
    st.wMonth = static_cast<WORD>(std::atoi(yyyymmdd.substr(4, 2).c_str()));
    st.wDay = static_cast<WORD>(std::atoi(yyyymmdd.substr(6, 2).c_str()));
    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) return SendMessageW(picker, DTM_SETSYSTEMTIME, GDT_NONE, 0) != 0;   // not a date
    return SendMessageW(picker, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&st)) != 0;
}

} // namespace win32fx
