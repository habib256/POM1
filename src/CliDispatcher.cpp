// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// CliDispatcher.cpp — parser + Phase-C runner. See CliDispatcher.h for the
// phase split and the verb → method map.

#include <algorithm>   // std::any_of (see runDeferredActions)
#include "CliDispatcher.h"

#include "EmulationController.h"
#include "HexDumpFile.h"
#include "Logger.h"
#include "MachinePresets.h"
#include "PomVersion.h"

#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace pom1 {
namespace {

constexpr int kMaxPasteChars = 4096;

std::string toLower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool endsWithIcase(const std::string& s, std::string_view suffix)
{
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[s.size() - suffix.size() + i]))
            != std::tolower(static_cast<unsigned char>(suffix[i]))) return false;
    }
    return true;
}

// Hex-or-decimal address parser. Accepts "0x0300", "$0300", "0300", "768".
// Returns false on anything unparseable or out of 16-bit range.
bool parseAddr16(const std::string& s, int& out)
{
    if (s.empty()) return false;
    std::string v = s;
    if (v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) {
        v.erase(0, 2);
    } else if (v[0] == '$') {
        v.erase(0, 1);
    } else {
        // Look for a decimal-only string (no hex letters) → treat as decimal.
        bool hasHexLetter = false;
        for (char c : v) if (std::isxdigit(static_cast<unsigned char>(c)) &&
                             !std::isdigit(static_cast<unsigned char>(c))) hasHexLetter = true;
        if (!hasHexLetter) {
            // Ambiguous: in the emulator/Woz world "0300" overwhelmingly means
            // hex. Stick with hex for 1-4 hex digits; anything longer is a
            // decimal literal the user typed by accident — reject it.
            if (v.size() > 4) {
                try {
                    size_t idx = 0;
                    long n = std::stol(v, &idx, 10);
                    if (idx != v.size() || n < 0 || n > 0xFFFF) return false;
                    out = static_cast<int>(n);
                    return true;
                } catch (...) { return false; }
            }
        }
    }
    if (v.empty() || v.size() > 4) return false;
    try {
        size_t idx = 0;
        long n = std::stol(v, &idx, 16);
        if (idx != v.size() || n < 0 || n > 0xFFFF) return false;
        out = static_cast<int>(n);
        return true;
    } catch (...) { return false; }
}

bool parseIntPositive(const std::string& s, int& out)
{
    try {
        size_t idx = 0;
        long n = std::stol(s, &idx, 10);
        if (idx != s.size() || n < 0 || n > INT_MAX) return false;  // avoid signed overflow on cast
        out = static_cast<int>(n);
        return true;
    } catch (...) { return false; }
}

// Parse an unsigned 64-bit count. Cycle counts for --paste-at-cycle routinely
// exceed INT_MAX (a few seconds of Apple-1 time is millions of cycles; minutes
// overflow int). Underscore digit separators are tolerated for readability
// (e.g. 7_600_000) and stripped before the decimal parse.
bool parseU64(const std::string& s, uint64_t& out)
{
    std::string digits;
    digits.reserve(s.size());
    for (char c : s) {
        if (c == '_') continue;
        if (c < '0' || c > '9') return false;
        digits.push_back(c);
    }
    if (digits.empty()) return false;
    try {
        size_t idx = 0;
        unsigned long long n = std::stoull(digits, &idx, 10);
        if (idx != digits.size()) return false;
        out = static_cast<uint64_t>(n);
        return true;
    } catch (...) { return false; }
}

bool parseTime(const std::string& s, std::time_t& out)
{
    std::tm tm{};
    std::istringstream iss(s);
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (iss.fail()) return false;
    tm.tm_isdst = -1;  // let libc decide from local DST rules
    std::time_t t = std::mktime(&tm);
    if (t == static_cast<std::time_t>(-1)) return false;
    out = t;
    return true;
}

struct CardNameEntry {
    std::string_view name;
    CliCard          card;
};

constexpr CardNameEntry kCardNames[] = {
    {"aci",          CliCard::Aci},
    {"sid",          CliCard::Sid},
    {"sid-se",       CliCard::SidSE},
    {"sidse",        CliCard::SidSE},
    {"microsd",      CliCard::MicroSD},
    {"sdcard",       CliCard::MicroSD},
    {"tms9918",      CliCard::Tms9918},
    {"tms",          CliCard::Tms9918},
    {"a1io-rtc",     CliCard::A1IoRtc},
    {"a1io",         CliCard::A1IoRtc},
    {"rtc",          CliCard::A1IoRtc},
    {"hgr",          CliCard::Hgr},
    {"gen2",         CliCard::Hgr},
    {"cffa1",        CliCard::Cffa1},
    {"cffa",         CliCard::Cffa1},
    {"krusader",     CliCard::Krusader},
    {"wifi",         CliCard::WifiModem},
    {"modem",        CliCard::WifiModem},
    {"terminal",     CliCard::TerminalCard},
    {"jukebox",      CliCard::JukeBox},
    {"codetank",     CliCard::CodeTank},
    {"pr40",         CliCard::Pr40},
    {"printer",      CliCard::Pr40},
    {"gt6144",       CliCard::GT6144},
    {"swtpc",        CliCard::GT6144},
    {"iec",          CliCard::IEC},
    {"xaci",         CliCard::ExtendedAci},
    {"extended-aci", CliCard::ExtendedAci},
    {"aci2",         CliCard::ExtendedAci},
};

