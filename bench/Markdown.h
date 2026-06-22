// Bench portable module — a lightweight Markdown preview renderer for the
// DevBench editor (the "presentation" half of opening a .md file). NOT a full
// CommonMark parser: it covers the constructs the project's docs actually use —
// ATX (#..######) and setext (===/---) headings, bold/italic/***bold-italic***,
// ~~strikethrough~~, inline `code`, [links](url), \-escapes, fenced code blocks
// (``` / ~~~, with a language-tag header bar), unordered (- * +) / ordered (1.) /
// GitHub task-list (- [ ] / - [x]) lists, pipe tables (with :--:/--: alignment),
// blockquotes (>), and horizontal rules (--- *** ___). Depends only on ImGui.
#ifndef BENCH_MARKDOWN_H
#define BENCH_MARKDOWN_H

#include <string>

namespace bench {

// Render `md` into the current ImGui window/child as a read-only formatted view.
// Call inside a scrollable child for a document-style preview. Returns the URL of
// a link the user clicked this frame (empty otherwise) so the caller can follow
// it — e.g. open another markdown document.
std::string RenderMarkdown(const std::string& md);

} // namespace bench

#endif // BENCH_MARKDOWN_H
