// Bench portable module — see Markdown.h.
#include "Markdown.h"

#include "imgui.h"

#include <string>
#include <vector>

namespace bench {
namespace {

// Inline styles applied to a run of text. Bold / italic / bold-italic are faked
// with colour (the bench bundles a single proportional font, no bold/italic cut);
// strikethrough draws a mid-line over the run.
enum Style { kPlain = 0, kBold, kItalic, kBoldItalic, kCode, kLink, kStrike };

struct Run { std::string text; int style; std::string url; };

// Link the user clicked this frame (UI thread only). RenderMarkdown clears it on
// entry and returns it; drawWord sets it. Lets the host open the target doc.
std::string g_mdClicked;

// A backslash before one of these emits the char literally (CommonMark escapes).
bool isEscapable(char c)
{
    switch (c) {
    case '\\': case '`': case '*': case '_': case '{': case '}':
    case '[':  case ']': case '(': case ')': case '#': case '+':
    case '-':  case '.': case '!': case '|': case '~': case '>':
        return true;
    default: return false;
    }
}

// Parse one logical line into styled runs: ***bold italic***, **bold**,
// *italic*/_italic_, ~~strike~~, `code`, [text](url), and \x escapes. Unmatched
// markers are emitted as literal text.
std::vector<Run> parseInline(const std::string& s)
{
    std::vector<Run> runs;
    std::string cur;
    auto flush = [&]() { if (!cur.empty()) { runs.push_back({cur, kPlain, ""}); cur.clear(); } };

    for (size_t i = 0; i < s.size();) {
        const char c = s[i];
        if (c == '\\' && i + 1 < s.size() && isEscapable(s[i + 1])) {   // \* -> literal *
            cur += s[i + 1]; i += 2; continue;
        }
        if (c == '`') {                                   // `inline code`
            const size_t e = s.find('`', i + 1);
            if (e == std::string::npos) { cur += c; ++i; continue; }
            flush(); runs.push_back({s.substr(i + 1, e - i - 1), kCode, ""}); i = e + 1; continue;
        }
        if (c == '*' && i + 2 < s.size() && s[i + 1] == '*' && s[i + 2] == '*') {  // ***bold italic***
            const size_t e = s.find("***", i + 3);
            if (e != std::string::npos) {
                flush(); runs.push_back({s.substr(i + 3, e - i - 3), kBoldItalic, ""}); i = e + 3; continue;
            }
        }
        if (c == '*' && i + 1 < s.size() && s[i + 1] == '*') {   // **bold**
            const size_t e = s.find("**", i + 2);
            if (e == std::string::npos) { cur += c; ++i; continue; }
            flush(); runs.push_back({s.substr(i + 2, e - i - 2), kBold, ""}); i = e + 2; continue;
        }
        if (c == '~' && i + 1 < s.size() && s[i + 1] == '~') {   // ~~strikethrough~~
            const size_t e = s.find("~~", i + 2);
            if (e != std::string::npos) {
                flush(); runs.push_back({s.substr(i + 2, e - i - 2), kStrike, ""}); i = e + 2; continue;
            }
        }
        if (c == '*' || c == '_') {                       // *italic* / _italic_
            const size_t e = s.find(c, i + 1);
            if (e == std::string::npos || e == i + 1) { cur += c; ++i; continue; }
            flush(); runs.push_back({s.substr(i + 1, e - i - 1), kItalic, ""}); i = e + 1; continue;
        }
        if (c == '[') {                                   // [text](url)
            const size_t rb = s.find(']', i + 1);
            if (rb != std::string::npos && rb + 1 < s.size() && s[rb + 1] == '(') {
                const size_t rp = s.find(')', rb + 2);
                if (rp != std::string::npos) {
                    flush();
                    runs.push_back({s.substr(i + 1, rb - i - 1), kLink, s.substr(rb + 2, rp - rb - 2)});
                    i = rp + 1; continue;
                }
            }
            cur += c; ++i; continue;
        }
        cur += c; ++i;
    }
    flush();
    return runs;
}

ImVec4 styleColor(int style)
{
    switch (style) {
    case kBold:       return ImVec4(1.00f, 1.00f, 1.00f, 1.0f);
    case kItalic:     return ImVec4(0.78f, 0.86f, 1.00f, 1.0f);
    case kBoldItalic: return ImVec4(0.96f, 0.91f, 0.72f, 1.0f);   // warm: distinct from bold + italic
    case kCode:       return ImVec4(0.96f, 0.58f, 0.46f, 1.0f);
    case kLink:       return ImVec4(0.40f, 0.66f, 1.00f, 1.0f);
    case kStrike:     return ImVec4(0.62f, 0.64f, 0.68f, 1.0f);   // muted
    default:          return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }
}

// Render one styled word at the cursor (code gets a subtle padded box, links
// underline + are clickable, strikethrough gets a mid-line).
void drawWord(const std::string& w, int style, const std::string& url)
{
    const ImVec4 col = styleColor(style);
    const ImVec2 sz  = ImGui::CalcTextSize(w.c_str());
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl   = ImGui::GetWindowDrawList();
    if (style == kCode)
        dl->AddRectFilled(ImVec2(pos.x - 2.0f, pos.y - 1.0f),
                          ImVec2(pos.x + sz.x + 2.0f, pos.y + sz.y + 1.0f),
                          IM_COL32(64, 64, 78, 160), 3.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextUnformatted(w.c_str());
    ImGui::PopStyleColor();
    if (style == kStrike) {
        const ImU32 u = ImGui::ColorConvertFloat4ToU32(col);
        const float y = pos.y + sz.y * 0.5f;
        dl->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + sz.x, y), u);
    }
    if (style == kLink) {
        const ImU32 u = ImGui::ColorConvertFloat4ToU32(col);
        dl->AddLine(ImVec2(pos.x, pos.y + sz.y - 1.0f), ImVec2(pos.x + sz.x, pos.y + sz.y - 1.0f), u);
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (!url.empty()) ImGui::SetTooltip("%s", url.c_str());
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !url.empty())
                g_mdClicked = url;   // the host opens it (or copies, if not a local file)
        }
    }
}