constexpr std::string_view canonicalCardName(CardId id)
{
    switch (id) {
    case CardId::Aci: return "aci";
    case CardId::Tms9918: return "tms9918";
    case CardId::Sid: return "sid";
    case CardId::SidSpecialEdition: return "sid-se";
    case CardId::MicroSD: return "microsd";
    case CardId::Cffa1: return "cffa1";
    case CardId::JukeBox: return "jukebox";
    case CardId::CodeTank: return "codetank";
    case CardId::WifiModem: return "wifi";
    case CardId::TerminalCard: return "terminal";
    case CardId::A1IoRtc: return "a1io";
    case CardId::Pr40: return "pr40";
    case CardId::Gt6144: return "gt6144";
    case CardId::Iec: return "iec";
    case CardId::Gen2: return "hgr";
    case CardId::ExtendedAci: return "xaci";
    case CardId::Count:
    case CardId::Invalid: return {};
    }
    return {};
}

bool parseCard(const std::string& raw, CliCard& out)
{
    const std::string key = toLower(raw);
    for (const auto& e : kCardNames) {
        if (e.name == key) { out = e.card; return true; }
    }
    return false;
}

// Split "a,b,c" → ["a","b","c"]. Empty entries are ignored.
std::vector<std::string> splitCsv(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool addCardsFromCsv(const std::string& csv, bool enable, std::vector<CliCardOverride>& out)
{
    auto names = splitCsv(csv);
    if (names.empty()) return false;
    for (const auto& n : names) {
        CliCard c;
        if (!parseCard(n, c)) {
            pom1::log().error("CLI", "Unknown card name '" + n + "'. Valid: "
                "aci,xaci,sid,sid-se,microsd,tms9918,a1io-rtc,hgr,cffa1,krusader,wifi,"
                "terminal,jukebox,codetank,pr40,gt6144,iec");
            return false;
        }
        out.push_back({c, enable});
    }
    return true;
}

// Split once on ':' — used for "addr:path" and "host:guest".
bool splitOnColon(const std::string& s, std::string& left, std::string& right)
{
    auto pos = s.find(':');
    if (pos == std::string::npos) return false;
    left  = s.substr(0, pos);
    right = s.substr(pos + 1);
    return !left.empty() && !right.empty();
}

bool logAndFail(const char* msg)
{
    pom1::log().error("CLI", msg);
    return false;
}

} // namespace

std::string resolveSaveTapePath(const std::string& path, CliSaveTapeFormat hint)
{
    if (path.empty() || hint == CliSaveTapeFormat::NoHint) return path;
    if (endsWithIcase(path, ".aci") || endsWithIcase(path, ".wav")) return path;
    switch (hint) {
        case CliSaveTapeFormat::Aci: return path + ".aci";
        case CliSaveTapeFormat::Wav: return path + ".wav";
        default: return path;
    }
}

