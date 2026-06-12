# GEN2 *release* card — open questions for Uncle Bernie

**Status:** Phase 0 blocker for the GEN2 beam-racing back-port (see `TODO.md` →
*Uncle Bernie GEN2 — moteur faisceau*). Phase 1 (cycle counter + floating-bus
address oracle, `src/Gen2VideoScanner.{h,cpp}`) is **done and merged**; Phase 2
(soft switches `$C250–$C257` + MSB blank flag) is **blocked** until the answers
below land. Phase 2 can be implemented speculatively against the assumptions
here — every guess is isolable and marked `// SPEC PENDING BERNIE` in code — but
the bit-exact map needs confirmation before it ships.

**POM2 reference (audit 2026-06-10).** The upstream beam-racing engine in
`../POM2/` has evolved significantly since the initial POM1 plan was written.
Horizontal mid-scanline splits (per-byte-column granularity) landed **2026-06-09**
in both the RGBA framebuffer *and* the 14.318 MHz composite signal. The POM1
back-port should mirror these POM2 artefacts — not the older scanline-only model
described in early notes below:

| POM2 artefact | Role for POM1 back-port |
|---------------|-------------------------|
| `Memory::VideoEvent{emuCycle, scanline, kind, value}` | Journal model — `emuCycle` is authoritative; horizontal position via `frameCycleToPos` |
| `Apple2Display::frameCycleToPos(emuCycle)` | `byteCol = clamp((emuCycle % 65) − 25, 0, 40)` |
| `Apple2Display::forEachBeamSegment` | Shared decomposition for RGBA + composite — **port this first** |
| `renderInternalSegment` / `renderBeamRacing` | Column-bounded HGR (decode whole line, clip write-back) |
| `fillCompositeSignal` + `paintHgr(col0,col1)` | Phase 4 optional — same `forEachBeamSegment` |
| Tests `horizontal_split`, `beam_race_composite` | Adapt for GEN2 `$C25x` addresses |

Out of scope for POM1: POM2's 560-wide IIe path (`horizontal_split_560`,
save/restore `frame80`), DHGR, PAL 50 Hz profiles. Detail → `POM2/DEV.md` §
Beam-racing, `POM2/TODO.md` § Display.

**Sent:** 2026-06-09 (Arnaud → Uncle Bernie, AppleFritter PM).

**Why each answer matters (impl mapping):**
- Q1 → the `$C250–$C257` → `Gen2VideoScanner::DisplayState` write decode.
- Q2 → whether the `PeripheralBus` handler toggles on read (architectural fork).
- Q3/Q4 → `inHorizontalBlank()` / `inVerticalBlank()` + the MSB read value.
- Q5 → whether `floatingBus()` page 2 reads `$4000–$5FFF` or stays page 1.
- Q6 → `$C2xx` decode width (mirrors/aliases) for stray accesses.
- Q7 → ACI ↔ GEN2 bus interference model (Parmigiani "one board" exception).
- Q8 → power-on `DisplayState` default.

---

## Message sent

Hi Uncle Bernie,

Take your time… such work needs time.

I'm adding the cycle-accurate video scanner (Apple II 65 cycles × 262 lines
counter + MAME `scanner_address` floating bus) so that HGR game ports target the
release card properly.

From your AppleFritter posts I understand the release board (a) moved the
graphics soft switches to `$C250–$C257`, and (b) since the classic `$C050`
vaporlock doesn't work with the ACI present, exposes a blank flag in the MSB when
those switches are read. I want to get this bit-exact and I'd rather not guess.
Could you confirm or correct the following?

**1. Soft-switch map.** Is `$C250–$C257` a 1:1 port of the Apple II
`$C050–$C057`? My current assumption:

```
$C250 │ GRAPHICS
$C251 │ TEXT
$C252 │ MIXED off
$C253 │ MIXED on
$C254 │ PAGE 1
$C255 │ PAGE 2   (← is this one right?)
$C256 │ LORES
$C257 │ HIRES
```

Anything reordered, dropped, or with different power-on defaults?

**2. Read vs write.** On the Apple II, any access to `$C05x` (read or write)
flips the mode. On the release card, does a read of `$C25x` also toggle the mode,
or is the read non-toggling / status-only (i.e. you decode R/W separately, so
software can poll the blank flag without disturbing the display mode)? This is
the single most important point for me.

**3. The MSB blank flag.** On a `$C25x` read:
- Is bit 7 asserted during H-blank, V-blank, or both?
- Polarity — is bit 7 = 1 while blanking, or 1 while active video?
- What are the low 7 bits — the floating-bus byte the scanner is fetching, zeros,
  or something else?

