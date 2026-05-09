#pragma once

// IECBus — Commodore IEC serial bus line-state model.
//
// The IEC bus is a 4-line open-collector wired-AND bus (ATN, CLK, DATA,
// optional SRQ). Every device on the bus may either pull a line LOW or
// release (tri-state) it. The bus level is HIGH only when every device
// has released the line. We model two participants for now: the Apple-1
// host (pulled state driven by the SN7406 daughterboard via VIA PORTB),
// and the virtual 1541 drive. SRQ is unwired on the P-LAB card and stays
// released forever.

#include <array>
#include <cstdint>
#include <cstddef>

namespace pom1 {

class IECBus {
public:
    enum class Line : uint8_t { ATN = 0, CLK = 1, DATA = 2, SRQ = 3 };
    static constexpr size_t kLineCount = 4;

    void setHostPulled(Line l, bool pulled) {
        host_[static_cast<size_t>(l)] = pulled;
    }
    void setDrivePulled(Line l, bool pulled) {
        drv_[static_cast<size_t>(l)] = pulled;
    }

    bool hostPulled(Line l) const  { return host_[static_cast<size_t>(l)]; }
    bool drivePulled(Line l) const { return drv_[static_cast<size_t>(l)]; }

    // Wired-AND: HIGH unless any device pulls LOW.
    bool level(Line l) const {
        size_t i = static_cast<size_t>(l);
        return !(host_[i] || drv_[i]);
    }

    // Reset to bus-idle (everyone released).
    void release() {
        host_.fill(false);
        drv_.fill(false);
    }

    // Snapshot helpers.
    std::array<bool, kLineCount> hostBits()  const { return host_; }
    std::array<bool, kLineCount> driveBits() const { return drv_; }
    void restoreHostBits (const std::array<bool, kLineCount>& b) { host_ = b; }
    void restoreDriveBits(const std::array<bool, kLineCount>& b) { drv_  = b; }

private:
    std::array<bool, kLineCount> host_{false, false, false, false};
    std::array<bool, kLineCount> drv_ {false, false, false, false};
};

} // namespace pom1
