//
// Ask.cpp - see Ask.h.
//
#include "Ask.h"

#include <CommCtrl.h>

#pragma comment(lib, "comctl32.lib")

namespace win32fx {

namespace {

HRESULT CALLBACK askCallback(HWND, UINT, WPARAM, LPARAM, LONG_PTR)
{
    return S_OK;
}

} // namespace

int Ask(HWND owner, const std::wstring& title, const std::wstring& instruction, const std::wstring& content,
        const std::vector<AskButton>& buttons, int defaultId, int cancelId, AskIcon icon)
{
    std::vector<TASKDIALOG_BUTTON> b;
    b.reserve(buttons.size());
    for (const AskButton& x : buttons) b.push_back({x.id, x.text.c_str()});
    TASKDIALOGCONFIG cfg{};
    cfg.cbSize = sizeof cfg;
    cfg.hwndParent = owner;
    cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    cfg.pszWindowTitle = title.c_str();
    cfg.pszMainInstruction = instruction.c_str();
    cfg.pszContent = content.empty() ? nullptr : content.c_str();
    cfg.cButtons = static_cast<UINT>(b.size());
    cfg.pButtons = b.data();
    cfg.nDefaultButton = defaultId;
    cfg.pfCallback = askCallback;
    switch (icon) {
    case AskIcon::Information: cfg.pszMainIcon = TD_INFORMATION_ICON; break;
    case AskIcon::Warning: cfg.pszMainIcon = TD_WARNING_ICON; break;
    case AskIcon::Error: cfg.pszMainIcon = TD_ERROR_ICON; break;
    case AskIcon::Question: cfg.pszMainIcon = TD_INFORMATION_ICON; break;
    case AskIcon::None: break;
    }
    int chosen = cancelId;
    if (FAILED(TaskDialogIndirect(&cfg, &chosen, nullptr, nullptr))) return cancelId;
    return chosen == IDCANCEL ? cancelId : chosen;
}

} // namespace win32fx
