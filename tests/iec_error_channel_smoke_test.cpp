// iec_error_channel_smoke — pins the Drive1541 @ERR status-channel read.
//
// The SD OS IEC kernel reads channel 15 as a fresh LISTEN/TALK/TKSA $6F
// sequence each time (kernal_err.lm IECERR: ACPTR/ECHO loop until STATUS!=0),
// so the drive must restart the status message from byte 0 on every (re)open.
// A read interrupted before EOI must NOT leave the cursor mid-buffer and make
// the next @ERR replay a truncated status. This test drives Drive1541's bus
// surface directly and asserts:
//   1. a fresh @ERR read after a partial read restarts from the top;
//   2. a complete read (to EOI) reverts the channel to "00, OK,00,00".

#include "Drive1541.h"

#include <cassert>
#include <cstdio>
#include <string>

using pom1::Drive1541;

// Read the whole command channel until talkByte() reports nothing more,
// collecting bytes and recording whether EOI landed on the final byte.
static std::string readErrChannel(Drive1541& drv, bool* sawEoi = nullptr) {
    std::string out;
    uint8_t b = 0;
    bool eoi = false;
    while (drv.talkByte(b, eoi)) {
        out.push_back(static_cast<char>(b));
        if (sawEoi) *sawEoi = eoi;
    }
    return out;
}

int main() {
    Drive1541 drv;
    drv.setError(21, "READ ERROR", 18, 4);
    const std::string kFull = "21, READ ERROR,18,04\r";

    // --- 1. Interrupted read then a fresh @ERR must restart from byte 0. ---
    drv.openChannel(15, /*isOpenCommand=*/false);   // TKSA $6F
    uint8_t b = 0;
    bool eoi = false;
    for (int i = 0; i < 4; ++i) {                   // read only a few bytes
        bool ok = drv.talkByte(b, eoi);
        assert(ok);
    }
    // Host aborts (UNTALK) mid-message — no EOI reached. New @ERR:
    drv.openChannel(15, /*isOpenCommand=*/false);
    bool eoi2 = false;
    std::string restarted = readErrChannel(drv, &eoi2);
    if (restarted != kFull) {
        std::fprintf(stderr,
                     "stale cursor: fresh @ERR did not restart from top\n"
                     "  expected '%s'\n  got      '%s'\n",
                     kFull.c_str(), restarted.c_str());
        return 1;
    }
    assert(eoi2 && "EOI must land on the final status byte (CR)");

    // --- 2. A complete read reverts the channel to "00, OK,00,00". ---
    drv.openChannel(15, /*isOpenCommand=*/false);
    std::string afterRevert = readErrChannel(drv);
    if (afterRevert.find("00, OK,00,00") == std::string::npos) {
        std::fprintf(stderr,
                     "channel did not revert to OK after full read: '%s'\n",
                     afterRevert.c_str());
        return 1;
    }

    std::printf("iec_error_channel_smoke: OK\n");
    return 0;
}
