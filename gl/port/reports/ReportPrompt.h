//
// ReportPrompt.h - a user report's questions, IDD_RW_PROMPT in port.rc (spec
// #7 6.3, D6; plan S7-M3). New: the original's Crystal reports asked through
// Crystal's own parameter prompts.
//
// One row per asked filter (rw::Filter::ask), up to rw::kMaxQuestions, each
// with the inputs its field takes: a period combo for a period, date boxes
// for a date, a customer combo for a customer, a text box otherwise; two for
// "from ... to". OK checks each answer against its field (rw::FilterProblem).
//
#pragma once

#include "../ReportWriter.h"
#include "ReportOptions.h"   // ReportPanel

#include <Windows.h>

#include <memory>

// Asks `def`'s questions over `owner`. `answers` holds the last answers to
// start from (by filter index), and gets the new ones. False on Cancel, or
// when the report asks nothing it can show.
bool AskReportQuestions(HWND owner, const rw::ReportDefinition& def, rw::Answers& answers, bool closeAtOnce = false);

// Spec #8 G1: the same questions as a panel above the report's preview
// (IDD_PNL_RW_PROMPT). `def` and `answers` must outlive it; Read stores the
// answers in `answers` after OK's checks.
std::unique_ptr<ReportPanel> MakeQuestionsPanel(const rw::ReportDefinition& def, rw::Answers& answers);
