// Bench portable module — see Markdown.h.
#include "Markdown.h"

#include "imgui.h"

#include <string>
#include <vector>

namespace bench {
namespace {

// Inline styles applied to a run of text.
enum Style { kPlain = 0, kBold, kItalic, kCode, kLink };

struct Run { std::string text; int style; std::string url; };

// Link the user clicked this frame (UI thread only). RenderMarkdown clears it on
// entry and returns it; drawWord sets it. Lets the host open the target doc.
std::string g_mdClicked;

// Parse one logical line into styled runs: **bold**, *italic*/_italic_,
// `code`, [text](url). Unmatched markers are emitted as literal text.
std::vector<Run> parseInline(const std::string& s)
{
    std::vector<Run> runs;
    std::string cur;
    auto flush = [&]() { if (!cur.empty()) { runs.push_back({cur, kPlain, ""}); cur.clear(); } };

    for (size_t i = 0; i < s.size();) {
        const char c = s[i];
        if (c == '`') {                                   // `inline code`
            const size_t e = s.find('`', i + 1);
            if (e == std::string::npos) { cur += c; ++i; continue; }
            flush(); runs.push_back({s.substr(i + 1, e - i - 1), kCode, ""}); i = e + 1; continue;
        }
        if (c == '*' && i + 1 < s.size() && s[i + 1] == '*') {   // **bold**
            const size_t e = s.find("**", i + 2);
            if (e == std::string::npos) { cur += c; ++i; continue; }
            flush(); runs.push_back({s.substr(i + 2, e - i - 2), kBold, ""}); i = e + 2; continue;
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
    case kBold:   return ImVec4(1.00f, 1.00f, 1.00f, 1.0f);
    case kItalic: return ImVec4(0.78f, 0.86f, 1.00f, 1.0f);
    case kCode:   return ImVec4(0.96f, 0.58f, 0.46f, 1.0f);
    case kLink:   return ImVec4(0.40f, 0.66f, 1.00f, 1.0f);
    default:      return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }
}

// Render one styled word at the cursor (code gets a subtle box, links underline
// + are clickable → copy the URL to the clipboard).
void drawWord(const std::string& w, int style, const std::string& url)
{
    const ImVec4 col = styleColor(style);
    const ImVec2 sz  = ImGui::CalcTextSize(w.c_str());
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl   = ImGui::GetWindowDrawList();
    if (style == kCode)
        dl->AddRectFilled(ImVec2(pos.x - 1.0f, pos.y), ImVec2(pos.x + sz.x + 1.0f, pos.y + sz.y),
                          IM_COL32(64, 64, 78, 150), 2.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextUnformatted(w.c_str());
    ImGui::PopStyleColor();
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
// items / blockquotes wrap within their column).
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

std::string rstrip(const std::string& s)
{
    size_t e = s.size();
    while (e > 0 && (s[e - 1] == '\r' || s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return s.substr(0, e);
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
            std::string code;
            ++li;
            for (; li < lines.size(); ++li) {
                const std::string& cl = lines[li];
                const size_t a = cl.find_first_not_of(" \t");
                if (a != std::string::npos && cl.substr(a).rfind(fence, 0) == 0) break;
                code += cl; code += '\n';
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

        // Unordered list item.
        {
            const size_t a = line.find_first_not_of(" ");
            if (a != std::string::npos && a + 1 < line.size() &&
                (line[a] == '-' || line[a] == '*' || line[a] == '+') && line[a + 1] == ' ') {
                const float indent = 14.0f + static_cast<float>(a) * 8.0f;
                ImGui::Indent(indent);
                ImGui::TextUnformatted("\xe2\x80\xa2");   // bullet •
                ImGui::SameLine(0.0f, 6.0f);
                renderInline(parseInline(line.substr(a + 2)));
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

        // Paragraph.
        renderInline(parseInline(line));
    }
    return g_mdClicked;
}

} // namespace bench
