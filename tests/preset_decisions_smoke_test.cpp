// What applying a preset MEANS — pom1::presets (src/PresetDecisions.h).
//
// Fourth seam of the family (Apple1KeyMap / FullscreenExpand / WindowGeometry /
// StagedCardConfiguration / LayoutDecisions). `MachinePresets.h` already owned
// the DATA and `CardTopology.h` the card POLICY; what stayed inside
// applyMachineConfig() was the composition — and three of its rules were each
// stated TWICE, in two files, under a comment asking the reader to keep the
// copies in sync. A rule stated twice is a rule that can disagree with itself.
//
// This test links the REAL preset table, so every assertion below is made about
// the thirteen machines POM1 actually ships rather than about fixtures:
//
//   §1  the silicon-fidelity bundle is all-or-nothing, and Fantasy is the only
//       preset that disarms it;
//   §2  the preset path and the Silicon Strict master button compose the SAME
//       bundle — the duplication that carried a "keep them in sync" comment;
//   §3  the strict RAM ceiling;
//   §4  the ROM profile for all thirteen presets, and the one predicate that
//       decides where Applesoft Lite lives;
//   §5  the ROM INVENTORY agrees with that profile — they used to be two copies
//       of the predicate, i.e. a Memory Map that can describe a machine other
//       than the one running;
//   §6  which preset preloads which tape, and which of them is DATA;
//   §7  the DevBench / boot-animation / banner predicates;
//   §8  the cold-reset rule, including the silicon exception on first apply.

#include "PresetDecisions.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace pom1::presets;
using pom1::CardId;
using pom1::PresetId;
using pom1::kMachinePresetCount;
using pom1::kMachinePresets;
using pom1::isFantasyPreset;
using pom1::presetIdFromIndex;
using pom1::BasicType;

namespace {

bool inventoryHas(const std::vector<RomSpan>& roms, const char* name)
{
    for (const RomSpan& r : roms)
        if (std::strcmp(r.name, name) == 0) return true;
    return false;
}

} // namespace

