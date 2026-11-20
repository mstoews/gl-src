//
// model/Anchors.h - where a control goes when its page resizes (spec #8 10;
// plan S8-M1 task 3). Portable, header-only: fx::Layout (Layout.h) applies
// it to windows, and gl_tests checks the arithmetic.
//
// A control keeps its distance from each edge it is anchored to. Anchored
// left and right, it stretches with the page's width; right only, it moves
// with the right edge; left only (or neither), it stays. Likewise top and
// bottom. A page never lays its controls out smaller than its template:
// below that size it scrolls (Layout clamps `now` to `original`).
//
#pragma once

namespace win32fx::model {

enum Anchor : unsigned { kLeft = 1, kTop = 2, kRight = 4, kBottom = 8, kTopLeft = kLeft | kTop,
                         kTopRight = kTop | kRight, kTopWide = kLeft | kTop | kRight,
                         kAll = kLeft | kTop | kRight | kBottom, kLeftTall = kLeft | kTop | kBottom,
                         kBottomLeft = kLeft | kBottom, kBottomRight = kRight | kBottom,
                         kBottomWide = kLeft | kRight | kBottom };

struct Box {
    int left = 0, top = 0, right = 0, bottom = 0;
    int width() const noexcept { return right - left; }
    int height() const noexcept { return bottom - top; }
    bool operator==(const Box&) const = default;
};

struct Size {
    int cx = 0, cy = 0;
};

// The page's size as laid out: never below its template's.
inline Size Clamp(Size now, Size original) noexcept
{
    return {now.cx < original.cx ? original.cx : now.cx, now.cy < original.cy ? original.cy : now.cy};
}

// Where a control at `box` (laid out for a page `original` in size) goes in a
// page `now` in size.
inline Box Place(Box box, unsigned anchors, Size original, Size now) noexcept
{
    const Size n = Clamp(now, original);
    const int dx = n.cx - original.cx, dy = n.cy - original.cy;
    Box b = box;
    if (anchors & kRight) {
        b.right += dx;
        if (!(anchors & kLeft)) b.left += dx;
    }
    if (anchors & kBottom) {
        b.bottom += dy;
        if (!(anchors & kTop)) b.top += dy;
    }
    return b;
}

} // namespace win32fx::model

namespace fx = win32fx;   // as win32fx.h names it: portable code uses fx::model too