namespace {

// ---------------------------------------------------------------------------
// --help
//
// The unknown-flag error told the user to "Run with --help for the supported
// list" while no such flag existed — a dead end for exactly the person who
// needed the list. The usage text lives HERE and nowhere else, and ctest
// `cli_flags_sync` (tools/check_cli_flags.py) asserts three sets agree: what
// this table prints, what the parser below actually accepts (`arg == "--x"`),
// and what doc/CLI.md documents. A help table nobody checks would rot the same
// way that error message did.
//
// Phase letters match doc/CLI.md: A = read at boot before the window exists,
// B = machine configuration, C = deferred until the cards are plugged.
// ---------------------------------------------------------------------------
struct CliFlagHelp { char phase; const char* spelling; const char* help; };

constexpr CliFlagHelp kCliFlagHelp[] = {
    {'A', "--help / -h",                        "Print this list and exit."},
    {'A', "--list-presets",                     "Print the machine preset table and exit."},
    {'A', "--preset <N|name> / -p",             "Boot preset: index, or case-insensitive substring."},
    {'A', "--terminal",                         "Force-enable the Terminal Card (127.0.0.1:6502)."},
    {'A', "--telemetry-port <N>",               "Dev-only: telemetry side channel on 127.0.0.1:N."},
    {'A', "--telemetry-log <path>",             "Dev-only: tee the telemetry frame stream to a file."},
    {'A', "--dump-gen2-frame <path>",           "Headless: write the GEN2 HGR framebuffer to a PNG, then exit."},
    {'A', "--dump-tms-frame <path>",            "Headless: same for the TMS9918 framebuffer."},
    {'A', "--dump-after-cycles <N>",            "Deterministic settle before a --dump-*-frame capture."},
    {'A', "--dump-settle-ms <N>",               "Wall-clock settle before a --dump-*-frame capture (default 1000)."},
    {'A', "--exit-after-cycles <N>",            "Headless: run exactly N emulated cycles after the deferred verbs, then exit 0."},
    {'A', "--tape <path>",                      "Preload a cassette and auto-Play (.aci .wav .aiff .ogg .mp3 .flac)."},
    {'A', "--save-tape <path>",                 "Dump the deck on clean shutdown."},
    {'A', "--save-tape-format <aci|wav>",       "Format for --save-tape."},
    {'A', "--cpu-max",                          "Pin execution speed to 1 000 000 cycles/frame."},
    {'A', "--audio-latency <ms>",               "Output cushion, clamped to [20, 250]."},
    {'A', "--fullscreen",                       "Open fullscreen and re-assert it after every preset switch."},
    {'A', "--headless",                         "No window, no GL, no ImGui — for CI and scripted runs."},

    {'B', "--speed <cycles/frame>",             "Execution speed (1x = 17045, 2x = 34091)."},
    {'B', "--enable <card>[,...]",              "Plug cards: aci, xaci, sid, microsd, tms9918, hgr, cffa1, wifi..."},
    {'B', "--disable <card>[,...]",             "Unplug cards from the deferred plug list."},
    {'B', "--sid-chip <6581|8580>",             "Swap the SID chip model."},
    {'B', "--jukebox-jumper <ram16|ram32>",     "Juke-Box ROM window jumper."},
    {'B', "--jukebox-chip <flash|eeprom>",      "Juke-Box chip mode."},
    {'B', "--codetank-jumper <lower|upper>",    "Pick the 16 kB half of the 32 kB CodeTank 28c256."},
    {'B', "--codetank-rom <path>",              "Override the CodeTank ROM probe."},
    {'B', "--iec-disk <path>",                  "Mount a .d64 on the IEC daughterboard's drive 8."},
    {'B', "--silicon-strict",                   "Force TMS9918 paranoid VRAM timing on."},
    {'B', "--no-silicon-strict",                "Force TMS9918 paranoid VRAM timing off."},
    {'B', "--dram-refresh",                     "Force the Apple-1 DRAM-refresh CPU stall on."},
    {'B', "--no-dram-refresh",                  "Force the Apple-1 DRAM-refresh CPU stall off."},
    {'B', "--display-field-sync",               "Phase-lock $D012 busy to the 60 Hz video scan."},
    {'B', "--no-display-field-sync",            "Use the fixed $D012 busy countdown (default)."},
    {'B', "--vram-noise",                       "Power on TMS9918 VRAM with real noise instead of $FF/$00."},
    {'B', "--tms-frameflag-hostile",            "Model silicon whose status frame flag never registers."},
    {'B', "--ram-poison <HH>",                  "Fill system RAM with sentinel byte HH instead of $00."},
    {'B', "--ram-trap",                         "Log the first read of any RAM cell never written this run."},

    {'C', "--load <addr>:<path>",               "Load a program (extension-routed: .txt .hex .apl .mon .tur). Repeatable."},
    {'C', "--run <addr>",                       "Jump to <addr> once the machine is up."},
    {'C', "--paste <file>",                     "Push up to 4096 chars into the keyboard queue."},
    {'C', "--paste-at-cycle <N> \"<keys>\"",      "Headless: inject <keys> at cumulative cycle N."},
    {'C', "--step <N>",                         "Single-step N instructions."},
    {'C', "--trace-brk",                        "Dump a trace on BRK."},
    {'C', "--play",                             "Cassette transport: play."},
    {'C', "--rec",                              "Cassette transport: arm recording (no $C000 wait)."},
    {'C', "--rewind",                           "Cassette transport: rewind."},
    {'C', "--sd-mkdir <path>",                  "Create a directory on the microSD image."},
    {'C', "--sd-put <host>:<guest>",            "Copy a host file onto the microSD image."},
    {'C', "--sd-get <guest>:<host>",            "Copy a file off the microSD image."},
    {'C', "--rtc-freeze \"YYYY-MM-DD HH:MM:SS\"", "Set the A1-IO RTC offset."},
    {'C', "--snapshot-save <path>",             "Write the machine state to a .snap file."},
    {'C', "--snapshot-load <path>",             "Restore a .snap written by --snapshot-save."},
    {'C', "--break <addr>",                     "Arm a PC-matched halt before the instruction at <addr>."},
};

void printCliUsage()
{
    std::cout << "POM1 " POM1_VERSION_STRING " - Apple 1 emulator\n"
                 "Usage: POM1 [flags]\n\n"
                 "  Phase A = read at boot, before the window exists\n"
                 "  Phase B = machine configuration\n"
                 "  Phase C = replayed once the cards are plugged\n\n"
                 "Full reference, with the details this list cannot hold: doc/CLI.md\n";
    char phase = 0;
    for (const CliFlagHelp& f : kCliFlagHelp) {
        if (f.phase != phase) {
            phase = f.phase;
            std::cout << "\nPhase " << phase << ":\n";
        }
        std::cout << "  " << std::left << std::setw(38) << f.spelling
                  << ' ' << f.help << '\n';
    }
    std::cout << std::flush;
}

}  // namespace