**4. Video timing.** Does the card use standard Apple II NTSC timing — 65 CPU
cycles/line, 262 lines, 40 visible bytes (h-counter 25…64), 192 visible lines —
and assert the blank MSB exactly on those boundaries? If the release card differs
(different active window, different VBL line count), what are the real numbers?

**5. Second HGR page.** Does the release card support a PAGE 2 HIRES buffer
mirroring the Apple II (i.e. `$4000–$5FFF` in the Apple-1 address space), or is it
page 1 only at `$2000–$3FFF`? If page 2 exists, what address does it occupy, and
does `$C255`/`$C254` select it for display?

**6. Address decoding / A6.** You mentioned the release PCB has its own A6 decode
constraints. Is `$C2xx` fully decoded to the eight `$C250–$C257` locations, or are
there mirrors/aliases within `$C2xx` (the way the Apple-1 PIA incompletely decodes
`$D0xx`)? Knowing the exact decode lets me reproduce stray accesses correctly.

**7. Multiplexing! ACI coexistence.** With the ACI plugged, I assume the graphics
switches at `$C25x` and the ACI at `$C0xx` never collide, and the old `$C050`
vaporlock is simply dead. Is that right? And when the ACI drives `$C0xx` during a
video refresh cycle, does anything change on the GEN2 side (blank flag,
floating-bus contents)?

**8. Reset/power-on state** of the display-mode latch — TEXT/PAGE1/LORES like an
Apple II cold start, or something specific to the card?

That's everything I need to make the emulated release card cycle-accurate. POM1
is free and educational; I have already credited you for the hardware, and I'll
document the map. Happy to share a test build once the beam-racing path lands, so
you can sanity-check it against the real board.

Thank you very much,

Arnaud

---

## Answers from Bernie

> _Record his replies here as they arrive — once Q1, Q2, Q3 are confirmed,
> unblock Phase 2 and drop the `// SPEC PENDING BERNIE` markers in
> `src/Gen2VideoScanner.{h,cpp}` + the new `$C25x` `PeripheralBus` handler._

### 2026-06-09 — public AppleFritter post (PCB layout reveal)

Not a point-by-point reply to the PM, but the card announcement (PCB routing
finished, "short version" without cooling fan, all parts on the back except the
44-pin connector, cutout for the ACI audio plugs) confirms two things and pins
design intent:

- **Q3 (partial):** the blank flag tracks **both VBLANK and HBLANK** — quote:
  *"all the features like the special flag I've added to allow software to track
  VBLANK and HBLANK events on the card. A much more powerful hardware feature
  than the VBI flag seen in the Apple IIe and IIc."* → confirms the
  `MSB = (inHorizontalBlank() || inVerticalBlank())` model and the `$C019`-style
  bit-7 idiom. **Still open:** exact polarity (blank=1 vs active=1) and the low
  7 bits.
- **Q7 (context):** the PCB has a dedicated cutout for the ACI audio plugs, so
  the card is physically designed to coexist with the ACI — consistent with the
  "vaporlock is dead, use the MSB flag instead" story. Electrical interference
  detail during refresh still open.

**Design intent for Phase 3 (beam-racing):** Bernie expects the flag to enable
not just **vertical** screen splits at any scanline, but **horizontal** splits —
columns of TEXT alternating with columns of LORES graphics (a "color peg"
Codebreaker). → the beam-raced renderer must support **mid-scanline mode
changes**, not just per-band switching.

> **✅ POM2 renderer granularity (updated 2026-06-10).** The early concern that
> POM2 was scanline-quantized is **obsolete**. As of **2026-06-09** POM2 ships
> per-byte-column beam-racing via `forEachBeamSegment` + `frameCycleToPos`
> (`POM2/src/Apple2Display.cpp`). `VideoEvent.emuCycle` was always stored; only
> the horizontal component was previously discarded at replay — that is fixed.
> RGBA and composite (`fillCompositeSignal`) share the same decomposition;
> pinned by `horizontal_split` and `horizontal_split_composite`.
>
> **Back-port plan for POM1:** copy the POM2 trio (`VideoEvent.emuCycle` journal,
> `frameCycleToPos`, `forEachBeamSegment`) — adapted for GEN2 `$C250–$C257` soft
> switches and Bernie MSB blank reads (Phase 2). POM1 subset = 280-wide TEXT /
> LORES / HGR only (no 560-wide IIe save/restore). Still needed on POM1: TEXT +
> LORES renderers on the GEN2 window (HGR-only today), Phase 2 HBLANK MSB flag,
> then port `horizontal_split_smoke` with `$C25x` addresses. **v1 refinement
> scope:** column-byte boundary is exact; transition cycle within a character
> clock is deferred (same as POM2). Tracked in `TODO.md` Phase 3.

