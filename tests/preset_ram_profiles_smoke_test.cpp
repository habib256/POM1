#include <cassert>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string readFile(const char* path)
{
    std::ifstream in(path);
    if (!in) {
        std::cerr << "Unable to open " << path << "\n";
        std::exit(2);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string lowerAscii(std::string s)
{
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::string firstQuotedString(const std::string& text)
{
    const std::size_t begin = text.find('"');
    assert(begin != std::string::npos);
    const std::size_t end = text.find('"', begin + 1);
    assert(end != std::string::npos);
    return text.substr(begin + 1, end - begin - 1);
}

int ramKbBeforeBasicType(const std::string& text)
{
    const std::size_t basic = text.find("BasicType::");
    assert(basic != std::string::npos);
    std::size_t end = basic;
    while (end > 0 && !std::isdigit(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    assert(end > 0);
    std::size_t begin = end;
    while (begin > 0 && std::isdigit(static_cast<unsigned char>(text[begin - 1]))) {
        --begin;
    }
    return std::stoi(text.substr(begin, end - begin));
}

std::string basicTypeToken(const std::string& text)
{
    const std::size_t begin = text.find("BasicType::");
    assert(begin != std::string::npos);
    std::size_t end = begin;
    while (end < text.size() &&
           (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == ':')) {
        ++end;
    }
    return text.substr(begin, end - begin);
}

// Count the data rows of the README "Machine Presets" table — Markdown lines
// of the form `| <number> | ...`. The table is found by its header row
// (`| # | Preset | RAM | BASIC | Cards |`) and ends at the first line that no
// longer starts with a pipe. Returns -1 if the table header is absent.
int readmePresetRowCount(const std::string& readme)
{
    const std::size_t header = readme.find("| # | Preset | RAM | BASIC | Cards |");
    if (header == std::string::npos) return -1;
    std::istringstream rows(readme.substr(header));
    std::string line;
    int count = 0;
    bool started = false;
    while (std::getline(rows, line)) {
        std::size_t i = 0;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= line.size() || line[i] != '|') {
            if (started) break;   // table ended
            continue;
        }
        started = true;
        // Skip the header and the `|:--|` separator; count `| <digit>` rows.
        std::size_t j = i + 1;
        while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j]))) ++j;
        if (j < line.size() && std::isdigit(static_cast<unsigned char>(line[j]))) ++count;
    }
    return count;
}

// The machine-config core of one preset entry (everything a DevBench mirror
// must keep in lockstep — name/description/layout deliberately excluded).
struct PresetEntry {
    std::string name;
    int ramKB;
    std::string basicType;
    std::vector<std::string> bools;   // ordered card-enable flags
    std::string codeTankRom;          // "" when the field is nullptr
};

// Ordered list of the `true`/`false` tokens in an entry — the card-enable bool
// vector. Layout placements carry no bools, so this is layout-independent.
std::vector<std::string> boolTokens(const std::string& entry)
{
    std::vector<std::string> out;
    for (std::size_t i = 0; i < entry.size();) {
        const bool isT = entry.compare(i, 4, "true") == 0;
        const bool isF = entry.compare(i, 5, "false") == 0;
        const std::size_t len = isT ? 4 : (isF ? 5 : 0);
        const bool boundaryL = i == 0 || !std::isalnum(static_cast<unsigned char>(entry[i - 1]));
        const bool boundaryR = i + len >= entry.size() ||
                               !std::isalnum(static_cast<unsigned char>(entry[i + len]));
        if (len && boundaryL && boundaryR) {
            out.push_back(isT ? "true" : "false");
            i += len;
        } else {
            ++i;
        }
    }
    return out;
}

// The CodeTank ROM path (the only "roms/..." literal in an entry), or "" when
// the codeTankRomPath field is nullptr.
std::string codeTankRom(const std::string& entry)
{
    const std::size_t begin = entry.find("\"roms/");
    if (begin == std::string::npos) return "";
    const std::size_t end = entry.find('"', begin + 1);
    if (end == std::string::npos) return "";
    return entry.substr(begin + 1, end - begin - 1);
}

