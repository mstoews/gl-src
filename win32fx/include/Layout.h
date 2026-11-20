//
// Layout.h - a form's controls follow its size (spec #8 10; plan S8-M1
// task 3). The arithmetic is model::Place (model/Anchors.h); this keeps each
// control's rectangle as the template laid it out, and moves the controls
// with DeferWindowPos when the form is sized.
//
// A control without an anchor stays where the template put it (top left).
// Covers() lists the controls without one, for /checkres: every control of a
// page should say how it moves.
//
#pragma once

#include "model/Anchors.h"

#include <Windows.h>

#include <utility>
#include <vector>

namespace win32fx {

class Layout {
public:
    // Records `form`'s client size and each child's rectangle, and the anchors
    // by control ID.
    void Capture(HWND form, const std::vector<std::pair<int, unsigned>>& anchors);

    // Lays the controls out for a form `cx` by `cy` (never below original()).
    void Apply(int cx, int cy) const;

    // After a DPI change Windows rescales the form's controls; the recorded
    // rectangles follow.
    void Rescale(UINT oldDpi, UINT newDpi);

    model::Size original() const noexcept { return original_; }

    // The IDs of `form`'s controls (with an ID) that have no anchor.
    static std::vector<int> Unanchored(HWND form, const std::vector<std::pair<int, unsigned>>& anchors);

private:
    struct Item {
        HWND hwnd;
        model::Box box;
        unsigned anchors;
    };
    HWND form_ = nullptr;
    model::Size original_;
    std::vector<Item> items_;
};

} // namespace win32fx