std::optional<CliPlan> parseCli(int argc, char* argv[], bool& cleanExitOut)
{
    cleanExitOut = false;
    CliPlan plan;

    auto needArg = [&](int i, const char* flag) -> bool {
        if (i + 1 >= argc) {
            pom1::log().error("CLI", std::string(flag) + " expects a value");
            return false;
        }
        return true;
    };

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        // -----------------------------------------------------------------
        // Phase-A flags — picked up at boot before the window opens.
        // -----------------------------------------------------------------
        if (arg == "--help" || arg == "-h") {
            cleanExitOut = true;
            printCliUsage();
            return std::nullopt;
        }
        if (arg == "--list-presets") {
            cleanExitOut = true;
            int n = pom1::machinePresetCount();
            std::cout << "Available machine presets:" << std::endl;
            for (int p = 0; p < n; ++p) {
                std::cout << "  " << p << ": " << pom1::machinePresetName(p)
                          << "\t[cards=";
                const MachineConfig* preset = machinePreset(presetIdFromIndex(p));
                bool first = true;
                if (preset) {
                    // `card`, not `i`: the argv walk this is nested inside owns
                    // that name (MSVC C4456).
                    for (std::size_t card = 0; card < kCardCount; ++card) {
                        const CardId id = static_cast<CardId>(card);
                        if (!preset->cards.contains(id)) continue;
                        if (!first) std::cout << ',';
                        std::cout << canonicalCardName(id);
                        first = false;
                    }
                }
                std::cout << "]" << std::endl;
            }
            return std::nullopt;
        }
        if (arg == "--terminal") {
            plan.terminalOverride = true;
            continue;
        }
        if (arg == "--cpu-max") {
            plan.cpuMax = true;
            continue;
        }
        if (arg == "--headless") {
            plan.headless = true;
            continue;
        }
        if (arg == "--trace-brk") {
            CliAction a; a.kind = CliAction::Kind::TraceBrk;
            plan.deferredActions.push_back(std::move(a));
            continue;
        }
        if (arg == "--tape") {
            if (!needArg(i, "--tape")) return std::nullopt;
            plan.initialTapePath = argv[++i];
            plan.initialTapeAutoPlay = true;
            continue;
        }
        if (arg == "--save-tape") {
            if (!needArg(i, "--save-tape")) return std::nullopt;
            plan.saveTapePath = argv[++i];
            continue;
        }
        if (arg == "--save-tape-format") {
            if (!needArg(i, "--save-tape-format")) return std::nullopt;
            const std::string v = toLower(argv[++i]);
            if      (v == "aci") plan.saveTapeFormat = CliSaveTapeFormat::Aci;
            else if (v == "wav") plan.saveTapeFormat = CliSaveTapeFormat::Wav;
            else { logAndFail("--save-tape-format expects aci or wav"); return std::nullopt; }
            continue;
        }
        if (arg == "--speed") {
            if (!needArg(i, "--speed")) return std::nullopt;
            int cpf;
            if (!parseIntPositive(argv[++i], cpf) || cpf <= 0) {
                logAndFail("--speed expects a positive integer (cycles per frame)");
                return std::nullopt;
            }
            plan.executionSpeed = cpf;
            continue;
        }
        if (arg == "--audio-latency") {
            if (!needArg(i, "--audio-latency")) return std::nullopt;
            int ms = 0;
            if (!parseIntPositive(argv[++i], ms) || ms < 20 || ms > 250) {
                logAndFail("--audio-latency expects milliseconds in [20, 250]");
                return std::nullopt;
            }
            plan.audioLatencyMs = ms;
            continue;
        }
        if (arg == "--fullscreen") {
            plan.fullscreen = true;
            continue;
        }
        if (arg == "--telemetry-port") {
            if (!needArg(i, "--telemetry-port")) return std::nullopt;
            int port = 0;
            if (!parseIntPositive(argv[++i], port) || port < 1 || port > 65535) {
                logAndFail("--telemetry-port expects a TCP port 1-65535");
                return std::nullopt;
            }
            plan.telemetryPort = port;
            continue;
        }
        if (arg == "--telemetry-log") {
            if (!needArg(i, "--telemetry-log")) return std::nullopt;
            plan.telemetryLogPath = argv[++i];
            continue;
        }
        if (arg == "--dump-gen2-frame") {
            if (!needArg(i, "--dump-gen2-frame")) return std::nullopt;
            plan.dumpGen2Path = argv[++i];
            plan.headless = true;   // a frame capture is a headless one-shot
            continue;
        }
        if (arg == "--dump-tms-frame") {
            if (!needArg(i, "--dump-tms-frame")) return std::nullopt;
            plan.dumpTmsPath = argv[++i];
            plan.headless = true;
            continue;
        }
        if (arg == "--dump-settle-ms") {
            if (!needArg(i, "--dump-settle-ms")) return std::nullopt;
            int ms = 0;
            if (!parseIntPositive(argv[++i], ms)) {
                logAndFail("--dump-settle-ms expects a positive integer (milliseconds)");
                return std::nullopt;
            }
            plan.dumpSettleMs = ms;
            continue;
        }
        if (arg == "--dump-after-cycles") {
            if (!needArg(i, "--dump-after-cycles")) return std::nullopt;
            int n = 0;
            if (!parseIntPositive(argv[++i], n)) {
                logAndFail("--dump-after-cycles expects a positive integer (CPU cycles)");
                return std::nullopt;
            }
            plan.dumpAfterCycles = n;
            continue;
        }
        if (arg == "--exit-after-cycles") {
            if (!needArg(i, "--exit-after-cycles")) return std::nullopt;
            int n = 0;
            if (!parseIntPositive(argv[++i], n)) {
                logAndFail("--exit-after-cycles expects a positive integer (CPU cycles)");
                return std::nullopt;
            }
            plan.exitAfterCycles = n;
            plan.headless = true;   // a bounded run is a headless one-shot
            continue;
        }
        if (arg == "--preset" || arg == "-p") {
            if (!needArg(i, "--preset")) return std::nullopt;
            const std::string val = argv[++i];
            char* end = nullptr;
            long idx = std::strtol(val.c_str(), &end, 10);
            if (end != val.c_str() && *end == '\0') {
                int n = pom1::machinePresetCount();
                if (idx < 0 || idx >= n) {
                    pom1::log().error("CLI", "Preset index " + val + " out of range [0, " +
                                              std::to_string(n - 1) +
                                              "]. Use --list-presets to see available presets.");
                    return std::nullopt;
                }
                plan.presetIndex = static_cast<int>(idx);
            } else {
                int match = -1;
                int n = pom1::machinePresetCount();
                std::string vLow = toLower(val);
                for (int p = 0; p < n; ++p) {
                    std::string pLow = toLower(pom1::machinePresetName(p));
                    if (pLow.find(vLow) != std::string::npos) { match = p; break; }
                }
                if (match < 0) {
                    pom1::log().error("CLI", "Unknown preset '" + val +
                                              "'. Use --list-presets to see available presets.");
                    return std::nullopt;
                }
                plan.presetIndex = match;
            }
            pom1::log().info("CLI", std::string("Preset: ") +
                              pom1::machinePresetName(plan.presetIndex));
            continue;
        }
        if (arg == "--enable") {
            if (!needArg(i, "--enable")) return std::nullopt;
            if (!addCardsFromCsv(argv[++i], /*enable=*/true, plan.cardOverrides)) return std::nullopt;
            continue;
        }
        if (arg == "--disable") {
            if (!needArg(i, "--disable")) return std::nullopt;
            if (!addCardsFromCsv(argv[++i], /*enable=*/false, plan.cardOverrides)) return std::nullopt;
            continue;
        }
        if (arg == "--silicon-strict") {
            plan.siliconStrictModeOverride = true;
            continue;
        }
        if (arg == "--no-silicon-strict") {
            plan.siliconStrictModeOverride = false;
            continue;
        }
        if (arg == "--dram-refresh") {
            plan.dramRefreshOverride = true;
            continue;
        }
        if (arg == "--no-dram-refresh") {
            plan.dramRefreshOverride = false;
            continue;
        }
        if (arg == "--display-field-sync") {
            plan.displayFieldSyncOverride = true;
            continue;
        }
        if (arg == "--no-display-field-sync") {
            plan.displayFieldSyncOverride = false;
            continue;
        }
        if (arg == "--vram-noise") {
            plan.vramNoiseOnReset = true;
            continue;
        }
        if (arg == "--tms-frameflag-hostile") {
            plan.tmsFrameFlagHostile = true;
            continue;
        }
        if (arg == "--ram-poison") {
            if (!needArg(i, "--ram-poison")) return std::nullopt;
            plan.ramPoisonByte =
                static_cast<uint8_t>(std::strtoul(argv[++i], nullptr, 16) & 0xFF);
            continue;
        }
        if (arg == "--ram-trap") {
            plan.ramWriteTrap = true;
            continue;
        }
        if (arg == "--sid-chip") {
            if (!needArg(i, "--sid-chip")) return std::nullopt;
            const std::string v = argv[++i];
            if      (v == "6581") plan.sidChipOverride = SID::ChipModel::MOS6581;
            else if (v == "8580") plan.sidChipOverride = SID::ChipModel::MOS8580;
            else { logAndFail("--sid-chip expects 6581 or 8580"); return std::nullopt; }
            continue;
        }
        if (arg == "--jukebox-jumper") {
            if (!needArg(i, "--jukebox-jumper")) return std::nullopt;
            const std::string v = toLower(argv[++i]);
            if      (v == "ram16" || v == "ram16-rom32") plan.jukeBoxJumperOverride = JukeBox::Jumper::RAM16_ROM32;
            else if (v == "ram32" || v == "ram32-rom16") plan.jukeBoxJumperOverride = JukeBox::Jumper::RAM32_ROM16;
            else { logAndFail("--jukebox-jumper expects ram16 or ram32"); return std::nullopt; }
            continue;
        }
        if (arg == "--jukebox-chip") {
            if (!needArg(i, "--jukebox-chip")) return std::nullopt;
            const std::string v = toLower(argv[++i]);
            if      (v == "flash") plan.jukeBoxChipModeOverride = JukeBox::ChipMode::Flash;
            else if (v == "eeprom" || v == "28c256") plan.jukeBoxChipModeOverride = JukeBox::ChipMode::EEPROM28C256;
            else { logAndFail("--jukebox-chip expects flash or eeprom"); return std::nullopt; }
            continue;
        }
        if (arg == "--codetank-jumper") {
            if (!needArg(i, "--codetank-jumper")) return std::nullopt;
            const std::string v = toLower(argv[++i]);
            if      (v == "lower" || v == "lower16") plan.codeTankJumperOverride = CodeTank::Jumper::Lower16;
            else if (v == "upper" || v == "upper16") plan.codeTankJumperOverride = CodeTank::Jumper::Upper16;
            else { logAndFail("--codetank-jumper expects lower or upper"); return std::nullopt; }
            continue;
        }
        if (arg == "--iec-disk") {
            if (!needArg(i, "--iec-disk")) return std::nullopt;
            plan.iecDiskPath = argv[++i];
            continue;
        }
        if (arg == "--codetank-rom") {
            if (!needArg(i, "--codetank-rom")) return std::nullopt;
            plan.codeTankRomPath = argv[++i];
            continue;
        }

        // -----------------------------------------------------------------
        // Phase-C flags — queued, run after the card deferred-plug fires.
        // -----------------------------------------------------------------
        if (arg == "--load") {
            if (!needArg(i, "--load")) return std::nullopt;
            std::string addrS, pathS;
            if (!splitOnColon(argv[++i], addrS, pathS)) {
                logAndFail("--load expects addr:path (e.g. 0300:prog.bin)");
                return std::nullopt;
            }
            int addr;
            if (!parseAddr16(addrS, addr)) {
                logAndFail("--load: addr must be a 16-bit hex address");
                return std::nullopt;
            }
            CliAction a; a.kind = CliAction::Kind::Load; a.addressI = addr; a.pathS = pathS;
            plan.deferredActions.push_back(std::move(a));
            continue;
        }
        if (arg == "--run") {
            if (!needArg(i, "--run")) return std::nullopt;
            int addr;
            if (!parseAddr16(argv[++i], addr)) {
                logAndFail("--run: addr must be a 16-bit hex address");
                return std::nullopt;
            }
            CliAction a; a.kind = CliAction::Kind::Run; a.addressI = addr;
            plan.deferredActions.push_back(std::move(a));
            continue;
        }
        if (arg == "--paste") {
            if (!needArg(i, "--paste")) return std::nullopt;
            CliAction a; a.kind = CliAction::Kind::Paste; a.pathS = argv[++i];
            plan.deferredActions.push_back(std::move(a));
            continue;
        }
        if (arg == "--paste-at-cycle") {
            // --paste-at-cycle N "keys" : inject literal keystrokes when the
            // emulated CPU reaches cycle N (headless only — deterministic A/B).
            if (!needArg(i, "--paste-at-cycle")) return std::nullopt;
            uint64_t cyc;
            if (!parseU64(argv[++i], cyc)) {
                logAndFail("--paste-at-cycle expects a cycle count then a key string "
                           "(e.g. --paste-at-cycle 5000000 \"1\")");
                return std::nullopt;
            }
            if (!needArg(i, "--paste-at-cycle")) return std::nullopt;
            plan.timedPastes.push_back(CliTimedPaste{cyc, argv[++i]});
            continue;
        }
        if (arg == "--step") {
            if (!needArg(i, "--step")) return std::nullopt;
            int n;
            if (!parseIntPositive(argv[++i], n) || n <= 0) {
                logAndFail("--step expects a positive integer");
                return std::nullopt;
            }
            CliAction a; a.kind = CliAction::Kind::Step; a.countI = n;
            plan.deferredActions.push_back(std::move(a));
            continue;
        }
        if (arg == "--break") {
            if (!needArg(i, "--break")) return std::nullopt;
            int addr;
            if (!parseAddr16(argv[++i], addr)) {
                logAndFail("--break: addr must be a 16-bit hex address");
                return std::nullopt;
            }
            CliAction a; a.kind = CliAction::Kind::Break; a.addressI = addr;
            plan.deferredActions.push_back(std::move(a));
            continue;
        }
        if (arg == "--play") {
            CliAction a; a.kind = CliAction::Kind::Play;
            plan.deferredActions.push_back(std::move(a));
            continue;
        }
        if (arg == "--rec") {
            CliAction a; a.kind = CliAction::Kind::Rec;
            plan.deferredActions.push_back(std::move(a));
            continue;
        }
        if (arg == "--rewind") {
            CliAction a; a.kind = CliAction::Kind::Rewind;
            plan.deferredActions.push_back(std::move(a));
            continue;
        }
        if (arg == "--sd-mkdir") {
            if (!needArg(i, "--sd-mkdir")) return std::nullopt;
            CliAction a; a.kind = CliAction::Kind::SdMkdir; a.pathS = argv[++i];
            plan.deferredActions.push_back(std::move(a));
            continue;
        }
        if (arg == "--sd-put") {
            if (!needArg(i, "--sd-put")) return std::nullopt;
            std::string hostS, guestS;
            if (!splitOnColon(argv[++i], hostS, guestS)) {
                logAndFail("--sd-put expects host:guest paths");
                return std::nullopt;
            }
            CliAction a; a.kind = CliAction::Kind::SdPut; a.pathS = hostS; a.pathS2 = guestS;
            plan.deferredActions.push_back(std::move(a));
            continue;
        }
        if (arg == "--sd-get") {
            if (!needArg(i, "--sd-get")) return std::nullopt;
            std::string guestS, hostS;
            if (!splitOnColon(argv[++i], guestS, hostS)) {
                logAndFail("--sd-get expects guest:host paths");
                return std::nullopt;
            }
            CliAction a; a.kind = CliAction::Kind::SdGet; a.pathS = guestS; a.pathS2 = hostS;
            plan.deferredActions.push_back(std::move(a));
            continue;
        }
        if (arg == "--rtc-freeze") {
            if (!needArg(i, "--rtc-freeze")) return std::nullopt;
            std::time_t t;
            if (!parseTime(argv[++i], t)) {
                logAndFail("--rtc-freeze expects \"YYYY-MM-DD HH:MM:SS\"");
                return std::nullopt;
            }
            CliAction a; a.kind = CliAction::Kind::RtcFreeze; a.timeT = t;
            plan.deferredActions.push_back(std::move(a));
            continue;
        }
        if (arg == "--snapshot-save") {
            if (!needArg(i, "--snapshot-save")) return std::nullopt;
            CliAction a; a.kind = CliAction::Kind::SnapshotSave; a.pathS = argv[++i];
            plan.deferredActions.push_back(std::move(a));
            continue;
        }
        if (arg == "--snapshot-load") {
            if (!needArg(i, "--snapshot-load")) return std::nullopt;
            CliAction a; a.kind = CliAction::Kind::SnapshotLoad; a.pathS = argv[++i];
            plan.deferredActions.push_back(std::move(a));
            continue;
        }

        pom1::log().error("CLI", "Unknown flag '" + arg +
                          "'. Run with --help for the supported list.");
        return std::nullopt;
    }

    return plan;
}