// Render runs as wrapped prose: word by word, one space between words, wrapping
// at the content-region width. Honours the current cursor X (so indented list
// items / blockquotes / table cells wrap within their column).
void renderInline(const std::vector<Run>& runs)
{
    const float avail  = ImGui::GetContentRegionAvail().x;
    const float spaceW = ImGui::CalcTextSize(" ").x;
    float lineW = 0.0f;
    bool  lineStart = true;

    for (const auto& r : runs) {
        // split the run into whitespace-separated words (code keeps its spaces as
        // one boxed unit so "a b" stays together).
        std::vector<std::string> words;
        if (r.style == kCode) {
            words.push_back(r.text);
        } else {
            std::string w;
            for (char c : r.text) {
                if (c == ' ' || c == '\t') { if (!w.empty()) { words.push_back(w); w.clear(); } }
                else w += c;
            }
            if (!w.empty()) words.push_back(w);
        }
        for (const auto& w : words) {
            const float ww = ImGui::CalcTextSize(w.c_str()).x;
            if (lineStart) {
                drawWord(w, r.style, r.url); lineW = ww; lineStart = false;
            } else if (lineW + spaceW + ww <= avail) {
                ImGui::SameLine(0.0f, spaceW); drawWord(w, r.style, r.url); lineW += spaceW + ww;
            } else {
                drawWord(w, r.style, r.url); lineW = ww;   // wrap: new line (no SameLine)
            }
        }
    }
}

bool isHrule(const std::string& t)
{
    if (t.size() < 3) return false;
    const char c = t[0];
    if (c != '-' && c != '*' && c != '_') return false;
    int n = 0;
    for (char ch : t) {
        if (ch == c) ++n;
        else if (ch != ' ') return false;
    }
    return n >= 3;
}

// A line of all '=' (H1) or all '-' (H2) underlining the previous text line.
// Returns the underline char, or 0. Only consulted when the previous line is a
// paragraph, so a standalone '---' after a blank line still reads as an hrule.
char setextChar(const std::string& t)
{
    if (t.empty()) return 0;
    const char c = t[0];
    if (c != '=' && c != '-') return 0;
    for (char ch : t) if (ch != c) return 0;   // line is already rstripped
    return c;
}

