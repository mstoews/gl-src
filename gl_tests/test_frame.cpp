//
// gl_tests: the main window's Windows 11 touches (spec #8 11, Q7; plan
// S8-M6 task 1): when Mica and the dark title bar are used.
//
#include "catch_amalgamated.hpp"

#include "model/FrameLook.h"

namespace m = fx::model;

TEST_CASE("Frame look: Mica on Windows 11 22H2 and later, unless high contrast or no transparency (S8-M6)", "[frame]")
{
    // Windows 11 22H2, light apps, transparency on: Mica, light title bar.
    m::FrameLook l = m::ChooseFrameLook(22621, false, true, true);
    CHECK(l.mica);
    CHECK_FALSE(l.darkTitleBar);
    // Dark apps: the title bar follows.
    CHECK(m::ChooseFrameLook(26100, false, true, false).darkTitleBar);
    // Windows 11 21H2 and Windows 10 (Q7): the same program, no backdrop.
    CHECK_FALSE(m::ChooseFrameLook(22000, false, true, true).mica);
    CHECK_FALSE(m::ChooseFrameLook(19045, false, true, true).mica);
    CHECK(m::ChooseFrameLook(19045, false, true, false).darkTitleBar);
    // Transparency effects off: no Mica.
    CHECK_FALSE(m::ChooseFrameLook(22621, false, false, true).mica);
    // High contrast: neither (the system draws the title bar in its colours).
    l = m::ChooseFrameLook(22621, true, true, false);
    CHECK_FALSE(l.mica);
    CHECK_FALSE(l.darkTitleBar);
}
