// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#ifndef POM1_MACHINE_COORDINATOR_H
#define POM1_MACHINE_COORDINATOR_H

#include <string>
#include <optional>

#include "CardTopology.h"

class Memory;

namespace pom1 {

/// Complete desired expansion-card state plus options configured while cards
/// are detached. This DTO is shared by GUI, CLI/headless and the application
/// facade; it contains no UI or threading concerns.
struct CardConfigurationRequest {
    enum class SystemRomProfile {
        Preserve,
        MonitorOnly,
        IntegerBasic,
        ApplesoftSd,
        ApplesoftSdFantasy,
        ApplesoftCffa1,
    };

    CardSet cards;
    TopologyMode mode = TopologyMode::Strict;
    JukeBoxJumper jukeBoxJumper = JukeBoxJumper::RAM16_ROM32;
    JukeBoxChipMode jukeBoxChipMode = JukeBoxChipMode::Flash;
    CodeTankJumper codeTankJumper = CodeTankJumper::Lower16;
    std::string codeTankRomPath;
    SystemRomProfile systemRomProfile = SystemRomProfile::Preserve;
    bool loadKrusader = false;
    bool loadCffa1Firmware = false;
    /// Perform a destructive machine reset before loading ROMs and attaching
    /// the target topology. Interpreted by EmulationController, which owns CPU
    /// and snapshot publication.
    bool coldReset = false;
    bool animateBoot = true;
    bool activateCassetteAudio = false;

    // Optional whole-machine settings. Absence means "preserve", which keeps
    // live card toggles lightweight while allowing preset application to fold
    // every fidelity knob into this same transaction.
    std::optional<int> presetRamKB;
    std::optional<bool> siliconStrict;
    std::optional<bool> outOfRangeStrict;
    std::optional<bool> vramNoiseOnReset;
    std::optional<bool> systemRamNoiseOnReset;
    std::optional<bool> cpuDecimalBugNMOS;
    std::optional<bool> dramRefresh;
    std::optional<bool> gen2RandomPowerOn;
};

enum class CardConfigurationError {
    None,
    TopologyRejected,
    DeviceConfigurationFailed,
    SystemRomLoadingFailed,
};

struct CardConfigurationResult {
    CardConfigurationError code = CardConfigurationError::None;
    std::string message;

    explicit operator bool() const { return code == CardConfigurationError::None; }
};

/// Executes deterministic machine transitions against an exclusively-owned
/// Memory instance. Locking and snapshot publication deliberately remain in
/// EmulationController, the thread-safe application facade.
class MachineCoordinator {
public:
    static void setCardEnabled(Memory& memory, CardId card, bool enabled);
    /// Call only after Memory has completed a real peripheral reset pass.
    static void markAttachedCardsReset(Memory& memory);
    /// Promote reset cards whose runtime producers are ready.
    static void activateResetCards(Memory& memory);
    static bool markCardActive(Memory& memory, CardId card);
    static bool markCardInactive(Memory& memory, CardId card);
    static CardConfigurationResult applyCardConfiguration(
        Memory& memory, const CardConfigurationRequest& request);
};

} // namespace pom1

#endif // POM1_MACHINE_COORDINATOR_H
