// Bench portable module — ImGuiColorTextEdit language definitions, keyed by a
// language id so a target just declares "6502" / "68000" / "C". Shared by
// POM1 / POM2 / NeoST. Depends only on the (vendored) ImGuiColorTextEdit.
#ifndef BENCH_LANG_H
#define BENCH_LANG_H

#include <string>
#include "TextEditor.h"

namespace bench {

// Cached LanguageDefinition for the given language id:
//   "6502"  — NMOS mnemonics + ca65 directives
//   "68000" — Motorola 68000 mnemonics (starter set, for NeoST)
//   "C"     — ImGuiColorTextEdit's built-in C
//   else    — plain text (no highlighting)
const TextEditor::LanguageDefinition& langDef(const std::string& language);

} // namespace bench

#endif // BENCH_LANG_H