### 2026-06-09 — full thread review (AppleFritter posts #1–#22)

Mining the whole project thread (mostly PCB/economics/history, but with real
technical nuggets) for answers and emulation-relevant facts:

**Architecture / compatibility**
- The Apple-1 video chain is *almost identical* to the Apple II — same IC types:
  74161 H/V counters, 74166 video shift register, Signetics 2513 char gen
  (#1, #17). GEN2 taps and extends that chain; Bernie states the card is
  **"100% Apple II compatible"** repeatedly (#4, #12, #19) → strong indirect
  support for the standard `$C05x`-semantics soft-switch set behind Q1, and for
  a PAGE 2 HGR buffer (Q5).
- GEN2 implements Woz's **'long cycle'** (US Pat. 4,136,359) with *no trace cuts*
  + one short flight wire, yielding NTSC that also works on LCD TVs (the GEN1
  weakness) (#1).
- **All blanking signals were thrown OUT of the video path** to fix the Apple IIe
  "no orange vertical line at the right edge" bug (#1). ⚠ This is the *display*
  blanking logic — **distinct from** the software-readable VBLANK/HBLANK flag he
  *added* (#22). Don't conflate the two in the model.

**Memory / framebuffer (new, relevant to Q5 + POM1 memory map)**
- The card carries **its own 48 KB DRAM** (cheap 64k×4 41464s; period-correct
  fallback is 96× 4k×1) (#15, #19). HIRES needs 8 KB of it.
- **Write-through**: TEXT/LORES pages live in the Apple-1 *lower 4 KB* DRAM bank.
  An Apple-1 quirk scrambles the slot/expansion address bus whenever on-board
  DRAM is accessed; GEN1 had to *disable* the Apple-1 DRAM. GEN2 instead uses an
  **address latch + a VMA signal from the card** to capture the unscrambled
  address just before the scramble, so the card's graphics DRAM mirrors CPU
  writes (#1, #5, #15). Net effect for emulation: the card "sees" CPU writes to
  the graphics pages — POM1's passive-read model is a fair abstraction.
- **Only HIRES works on the current prototype** — TEXT/LORES need the 2716 video
  ROM rewrite (HIRES is the only Apple II mode needing no byte→dot-pattern ROM)
  (#1, #15). ✅ This matches POM1 today (we only model HGR `$2000-$3FFF`).

**Timing (answers Q4 in part)**
- ✅ **65 CPU cycles per scanline — explicitly confirmed**: *"the scan line
  timing is the same (65 CPU cycles per scan line)"* (#19), *"Even the scan line
  timing is the same"* (#19). Same 74161 counter chain as Apple II ⇒ 262-line
  NTSC frame is implied. Exact active window (h 25–64, 192 visible lines) and the
  VBL boundary still unconfirmed, but Apple-II-standard is the safe assumption.

**Palette**
- HIRES = **4 colors + black (background) + white** (#22) — standard Apple II HGR,
  which POM1 already renders via the MAME artifact LUT.

### New questions raised by the thread (next PM round)

- **Q9 — 48 KB DRAM exposure.** How much of the card's 48 KB is CPU-addressable,
  and where? Is it an Apple-II-style 48 K RAM expansion, or only the video pages
  (HGR `$2000`/`$4000`, TEXT/LORES in the low 4 K)? This directly affects POM1's
  memory map, not just the renderer.
- **Q10 — VMA / write-through visibility.** Does the write-through latch make any
  CPU-visible difference (timing, a readable status), or is it purely internal?
  (Almost certainly internal — confirm so we can ignore it.)

### Per-question status

- **Q1 (soft-switch map):** _pending exact `$C25x` addresses_ — **reinforced** by
  repeated "100% Apple II compatible."
- **Q2 (read toggle vs status-only):** _pending_ — architectural fork, still the
  single most important open point.
- **Q3 (MSB blank: H/V, polarity, low 7 bits):** **H+V confirmed** (#22);
  polarity + low 7 bits _pending_. (NB: video-path blanking removed ≠ this flag.)
- **Q4 (video timing / blank boundaries):** **65 cycles/scanline confirmed**
  (#19); 262 lines implied; exact visible window / VBL boundary _pending_.
- **Q5 (PAGE 2 HIRES buffer):** _pending_ — likely **yes** (Apple II compatible +
  48 KB on-card DRAM); exact address unconfirmed. See also Q9.
- **Q6 (`$C2xx` decode width):** _pending_
- **Q7 (ACI coexistence / bus interference):** physical coexistence confirmed
  (ACI cutout); the bus-scramble-on-DRAM-access quirk + address-latch/VMA
  write-through is the mechanism (#1, #5); electrical/refresh detail _pending_.
- **Q8 (power-on latch state):** _pending_