namespace {

void runLoad(const CliAction& a, EmulationController& emu, bool startCpu)
{
    std::string err;
    int bytes = 0;
    // Extension-aware routing: Wozmon-hex extensions (.txt/.hex/.apl/.mon, see
    // HexDumpFile.h) go through the hex parser so multi-zone dumps (e.g.
    // games_chess Chess.txt = $0280 + $E000 blocks) load both zones from a
    // single CLI invocation. The addr argument is informational for hex dumps —
    // the file's own address prefixes win. Everything else is a raw binary; an
    // unrecognised hex extension used to land here and write the file's ASCII
    // text straight into RAM without an error, so keep this list central.
    if (pom1::isHexDumpPath(a.pathS)) {
        uint16_t startAddr = 0;
        if (!emu.loadHexDump(a.pathS, startAddr, err, &bytes, nullptr, startCpu)) {
            pom1::log().error("CLI", "--load " + a.pathS + ": " + err);
            return;
        }
        std::ostringstream ss;
        ss << "--load " << a.pathS << " (hex) → run $" << std::hex << std::uppercase
           << std::setw(4) << std::setfill('0') << startAddr
           << " (" << std::dec << bytes << " bytes across multiple zones)";
        pom1::log().info("CLI", ss.str());
        return;
    }
    if (!emu.loadBinary(a.pathS, static_cast<uint16_t>(a.addressI), err, &bytes,
                        startCpu)) {
        pom1::log().error("CLI", "--load " + a.pathS + ": " + err);
        return;
    }
    std::ostringstream ss;
    ss << "--load " << a.pathS << " → $" << std::hex << std::uppercase << std::setw(4)
       << std::setfill('0') << a.addressI << " (" << std::dec << bytes << " bytes)";
    pom1::log().info("CLI", ss.str());
}

void runPaste(const CliAction& a, EmulationController& emu)
{
    std::ifstream f(a.pathS, std::ios::binary);
    if (!f) {
        pom1::log().error("CLI", "--paste: cannot open '" + a.pathS + "'");
        return;
    }
    std::ostringstream buf;
    buf << f.rdbuf();
    const std::string content = buf.str();
    int sent = queueKeystrokes(emu, content, kMaxPasteChars);
    std::ostringstream ss;
    ss << "--paste " << a.pathS << ": sent " << sent << " chars";
    if (sent == kMaxPasteChars) ss << " (capped at " << kMaxPasteChars << ")";
    pom1::log().info("CLI", ss.str());
}

bool ensureSdRoot(EmulationController& emu, std::filesystem::path& rootOut)
{
    std::string root = emu.getMicroSDRootPath();
    if (root.empty()) {
        pom1::log().error("CLI",
            "microSD host path is not set (no sdcard/ tree was probed). "
            "Create a sdcard/ directory next to the POM1 binary or run "
            "from the repo root.");
        return false;
    }
    rootOut = std::filesystem::path(root);
    return true;
}

// Reject any guest-side path that escapes the sdcard root (e.g. "../etc/pw").
bool resolveGuestPath(const std::filesystem::path& root,
                      const std::string&           guest,
                      std::filesystem::path&       outFull)
{
    std::filesystem::path p = root / guest;
    std::error_code ec;
    std::filesystem::path lex = p.lexically_normal();
    auto rel = std::filesystem::relative(lex, root, ec);
    if (ec || rel.empty() || rel.native()[0] == '.' /* ../… or ./… */) {
        const auto dotdot = std::filesystem::path("..").native();
        if (!rel.empty() && rel.native().rfind(dotdot, 0) == 0) {
            pom1::log().error("CLI", "microSD path '" + guest + "' escapes the sdcard root");
            return false;
        }
    }
    outFull = lex;
    return true;
}

void runSdMkdir(const CliAction& a, EmulationController& emu)
{
    std::filesystem::path root;
    if (!ensureSdRoot(emu, root)) return;
    std::filesystem::path target;
    if (!resolveGuestPath(root, a.pathS, target)) return;
    std::error_code ec;
    std::filesystem::create_directories(target, ec);
    if (ec) {
        pom1::log().error("CLI", "--sd-mkdir '" + a.pathS + "': " + ec.message());
    } else {
        pom1::log().info("CLI", "--sd-mkdir " + a.pathS);
    }
}

void runSdPut(const CliAction& a, EmulationController& emu)
{
    std::filesystem::path root;
    if (!ensureSdRoot(emu, root)) return;
    std::filesystem::path target;
    if (!resolveGuestPath(root, a.pathS2, target)) return;
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    std::filesystem::copy_file(a.pathS, target,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        pom1::log().error("CLI", "--sd-put " + a.pathS + " -> " + a.pathS2 + ": " + ec.message());
    } else {
        pom1::log().info("CLI", "--sd-put " + a.pathS + " -> " + a.pathS2);
    }
}

void runSdGet(const CliAction& a, EmulationController& emu)
{
    std::filesystem::path root;
    if (!ensureSdRoot(emu, root)) return;
    std::filesystem::path source;
    if (!resolveGuestPath(root, a.pathS, source)) return;
    std::error_code ec;
    std::filesystem::path hostTarget(a.pathS2);
    if (hostTarget.has_parent_path()) {
        std::filesystem::create_directories(hostTarget.parent_path(), ec);
    }
    std::filesystem::copy_file(source, hostTarget,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        pom1::log().error("CLI", "--sd-get " + a.pathS + " -> " + a.pathS2 + ": " + ec.message());
    } else {
        pom1::log().info("CLI", "--sd-get " + a.pathS + " -> " + a.pathS2);
    }
}

} // namespace

