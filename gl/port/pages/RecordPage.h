//
// RecordPage.h - a list-and-detail page made from a descriptor (spec #8 5;
// plan S8-M2). The records in a list you filter as you type; the selected
// one's fields beside it, in groups; New, Save and Delete. The page is the
// Customers page's pattern (CustomersPage.cpp), driven by a PageDesc and a
// RecordModel instead of written for one table.
//
// The descriptors and the forms are generated together by
// gl/res/tools/make_pages.py (PageDescs.inc, gl/res/pages.rc).
//
#pragma once

#include "win32fx.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

enum class FieldKind { Text, Upper, Number, Decimal, Amount, Date, Choice, Account, Password, ReadOnly };
// Key: required, and read-only once saved; KeyPart: read-only once saved.
// Input (spec #10 D5): a command's input, not part of the record: the model
// gets it (RecordModel::InputDefaults, then what's typed) and its read-only
// fields may show what follows from it, but it never makes the page dirty,
// Save doesn't check it, and choosing another record starts it again.
enum class FieldFlag { None, Required, Key, KeyPart, Input };

struct FieldDesc {
    int id;            // the edit, combo box or date picker
    const char* key;   // RecordModel's field
    FieldKind kind;
    int width;         // characters (the table's field width)
    FieldFlag flag;
    int dept, name;    // Account: the dept edit (key "<key>_dept") and the name beside them
};

struct ColumnDesc {
    const wchar_t* heading;
    int width;         // at 96 DPI
    bool right;
};

struct ButtonDesc {
    int id;
    const wchar_t* text;
};

// A tab below the record's groups (spec #9 S9-M0): its own fields, or a grid
// (an entry grid over the model's RecordModel::Grid(gridKey)).
struct TabDesc {
    const wchar_t* caption;
    int grid;              // the grid's ListView, or 0
    const char* gridKey;   // RecordModel::Grid's argument
    std::vector<int> ids;  // every control on the tab, shown only while it's chosen
};

struct PageDesc {
    UINT form;
    const wchar_t* title;
    bool commands;     // the Users page: the level's commands beside the user
    bool nolist;       // one record (Company Profile, spec #9 S9-M2): no filter, list, New or Delete
    std::vector<ColumnDesc> columns;
    std::vector<FieldDesc> fields;
    std::vector<std::pair<int, unsigned>> anchors;
    std::vector<ButtonDesc> buttons;   // the page's own, after Delete
    std::vector<TabDesc> tabs;         // none, or the tab strip's tabs
};

constexpr int kPageFilter = 21000, kPageList = 21001, kPageCommands = 21002, kPageTabs = 21004;

// ---- the pages (RecordPages.cpp) -------------------------------------------

// The command a page replaces opens it (or brings the open one forward); null
// if the command isn't one of these pages.
HWND OpenRecordPage(fx::PageHost& host, WORD command);
bool IsRecordPageCommand(WORD command);
// A record page shows the record with `key` (command search); false if it isn't one, or the record isn't on file.
bool ShowRecord(HWND page, const std::string& key);
// A record page shows its tab captioned `caption` ("Budget": Budget Data, spec #9 S9-M1); false if it has none.
bool ShowRecordTab(HWND page, const std::wstring& caption);

// The commands that became pages, each with its classic dialog's command
// (spec #8 Q4: the dialogs stay until the acceptance) and its name there.
struct ClassicCommand {
    WORD page, classic;
    const wchar_t* text;
};
const std::vector<ClassicCommand>& ClassicCommands();

// /checkres: every page's form and anchors.
std::vector<const PageDesc*> RecordPageDescs();

// /stress: each page opened in a hidden workspace, filtered, two records
// selected and closed, `times` times; the tab check page (S9-M0) too, each
// tab chosen and its grid typed into. GDI + USER object growth, or -1.
int StressRecordPages(int times);
