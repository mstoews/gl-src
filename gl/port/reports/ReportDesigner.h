//
// ReportDesigner.h - the report writer's designer, IDD_RW_DESIGN in port.rc
// (spec #7 6.2, D5, D9; plan S7-M2). It replaces the original's
// GL_ADDREPORT (TGLAddReport / TGLModifyReport, gl/src/ar_rept.cpp), which
// only registered a Crystal file's description, name and path.
//
// Reporting > Custom Report opens it: Add on a new definition, Modify on a
// user report, Copy on a copy of one. It edits one ReportDefinition
// (ReportWriter.h): its name, description, title, subtitle and orientation;
// its source; its columns (fields added in order, each with a label, width,
// total and blank-when-zero); three sort fields; two groups; its filters;
// the grand total and counts. Preview builds it as it stands into a preview
// window over the dialog, without saving; Save checks it (rw::CheckDefinition)
// and saves it to gl_rptdef; Close asks to save changes.
//
// Spec #8 G1: the gallery opens the designer as a page instead
// (OpenReportDesigner): the same form on the left, and on the right a preview
// that is built again a moment after each change (debounced), without saving.
// A report that asks questions is previewed once they are answered: its
// Preview button asks them. The page's tab shows "•" while there are unsaved
// changes; Save (or Ctrl+S) saves, and closing the tab asks to save. The
// modal designer stays for Custom Report (classic).
//
#pragma once

#include "win32fx.h"

#include <Windows.h>

// Opens the designer over `owner` on definition `number` (0: a new report);
// with `copy`, on a copy of it, saved as a new report. Returns the number
// saved last, or 0 if nothing was saved.
long RunReportDesigner(HWND owner, long number, bool copy = false, bool closeAtOnce = false);

// The designer page in `host` on definition `number` (0: a new report), or a
// copy of it with `copy`. A definition already open in a page is shown
// again. Returns the page, or null.
HWND OpenReportDesigner(fx::PageHost& host, long number, bool copy = false);

// gl.exe /stress: opens and closes the designer `times` times on a new and an
// existing definition; returns the change in GDI + USER objects (-1 if it
// failed to open).
int StressReportDesigner(int times);
