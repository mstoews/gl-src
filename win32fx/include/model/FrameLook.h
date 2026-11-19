//
// FrameLook.h - the main window's Windows 11 touches, decided without a
// window (spec #8 11; plan S8-M6 task 1). Portable: fx::ApplyFrameLook reads
// the system's settings and applies the result; gl_tests checks the rules.
//
//   Mica backdrop   Windows 11 22H2 (build 22621) and later, unless high
//                   contrast is on or transparency effects are off
//   dark title bar  when apps use the dark theme (and high contrast is off)
//   Windows 10      (Q7) the same program, no backdrop
//
#pragma once

namespace win32fx::model {

struct FrameLook {
    bool mica = false;
    bool darkTitleBar = false;
};

constexpr unsigned long kMicaBuild = 22621;   // Windows 11 22H2

inline FrameLook ChooseFrameLook(unsigned long build, bool highContrast, bool transparency, bool appsUseLightTheme)
{
    FrameLook look;
    look.mica = build >= kMicaBuild && !highContrast && transparency;
    look.darkTitleBar = !highContrast && !appsUseLightTheme;
    return look;
}

} // namespace win32fx::model

namespace fx = win32fx;