int queueKeystrokes(EmulationController& emu, std::string_view text, int maxChars)
{
    int sent = 0;
    for (char c : text) {
        if (sent >= maxChars) break;
        if (c == '\n') c = '\r';                          // newline → Apple-1 CR
        if (c == '\r' || c == 0x1B ||                     // ESC passes: it is the
            (static_cast<unsigned char>(c) >= 32 &&       // canonical exit key of
             static_cast<unsigned char>(c) <= 126)) {     // every TMS9918 program —
            emu.queueKey(c);                              // headless runs must be
            ++sent;                                       // able to test exit paths
        }
    }
    return sent;
}

void runDeferredActions(const std::vector<CliAction>& actions,
                        EmulationController&          emu)
{
    // Does this plan single-step? Then --run must NEVER start the machine:
    // the WALL-CLOCK gap between jumpTo() and a later stopCpu()/stepCpu()
    // lets the program execute an unbounded number of instructions.
    // On an idle desktop the gap is microseconds and nothing shows; under a
    // sanitizer or a loaded machine it is milliseconds, i.e. thousands of
    // instructions, and "--run X --step N" stops meaning "execute exactly N
    // instructions from X". That is what made three TMS9918 micro-tests fail
    // intermittently under TSan while passing everywhere else — a real
    // determinism bug in the headless harness, not a race in the emulator.
    const bool planSteps =
        std::any_of(actions.begin(), actions.end(), [](const CliAction& x) {
            return x.kind == CliAction::Kind::Step;
        });

    for (const auto& a : actions) {
        switch (a.kind) {
            case CliAction::Kind::Load:      runLoad(a, emu, !planSteps); break;
            case CliAction::Kind::Run:       if (planSteps)
                                                 emu.jumpToPaused(static_cast<uint16_t>(a.addressI));
                                             else
                                                 emu.jumpTo(static_cast<uint16_t>(a.addressI));
                                             pom1::log().info("CLI",
                                                 "--run $" + std::to_string(a.addressI));
                                             break;
            case CliAction::Kind::Paste:     runPaste(a, emu);   break;
            case CliAction::Kind::Step:      for (int k = 0; k < a.countI; ++k) emu.stepCpu();
                                             pom1::log().info("CLI",
                                                 "--step " + std::to_string(a.countI));
                                             break;
            case CliAction::Kind::TraceBrk:  emu.setCpuBrkTraceEnabled(true);
                                             pom1::log().info("CLI", "--trace-brk enabled");
                                             break;
            case CliAction::Kind::Play:      emu.playTape();
                                             pom1::log().info("CLI", "--play");
                                             break;
            case CliAction::Kind::Rec:       emu.armCassetteRecord();
                                             pom1::log().info("CLI", "--rec armed");
                                             break;
            case CliAction::Kind::Rewind:    emu.rewindTape();
                                             pom1::log().info("CLI", "--rewind");
                                             break;
            case CliAction::Kind::SdMkdir:   runSdMkdir(a, emu); break;
            case CliAction::Kind::SdPut:     runSdPut(a, emu);   break;
            case CliAction::Kind::SdGet:     runSdGet(a, emu);   break;
            case CliAction::Kind::RtcFreeze: emu.setRtcOverrideTime(a.timeT);
                                             pom1::log().info("CLI", "--rtc-freeze applied");
                                             break;
            case CliAction::Kind::SnapshotSave: {
                std::string err;
                if (emu.saveSnapshot(a.pathS, err))
                    pom1::log().info("CLI", "--snapshot-save -> " + a.pathS);
                else
                    pom1::log().error("CLI", "--snapshot-save '" + a.pathS + "': " + err);
                break;
            }
            case CliAction::Kind::SnapshotLoad: {
                std::string err;
                if (emu.loadSnapshot(a.pathS, err))
                    pom1::log().info("CLI", "--snapshot-load <- " + a.pathS);
                else
                    pom1::log().error("CLI", "--snapshot-load '" + a.pathS + "': " + err);
                break;
            }
            case CliAction::Kind::Break: {
                emu.setCpuBreakpoint(static_cast<uint16_t>(a.addressI));
                std::ostringstream ss;
                ss << "--break $" << std::hex << std::uppercase
                   << std::setfill('0') << std::setw(4) << a.addressI
                   << " armed";
                pom1::log().info("CLI", ss.str());
                break;
            }
        }
    }
}

} // namespace pom1