// Ordered list of the integer arguments to every `presetItem(N)` call in the
// Presets menu source. The `auto presetItem = [&](int i)` lambda definition has
// a space before `=` (no `(` immediately after the name) so it is not matched.
std::vector<int> menuPresetIndices(const std::string& menu)
{
    std::vector<int> out;
    const std::string key = "presetItem(";
    std::size_t pos = 0;
    while ((pos = menu.find(key, pos)) != std::string::npos) {
        std::size_t j = pos + key.size();
        while (j < menu.size() && std::isspace(static_cast<unsigned char>(menu[j]))) ++j;
        if (j < menu.size() && std::isdigit(static_cast<unsigned char>(menu[j]))) {
            out.push_back(std::atoi(menu.c_str() + j));
        }
        pos = j;
    }
    return out;
}

} // namespace

int main(int argc, char** argv)
{
    // argv[1] = MainWindow_Presets.cpp (required);
    // argv[2] = README.md (optional — enables the preset-count lockstep check);
    // argv[3] = MainWindow_Menu.cpp (optional — enables the menu-completeness check).
    assert(argc >= 2);
    const std::string source = readFile(argv[1]);

    std::vector<std::string> failures;
    std::vector<PresetEntry> entries;   // one per kMachinePresets[] entry, in order
    int presets = 0;
    int excluded = 0;
    int checked = 0;
    int nonFantasy = 0;

    const std::size_t table = source.find("const MachineConfig kMachinePresets[]");
    assert(table != std::string::npos);
    const std::size_t arrayOpen = source.find('{', table);
    assert(arrayOpen != std::string::npos);

    int depth = 0;
    std::size_t entryStart = std::string::npos;
    for (std::size_t i = arrayOpen; i < source.size(); ++i) {
        if (source[i] == '{') {
            ++depth;
            if (depth == 2) entryStart = i;
            continue;
        }
        if (source[i] != '}') continue;
        if (depth == 2 && entryStart != std::string::npos) {
            const std::string entry = source.substr(entryStart, i - entryStart + 1);
            const std::size_t basic = entry.find("BasicType::");
            if (basic == std::string::npos) {
                --depth;
                continue;
            }

            ++presets;
            const std::string name = firstQuotedString(entry);
            const int ramKB = ramKbBeforeBasicType(entry);
            const std::string basicType = basicTypeToken(entry);
            entries.push_back({name, ramKB, basicType, boolTokens(entry), codeTankRom(entry)});
            const std::string lowerName = lowerAscii(name);
            const bool fantasy = lowerName.find("fantasy") != std::string::npos;
            if (!fantasy) {
                ++nonFantasy;
                if (basicType == "BasicType::Integer") {
                    failures.push_back(name + " still preloads Integer BASIC");
                }
            }
            if (fantasy || lowerName.find("bare apple-1") != std::string::npos) {
                ++excluded;
                --depth;
                continue;
            }
            ++checked;
            if (lowerName.find("gen2") != std::string::npos) {
                // Uncle Bernie's GEN2 release card is a documented RAM
                // expansion (doc/GEN2_RELEASE_questions.md Q9: 48 KB card
                // DRAM at $0000-$BFFF via write-through + the motherboard's
                // $E000-$EFFF bank — Bernie quotes 54 KB total). His real
                // machine therefore runs 48 KB, not the Parmigiani 8 KB
                // dual-bank.
                if (ramKB != 48) {
                    failures.push_back(name + " (GEN2) has " + std::to_string(ramKB)
                                       + " KB RAM, expected 48 (Bernie Q9)");
                }
            } else if (ramKB != 8) {
                failures.push_back(name + " has " + std::to_string(ramKB) + " KB RAM");
            }
        }
        --depth;
        if (depth == 0) break;
    }

    assert(presets >= 10);
    assert(excluded >= 2);
    assert(checked > 0);
    assert(nonFantasy > 0);

    // README lockstep: the Machine Presets table must have exactly one data row
    // per kMachinePresets[] entry (CLAUDE.md: "The README preset table must stay
    // in lockstep"). preset_ram_profiles already parses the source array; this
    // closes the doc-drift gap that the source-only parse left open.
    int readmeRows = -1;
    if (argc >= 3) {
        const std::string readme = readFile(argv[2]);
        readmeRows = readmePresetRowCount(readme);
        if (readmeRows < 0) {
            failures.push_back("README.md: Machine Presets table header not found");
        } else if (readmeRows != presets) {
            failures.push_back("README preset table has " + std::to_string(readmeRows)
                               + " rows but kMachinePresets[] has "
                               + std::to_string(presets) + " entries");
        }
    }

    // DevBench mirror guard: presets 0/1/2 (the CC65 / TMS9918 / GEN2 HGR
    // Development Benches) must mirror the machine config of the real presets
    // 4 / 9 / 11 (CLAUDE.md: "Each MIRRORS an existing preset's machine config").
    // name/description/layout legitimately differ; RAM, BASIC, the card bool
    // vector and the CodeTank ROM must not.
    const std::pair<int, int> mirrors[] = {{0, 4}, {1, 9}, {2, 11}};
    for (const auto& m : mirrors) {
        if (m.first >= static_cast<int>(entries.size()) ||
            m.second >= static_cast<int>(entries.size())) {
            failures.push_back("DevBench mirror pair (" + std::to_string(m.first) + ","
                               + std::to_string(m.second) + ") out of range");
            continue;
        }
        const PresetEntry& a = entries[m.first];
        const PresetEntry& b = entries[m.second];
        if (a.ramKB != b.ramKB || a.basicType != b.basicType ||
            a.bools != b.bools || a.codeTankRom != b.codeTankRom) {
            failures.push_back("DevBench preset " + std::to_string(m.first) + " (" + a.name
                               + ") no longer mirrors preset " + std::to_string(m.second)
                               + " (" + b.name + ")");
        }
    }

    // Menu-completeness guard: MainWindow_Menu.cpp hardcodes presetItem(N) — the
    // one preset site not driven by kMachinePresetCount — so a new preset would
    // be reachable via --preset yet invisible in the GUI. Require the menu to list
    // every index exactly once.
    if (argc >= 4) {
        const std::string menu = readFile(argv[3]);
        const std::vector<int> menuIdx = menuPresetIndices(menu);
        std::vector<bool> seen(entries.size(), false);
        for (int n : menuIdx) {
            if (n < 0 || n >= static_cast<int>(entries.size())) {
                failures.push_back("Presets menu references preset " + std::to_string(n)
                                   + " outside [0," + std::to_string(static_cast<int>(entries.size()) - 1) + "]");
            } else if (seen[static_cast<std::size_t>(n)]) {
                failures.push_back("Presets menu lists preset " + std::to_string(n) + " more than once");
            } else {
                seen[static_cast<std::size_t>(n)] = true;
            }
        }
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (!seen[i]) {
                failures.push_back("Presets menu is missing preset " + std::to_string(i)
                                   + " (" + entries[i].name + ")");
            }
        }
    }

    if (!failures.empty()) {
        std::cerr << "Preset realism invariant failed:\n";
        for (const auto& failure : failures) {
            std::cerr << "  - " << failure << "\n";
        }
        return 1;
    }

    std::cout << "Checked " << checked
              << " non-fantasy, non-bare presets; all are 8 KB dual-bank profiles. "
              << "Checked " << nonFantasy
              << " non-fantasy presets; none preload Integer BASIC.";
    if (readmeRows >= 0) {
        std::cout << " README preset table matches source (" << readmeRows
                  << " rows == " << presets << " entries).";
    }
    std::cout << " DevBench mirrors (0/1/2 == 4/9/11) hold.";
    if (argc >= 4) {
        std::cout << " Presets menu lists all " << presets << " presets.";
    }
    std::cout << "\n";
    return 0;
}
