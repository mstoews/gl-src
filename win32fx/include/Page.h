//
// Page.h - a redesigned screen (spec #8 5; plan S8-M1 task 2).
//
// A page is a modeless HostedPage that a workspace shows in a tab. It holds:
//   - an information bar along its top (InfoBar.h), hidden until needed;
//   - its form: a dialog template with WS_CHILD | DS_CONTROL, created as a
//     child, laid out by anchors (Layout.h) and scrolled when the window is
//     smaller than the template.
//
// The form's controls notify the page: OnFormCommand / OnFormNotify. Enter
// moves to the next field (spec #8 Q3), Esc does nothing; Ctrl+S and
// Ctrl+Enter save, Ctrl+N starts a new record (OnShortcut, from the
// workspace's key filter). A change to an edit (EN_CHANGE, unless the page is
// filling its fields: Filling) marks the page dirty, and its tab shows "•".
// Closing a dirty page asks to save it (CanClose).
//
// Fields are read and written as UTF-8, as MainDialog does (the same
// win32fx string functions). A field can have a validator
// (model::Validator): it runs when the field loses the focus and marks it
// (MarkField: a balloon tip, and its label in the error colour). Each page
// owns the tables it opens and closes them itself: never cb.closeAll() (D7).
//
#pragma once

#include "InfoBar.h"
#include "Layout.h"
#include "PageHost.h"
#include "model/Validate.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace win32fx {

class Page : public HostedPage {
public:
    explicit Page(UINT formTemplate) : template_(formTemplate) {}

    // Creates the page in `host` with `title`, active. Null on failure.
    HWND Open(PageHost& host, const std::wstring& title);

    HWND form() const noexcept { return form_; }
    HWND Item(int id) const noexcept { return form_ ? GetDlgItem(form_, id) : nullptr; }
    const std::wstring& title() const noexcept { return title_; }
    void SetPageTitle(const std::wstring& title);

    // ---- changes ----
    bool IsDirty() const noexcept { return dirty_; }
    void SetDirty(bool dirty);
    // Saves the page's changes; false (with the problem shown) if it can't.
    virtual bool Save() { SetDirty(false); return true; }
    // Unsaved changes: asks to save them. False to keep the page open.
    virtual bool CanClose();

    // ---- keys from the workspace (Ctrl+S, Ctrl+Enter, Ctrl+N) ----
    virtual bool OnShortcut(const MSG& msg);

    // ---- fields (UTF-8) ----
    std::string Text(int id) const;
    void SetText(int id, const std::string& utf8);
    bool Checked(int id) const;
    void SetChecked(int id, bool on);
    void SetValidator(int id, model::Validator v) { validators_[id] = std::move(v); }
    // Runs a field's validator, marking or clearing it. True when it passes.
    bool Validate(int id);
    void MarkField(int id, const std::string& message, bool focus = false);
    void ClearMark(int id);
    void ClearMarks();
    bool marked(int id) const { return marks_.count(id) != 0; }

    // ---- the information bar ----
    void ShowInfo(InfoKind kind, const std::wstring& text);
    void HideInfo();
    const InfoBar& info() const noexcept { return info_; }

    static Page* FromHandle(HWND hwnd) noexcept;

protected:
    // After the form exists: fill it, set anchors (SetAnchors), validators.
    virtual bool OnInitPage() { return true; }
    virtual bool OnFormCommand(WORD /*id*/, WORD /*code*/, HWND /*ctl*/) { return false; }
    virtual bool OnFormNotify(NMHDR* /*nm*/, LRESULT& /*result*/) { return false; }
    // An edit changed (EN_CHANGE, not while filling): by default the page is dirty.
    virtual void OnFieldChanged(int id);
    virtual void OnNew() {}

    void SetAnchors(std::vector<std::pair<int, unsigned>> anchors);
    const std::vector<std::pair<int, unsigned>>& anchors() const noexcept { return anchors_; }

    // While true, EN_CHANGE isn't an edit (the page is filling its fields).
    class Filling {
    public:
        explicit Filling(Page& p) : p_(p), was_(p.filling_) { p_.filling_ = true; }
        ~Filling() { p_.filling_ = was_; }
        Filling(const Filling&) = delete;
        Filling& operator=(const Filling&) = delete;

    private:
        Page& p_;
        bool was_;
    };
    bool filling() const noexcept { return filling_; }

    bool OnCreate(LPCREATESTRUCTW cs) override;
    bool OnSize(UINT type, int cx, int cy) override;
    bool OnDestroy() override;
    bool OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override;

private:
    static INT_PTR CALLBACK FormProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    INT_PTR FormMessage(UINT msg, WPARAM wp, LPARAM lp);
    void Relayout();
    void Scroll(int bar, WORD code);
    HWND labelOf(int id) const;

    UINT template_;
    HWND form_ = nullptr;
    InfoBar info_;
    Layout layout_;
    std::vector<std::pair<int, unsigned>> anchors_;
    std::wstring title_;
    bool dirty_ = false;
    bool filling_ = false;
    int sx_ = 0, sy_ = 0;
    UINT dpi_ = 96;
    HWND lastFocus_ = nullptr;
    std::map<int, model::Validator> validators_;
    std::map<int, std::string> marks_;
};

} // namespace win32fx
