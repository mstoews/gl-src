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
#include <utility>
#include <vector>

enum class FieldKind { Text, Upper, Number, Decimal, Amount, Date, Choice, Account, Password, ReadOnly };
enum class FieldFlag { None, Required, Key, KeyPart };   // Key: required, and read-only once saved; KeyPart: read-only once saved

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

struct PageDesc {
    UINT form;
    const wchar_t* title;
    bool commands;     // the Users page: the level's commands beside the user
    std::vector<ColumnDesc> columns;
    std::vector<FieldDesc> fields;
    std::vector<std::pair<int, unsigned>> anchors;
    std::vector<ButtonDesc> buttons;   // the page's own, after Delete
};

constexpr int kPageFilter = 21000, kPageList = 21001, kPageCommands = 21002;

// ---- the pages (RecordPages.cpp) -------------------------------------------

// The command a page replaces opens it (or brings the open one forward); null
// if the command isn't one of these pages.
HWND OpenRecordPage(fx::PageHost& host, WORD command);
bool IsRecordPageCommand(WORD command);

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
// selected and closed, `times` times. GDI + USER object growth, or -1.
int StressRecordPages(int times);