std::string rstrip(const std::string& s)
{
    size_t e = s.size();
    while (e > 0 && (s[e - 1] == '\r' || s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return s.substr(0, e);
}

std::string trim(const std::string& s)
{
    const size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

// --- Pipe tables ---------------------------------------------------------------

// Split a table row on unescaped '|', trim each cell, and drop the empty cells
// produced by an optional leading / trailing pipe ("| a | b |" -> {"a","b"}).
std::vector<std::string> splitRow(const std::string& s)
{
    std::vector<std::string> cells;
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) { cur += s[i]; cur += s[i + 1]; ++i; continue; }
        if (s[i] == '|') { cells.push_back(trim(cur)); cur.clear(); }
        else cur += s[i];
    }
    cells.push_back(trim(cur));
    if (!cells.empty() && cells.front().empty()) cells.erase(cells.begin());
    if (!cells.empty() && cells.back().empty())  cells.pop_back();
    return cells;
}

// A delimiter cell is optional ':' + one-or-more '-' + optional ':' (---, :--, --:, :-:).
bool isDelimCell(const std::string& c)
{
    if (c.empty()) return false;
    size_t i = 0;
    if (c[i] == ':') ++i;
    size_t dashes = 0;
    while (i < c.size() && c[i] == '-') { ++i; ++dashes; }
    if (i < c.size() && c[i] == ':') ++i;
    return i == c.size() && dashes >= 1;
}

// 0 = left, 1 = centre, 2 = right (from the delimiter cell's colons).
int cellAlign(const std::string& c)
{
    const bool l = !c.empty() && c.front() == ':';
    const bool r = !c.empty() && c.back()  == ':';
    if (l && r) return 1;
    if (r)      return 2;
    return 0;
}

// Render one table cell's inline content, offset for centre / right alignment.
void renderCell(const std::string& text, int align, bool header)
{
    const std::vector<Run> runs = parseInline(text);
    if (align != 0) {
        float w = 0.0f;
        for (const auto& r : runs) w += ImGui::CalcTextSize(r.text.c_str()).x;
        const float colW = ImGui::GetContentRegionAvail().x;
        const float off  = (align == 2) ? (colW - w) : (colW - w) * 0.5f;
        if (off > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
    }
    if (header) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.90f, 1.00f, 1.0f));
    renderInline(runs);
    if (header) ImGui::PopStyleColor();
}

} // namespace

