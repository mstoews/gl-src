//
// InfoBar.h - a page's information bar (spec #8 7.1; plan S8-M1 task 4).
//
// A strip across the top of a page with an icon and a line of text, in the
// system's information, warning or error look: "Period 6 is closed.",
// "Customer C004 saved.". Hidden, it takes no room; the page lays its form
// out below it.
//
#pragma once

#include <Windows.h>

#include <string>

namespace win32fx {

enum class InfoKind { Information, Success, Warning, Error };

class InfoBar {
public:
    InfoBar() = default;
    InfoBar(const InfoBar&) = delete;
    InfoBar& operator=(const InfoBar&) = delete;
    ~InfoBar();

    HWND Create(HWND parent, HINSTANCE inst);
    void Show(InfoKind kind, const std::wstring& text);
    void Hide();
    bool visible() const noexcept { return visible_; }
    InfoKind kind() const noexcept { return kind_; }
    const std::wstring& text() const noexcept { return text_; }

    // The bar's height when shown, at the parent's DPI; 0 when hidden.
    int Height() const;

    HWND hwnd() const noexcept { return hwnd_; }

private:
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    void Paint(HDC dc);

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    InfoKind kind_ = InfoKind::Information;
    std::wstring text_;
    bool visible_ = false;
};

} // namespace win32fx