int main()
{
    // The table is the subject; a change to its size must not silently shrink
    // the coverage of everything below.
    assert(kMachinePresetCount == 13);

    // -----------------------------------------------------------------
    // §1 The silicon-fidelity bundle is all-or-nothing.
    //
    // Twelve flags used to be armed by twelve `= !fantasyPreset;` lines. The
    // failure mode of a hand-written list is a flag that stops being in it —
    // silently, since every knob has a plausible default.
    // -----------------------------------------------------------------
    {
        int fantasyCount = 0;
        for (int i = 0; i < kMachinePresetCount; ++i) {
            const PresetId id = presetIdFromIndex(i);
            const SiliconFidelity f = siliconFidelityForPreset(id);
            const bool armed = !isFantasyPreset(id);
            if (!armed) ++fantasyCount;
            assert(f.siliconStrict          == armed);
            assert(f.outOfRangeStrict       == armed);
            assert(f.dramRefresh            == armed);
            assert(f.vramNoiseOnReset       == armed);
            assert(f.systemRamNoiseOnReset  == armed);
            assert(f.cpuDecimalBugNMOS      == armed);
            assert(f.gen2RandomPowerOn      == armed);
            // The four GEN2 power-on knobs ARE what gen2RandomPowerOn means;
            // the Inspector's individual checkboxes only mirror them.
            assert(f.gen2RandomLatch        == f.gen2RandomPowerOn);
            assert(f.gen2RandomFloatingBus  == f.gen2RandomPowerOn);
            assert(f.gen2RandomScannerPhase == f.gen2RandomPowerOn);
            assert(f.gen2RandomDramNoise    == f.gen2RandomPowerOn);
        }
        // The two multiplexing profiles — P-LAB Fantasy and POM1 Fantasy — are
        // the only machines that are not buildable, and so the only ones that
        // disarm the bundle. Everything else models silicon that existed.
        assert(fantasyCount == 2 &&
               "exactly the two multiplexing profiles disarm silicon fidelity");
        assert(isFantasyPreset(PresetId::PLabFantasy));
        assert(isFantasyPreset(PresetId::Pom1Fantasy));
        assert(isFantasyPreset(pom1::kDefaultPresetId) &&
               "and the shipped default is one of them");
    }

    // -----------------------------------------------------------------
    // §2 One bundle, two callers.
    //
    // The master button composes siliconFidelity(turnOn); the preset path
    // composes siliconFidelityForPreset(id). If those ever diverge, a machine
    // toggled to Strict by hand is not the machine a Strict preset gives you.
    // -----------------------------------------------------------------
    {
        for (int i = 0; i < kMachinePresetCount; ++i) {
            const PresetId id = presetIdFromIndex(i);
            const SiliconFidelity byPreset = siliconFidelityForPreset(id);
            const SiliconFidelity byMaster = siliconFidelity(!isFantasyPreset(id));
            assert(std::memcmp(&byPreset, &byMaster, sizeof(SiliconFidelity)) == 0);
        }
        // And the master switch is genuinely a switch.
        assert(siliconFidelity(true).siliconStrict);
        assert(!siliconFidelity(false).siliconStrict);
        assert(!siliconFidelity(false).gen2RandomDramNoise);
    }

    // -----------------------------------------------------------------
    // §3 The strict RAM ceiling: a real Apple-1 has 8 KB of dual-bank RAM.
    // -----------------------------------------------------------------
    {
        assert(strictRamKB(true) == 8);
        assert(strictRamKB(false) == 64);
    }

    // -----------------------------------------------------------------
    // §4 The ROM profile, over the shipped table.
    // -----------------------------------------------------------------
    {
        int applesoftSd = 0, applesoftCffa1 = 0, integer = 0, monitorOnly = 0;
        for (int i = 0; i < kMachinePresetCount; ++i) {
            const PresetId id = presetIdFromIndex(i);
            const pom1::MachineConfig& cfg = kMachinePresets[i];
            const bool fantasy = isFantasyPreset(id);
            const SystemRomProfile p = romProfileFor(cfg, fantasy);

            // Nothing ever asks the controller to "preserve" from a preset: a
            // preset REPLACES the machine, ROMs included.
            assert(p != SystemRomProfile::Preserve);

            switch (cfg.basicType) {
            case BasicType::ApplesoftLite:
                if (applesoftOnSdCard(cfg)) {
                    ++applesoftSd;
                    assert(p == (fantasy ? SystemRomProfile::ApplesoftSdFantasy
                                         : SystemRomProfile::ApplesoftSd));
                } else {
                    ++applesoftCffa1;
                    assert(p == SystemRomProfile::ApplesoftCffa1);
                }
                break;
            case BasicType::Integer:
                ++integer;
                assert(p == SystemRomProfile::IntegerBasic);
                break;
            default:
                ++monitorOnly;
                assert(p == SystemRomProfile::MonitorOnly);
                break;
            }
        }
        // The shipped table exercises every branch — otherwise the assertions
        // above are vacuous for the ones it misses.
        assert(applesoftSd > 0 && applesoftCffa1 > 0);
        assert(integer > 0 && monitorOnly > 0);
    }
    {
        // The predicate itself: the CFFA1 carries Applesoft when present, even
        // alongside the microSD card. That "even alongside" is the whole reason
        // it is a predicate and not a card test.
        for (int i = 0; i < kMachinePresetCount; ++i) {
            const pom1::MachineConfig& cfg = kMachinePresets[i];
            if (cfg.hasCard(CardId::Cffa1))
                assert(!applesoftOnSdCard(cfg));
            if (applesoftOnSdCard(cfg))
                assert(cfg.hasCard(CardId::MicroSD));
        }
    }

    // -----------------------------------------------------------------
    // §5 The inventory agrees with the profile.
    //
    // THE drift pin. These were two copies of applesoftOnSdCard(): the
    // controller loaded from one and the Memory Map described the other.
    // -----------------------------------------------------------------
    {
        for (int i = 0; i < kMachinePresetCount; ++i) {
            const PresetId id = presetIdFromIndex(i);
            const pom1::MachineConfig& cfg = kMachinePresets[i];
            const bool fantasy = isFantasyPreset(id);
            const SystemRomProfile p = romProfileFor(cfg, fantasy);
            const std::vector<RomSpan> roms = romInventoryFor(cfg, fantasy);

            assert(!roms.empty());
            for (const RomSpan& r : roms) {
                assert(r.name && r.name[0]);
                assert(r.start <= r.end);
            }

            switch (p) {
            case SystemRomProfile::ApplesoftSd:
            case SystemRomProfile::ApplesoftSdFantasy:
                assert(inventoryHas(roms, "Applesoft Lite (loaded in card RAM)"));
                assert(inventoryHas(roms, "Woz Monitor"));
                // The Fantasy machine keeps Integer BASIC in its socket too;
                // the strict one has a single socket and cannot.
                assert(inventoryHas(roms, "Integer BASIC") == fantasy);
                break;
            case SystemRomProfile::ApplesoftCffa1:
                assert(inventoryHas(roms, "Applesoft Lite (CFFA1)"));
                // It IS the $E000-$FFFF image: no separate Woz Monitor row.
                assert(!inventoryHas(roms, "Woz Monitor"));
                break;
            case SystemRomProfile::IntegerBasic:
                assert(inventoryHas(roms, "Integer BASIC"));
                assert(inventoryHas(roms, "Woz Monitor"));
                break;
            case SystemRomProfile::MonitorOnly:
                assert(inventoryHas(roms, "Woz Monitor"));
                assert(!inventoryHas(roms, "Integer BASIC"));
                assert(!inventoryHas(roms, "Applesoft Lite (loaded in card RAM)"));
                break;
            case SystemRomProfile::Preserve:
                assert(false && "unreachable for a preset");
                break;
            }
            // The two payloads that ride alongside, whatever the profile.
            assert(inventoryHas(roms, "Krusader") == cfg.krusader);
            assert(inventoryHas(roms, "CFFA1 Firmware") == cfg.hasCard(CardId::Cffa1));
        }
    }

    // -----------------------------------------------------------------
    // §6 Which preset preloads which tape.
    // -----------------------------------------------------------------
    {
        int withTape = 0, programMode = 0, deckAudio = 0;
        for (int i = 0; i < kMachinePresetCount; ++i) {
            const PresetId id = presetIdFromIndex(i);
            const pom1::MachineConfig& cfg = kMachinePresets[i];
            const PresetTape t = tapeFor(cfg, id);
            if (t.candidates.empty()) {
                // Nothing wanted, so nothing to warn about.
                assert(!t.forceProgramMode && !t.autoPlay);
                assert(t.missingWarning == nullptr);
                continue;
            }
            ++withTape;
            // A tape that can be missing must say so — this is the only place
            // the user learns an asset did not ship.
            assert(t.missingWarning && t.missingWarning[0]);
            for (const char* c : t.candidates) assert(c && c[0]);
            // Nothing auto-plays: inserting a tape is not pressing Play.
            assert(!t.autoPlay);

            if (cfg.basicType == BasicType::IntegerCassette) {
                ++programMode;
                assert(t.forceProgramMode &&
                       "an Integer BASIC tape is DATA the ACI reads, not music");
                assert(t.candidates.size() == 2 &&
                       std::strcmp(t.candidates[0], "cassettes/BASIC.aci") == 0);
            } else {
                ++deckAudio;
                assert(id == pom1::kDefaultPresetId &&
                       "only the shipped default preloads a non-data tape");
                assert(!t.forceProgramMode);
                assert(std::strcmp(t.candidates[0], "cassettes/WOZ_talk.mp3") == 0);
            }
        }
        assert(programMode > 0);
        assert(deckAudio == 1 && "WOZ_talk belongs to exactly one preset");
        assert(withTape == programMode + deckAudio);
    }

    // -----------------------------------------------------------------
    // §7 DevBench, boot animation, banner.
    // -----------------------------------------------------------------
    {
        int benches = 0;
        for (int i = 0; i < kMachinePresetCount; ++i) {
            const PresetId id = presetIdFromIndex(i);
            const bool bench = isDevBenchPreset(id);
            if (bench) ++benches;
            // The bench profiles are the first three entries — named here, not
            // spelled as an index range at the call site.
            assert(bench == (i >= 0 && i <= 2));
            // A bench is a compile-and-run workflow: no ~3 s power-on show.
            assert(animatesBoot(id) == !bench);
            assert(showsBanner(id) == (id == pom1::kDefaultPresetId));
        }
        assert(benches == 3);
        assert(isDevBenchPreset(PresetId::CC65Bench));
        assert(isDevBenchPreset(PresetId::TMS9918Bench));
        assert(isDevBenchPreset(PresetId::Gen2Bench));
    }

    // -----------------------------------------------------------------
    // §8 The cold reset.
    //
    // The first apply skips it — Memory::Memory() has just done the same work.
    // The exception is a SILICON preset, whose just-armed power-on noise has to
    // replace the constructor's lenient seed on the very first frame.
    // -----------------------------------------------------------------
    {
        for (int i = 0; i < kMachinePresetCount; ++i) {
            const PresetId id = presetIdFromIndex(i);
            // Second and later applies always reset.
            assert(coldResetOnApply(true, id));
            // First apply: only Fantasy skips.
            assert(coldResetOnApply(false, id) == !isFantasyPreset(id));
        }
        assert(!coldResetOnApply(false, pom1::kDefaultPresetId) &&
               "first boot into the default profile wants no power-on noise");
    }

    std::printf("preset_decisions_smoke: OK\n");
    return 0;
}