std::string RenderMarkdown(const std::string& md)
{
    g_mdClicked.clear();
    const float baseSize = ImGui::GetStyle().FontSizeBase > 0.0f ? ImGui::GetStyle().FontSizeBase
                                                                 : ImGui::GetFontSize();
    // Heading scale by level (H1..H6).
    static const float kHScale[6] = { 1.70f, 1.45f, 1.22f, 1.08f, 1.00f, 0.92f };

    // Split into lines.
    std::vector<std::string> lines;
    {
        size_t start = 0;
        while (start <= md.size()) {
            const size_t nl = md.find('\n', start);
            const size_t end = (nl == std::string::npos) ? md.size() : nl;
            lines.push_back(rstrip(md.substr(start, end - start)));
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
    }

    for (size_t li = 0; li < lines.size(); ++li) {
        const std::string& line = lines[li];

        // Fenced code block: ``` or ~~~ (optionally with a language tag).
        const std::string ls = [&]{ size_t a = line.find_first_not_of(" \t"); return a == std::string::npos ? std::string() : line.substr(a); }();
        if (ls.rfind("```", 0) == 0 || ls.rfind("~~~", 0) == 0) {
            const std::string fence = ls.substr(0, 3);
            const std::string lang  = trim(ls.substr(3));
            std::string code;
            ++li;
            for (; li < lines.size(); ++li) {
                const std::string& cl = lines[li];
                const size_t a = cl.find_first_not_of(" \t");
                if (a != std::string::npos && cl.substr(a).rfind(fence, 0) == 0) break;
                code += cl; code += '\n';
            }
            // Header bar with the language tag (drawn behind a reserved row).
            if (!lang.empty()) {
                const ImVec2 p0   = ImGui::GetCursorScreenPos();
                const float  barW = ImGui::GetContentRegionAvail().x;
                const float  barH = ImGui::GetTextLineHeight() + 4.0f;
                ImDrawList*  dl   = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p0, ImVec2(p0.x + barW, p0.y + barH), IM_COL32(36, 36, 46, 255), 3.0f);
                ImGui::Dummy(ImVec2(barW, barH));
                dl->AddText(ImVec2(p0.x + 6.0f, p0.y + 2.0f), IM_COL32(150, 170, 205, 255), lang.c_str());
            }
            // Read-only, selectable code block.
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.86f, 0.88f, 0.78f, 1.0f));
            int nl = 1; for (char c : code) if (c == '\n') ++nl;
            const float h = ImGui::GetTextLineHeight() * static_cast<float>(nl) + 8.0f;
            // InputTextMultiline needs a mutable buffer; copy into a static-grown vector.
            std::vector<char> buf(code.begin(), code.end()); buf.push_back('\0');
            const std::string id = "##mdcode" + std::to_string(li);
            ImGui::InputTextMultiline(id.c_str(), buf.data(), buf.size(),
                                      ImVec2(-FLT_MIN, h),
                                      ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor(2);
            continue;
        }

        if (line.empty()) { ImGui::Spacing(); continue; }

        if (isHrule(line)) { ImGui::Separator(); continue; }

        // ATX heading.
        if (line[0] == '#') {
            int level = 0;
            while (level < 6 && level < static_cast<int>(line.size()) && line[level] == '#') ++level;
            if (level >= 1 && level <= 6 &&
                (level == static_cast<int>(line.size()) || line[level] == ' ')) {
                std::string text = line.substr(level);
                if (!text.empty() && text[0] == ' ') text.erase(0, 1);
                ImGui::Spacing();
                ImGui::PushFont(nullptr, baseSize * kHScale[level - 1]);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.80f, 0.98f, 1.0f));
                renderInline(parseInline(text));
                ImGui::PopStyleColor();
                ImGui::PopFont();
                if (level <= 2) ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                continue;
            }
        }

        // Blockquote (one level).
        if (line[0] == '>') {
            std::string text = line.substr(1);
            if (!text.empty() && text[0] == ' ') text.erase(0, 1);
            ImGui::Indent(10.0f);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(p.x - 8.0f, p.y), ImVec2(p.x - 5.0f, p.y + ImGui::GetTextLineHeight()),
                IM_COL32(120, 130, 150, 200));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.76f, 0.82f, 1.0f));
            renderInline(parseInline(text));
            ImGui::PopStyleColor();
            ImGui::Unindent(10.0f);
            continue;
        }

        // Pipe table: this line has a '|' and the next line is a delimiter row.
        if (line.find('|') != std::string::npos && li + 1 < lines.size()) {
            const std::vector<std::string> header = splitRow(line);
            const std::vector<std::string> delim  = splitRow(lines[li + 1]);
            bool isTable = !header.empty() && delim.size() == header.size();
            if (isTable) for (const auto& d : delim) if (!isDelimCell(d)) { isTable = false; break; }
            if (isTable) {
                std::vector<int> align;
                align.reserve(delim.size());
                for (const auto& d : delim) align.push_back(cellAlign(d));

                std::vector<std::vector<std::string>> rows;
                size_t j = li + 2;
                for (; j < lines.size(); ++j) {
                    if (lines[j].empty() || lines[j].find('|') == std::string::npos) break;
                    rows.push_back(splitRow(lines[j]));
                }

                const int cols = static_cast<int>(header.size());
                const std::string tid = "##mdtable" + std::to_string(li);
                const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                              ImGuiTableFlags_SizingStretchProp;
                if (ImGui::BeginTable(tid.c_str(), cols, flags)) {
                    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                    for (int c = 0; c < cols; ++c) {
                        ImGui::TableSetColumnIndex(c);
                        renderCell(header[c], align[c], /*header=*/true);
                    }
                    for (const auto& row : rows) {
                        ImGui::TableNextRow();
                        for (int c = 0; c < cols; ++c) {
                            ImGui::TableSetColumnIndex(c);
                            renderCell(c < static_cast<int>(row.size()) ? row[c] : std::string(),
                                       align[c], /*header=*/false);
                        }
                    }
                    ImGui::EndTable();
                }
                li = j - 1;   // outer ++li lands on the first post-table line
                continue;
            }
        }

        // Unordered list item — including GitHub task lists ("- [ ] " / "- [x] ").
        {
            const size_t a = line.find_first_not_of(" ");
            if (a != std::string::npos && a + 1 < line.size() &&
                (line[a] == '-' || line[a] == '*' || line[a] == '+') && line[a + 1] == ' ') {
                std::string content = line.substr(a + 2);
                bool isTask = false, checked = false;
                if (content.size() >= 3 && content[0] == '[' && content[2] == ']' &&
                    (content[1] == ' ' || content[1] == 'x' || content[1] == 'X') &&
                    (content.size() == 3 || content[3] == ' ')) {
                    isTask  = true;
                    checked = (content[1] == 'x' || content[1] == 'X');
                    content = content.size() > 3 ? content.substr(4) : std::string();
                }
                const float indent = 14.0f + static_cast<float>(a) * 8.0f;
                ImGui::Indent(indent);
                if (isTask) {
                    const float lh  = ImGui::GetTextLineHeight();
                    const float box = lh * 0.78f;
                    const ImVec2 p  = ImGui::GetCursorScreenPos();
                    const float top = p.y + (lh - box) * 0.5f;
                    ImDrawList* dl  = ImGui::GetWindowDrawList();
                    const ImVec2 a0(p.x, top), a1(p.x + box, top + box);
                    if (checked) {
                        dl->AddRectFilled(a0, a1, IM_COL32(90, 170, 110, 235), 3.0f);
                        dl->AddLine(ImVec2(p.x + box * 0.22f, top + box * 0.52f),
                                    ImVec2(p.x + box * 0.42f, top + box * 0.74f), IM_COL32(18, 28, 18, 255), 1.7f);
                        dl->AddLine(ImVec2(p.x + box * 0.42f, top + box * 0.74f),
                                    ImVec2(p.x + box * 0.80f, top + box * 0.26f), IM_COL32(18, 28, 18, 255), 1.7f);
                    } else {
                        dl->AddRect(a0, a1, IM_COL32(150, 155, 165, 220), 3.0f);
                    }
                    ImGui::Dummy(ImVec2(box, lh));
                } else {
                    ImGui::TextUnformatted("\xe2\x80\xa2");   // bullet •
                }
                ImGui::SameLine(0.0f, 6.0f);
                renderInline(parseInline(content));
                ImGui::Unindent(indent);
                continue;
            }
        }

        // Ordered list item ("N. text").
        {
            const size_t a = line.find_first_not_of(" ");
            if (a != std::string::npos) {
                size_t d = a;
                while (d < line.size() && line[d] >= '0' && line[d] <= '9') ++d;
                if (d > a && d + 1 < line.size() && line[d] == '.' && line[d + 1] == ' ') {
                    const float indent = 14.0f + static_cast<float>(a) * 8.0f;
                    ImGui::Indent(indent);
                    ImGui::TextUnformatted(line.substr(a, d - a + 1).c_str());   // "N."
                    ImGui::SameLine(0.0f, 6.0f);
                    renderInline(parseInline(line.substr(d + 2)));
                    ImGui::Unindent(indent);
                    continue;
                }
            }
        }

        // Setext heading: this paragraph line underlined by === (H1) or --- (H2).
        if (li + 1 < lines.size()) {
            const char sc = setextChar(lines[li + 1]);
            if (sc) {
                ImGui::Spacing();
                ImGui::PushFont(nullptr, baseSize * (sc == '=' ? kHScale[0] : kHScale[1]));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.80f, 0.98f, 1.0f));
                renderInline(parseInline(line));
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                ++li;   // consume the underline
                continue;
            }
        }

        // Paragraph.
        renderInline(parseInline(line));
    }
    return g_mdClicked;
}

} // namespace bench
