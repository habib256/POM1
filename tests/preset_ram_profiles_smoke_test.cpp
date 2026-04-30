#include <cassert>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
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

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string source = readFile(argv[1]);

    std::vector<std::string> failures;
    int presets = 0;
    int excluded = 0;
    int checked = 0;

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
            const std::string lowerName = lowerAscii(name);
            if (lowerName.find("fantasy") != std::string::npos ||
                lowerName.find("bare apple-1") != std::string::npos) {
                ++excluded;
                --depth;
                continue;
            }
            ++checked;
            if (ramKB != 8) {
                failures.push_back(name + " has " + std::to_string(ramKB) + " KB RAM");
            }
        }
        --depth;
        if (depth == 0) break;
    }

    assert(presets >= 10);
    assert(excluded >= 2);
    assert(checked > 0);

    if (!failures.empty()) {
        std::cerr << "Non-fantasy, non-bare presets must use standard 8 KB dual-bank RAM:\n";
        for (const auto& failure : failures) {
            std::cerr << "  - " << failure << "\n";
        }
        return 1;
    }

    std::cout << "Checked " << checked
              << " non-fantasy, non-bare presets; all are 8 KB dual-bank profiles.\n";
    return 0;
}
