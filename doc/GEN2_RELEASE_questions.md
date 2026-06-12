# GEN2 *release* card — open questions for Uncle Bernie

**Status:** Phase 0 blocker for the GEN2 beam-racing back-port (see `TODO.md` →
*Uncle Bernie GEN2 — moteur faisceau*). Phase 1 (cycle counter + floating-bus
address oracle, `src/Gen2VideoScanner.{h,cpp}`) is **done and merged**; Phase 2
(soft switches `$C250–$C257` + MSB blank flag) is **fully unblocked as of
2026-06-12** — Bernie delivered the complete bit-exact reference in
`doc/ColorGraphicsCard_doc_for_Arnaud.pdf` (transcribed under *Answers* below).
**Every question Q1–Q10 is now RESOLVED**; drop the `// SPEC PENDING BERNIE`
markers as Phase 2 lands. Key decided semantics: soft switches are **read-only**
(reads toggle + return HST0 in D7, writes are no-ops), HST0 = **1 during H/V-blank
with a 0 notch in the 3-cycle color burst**, page-2 HIRES at `$4000–$5FFF`, decode
`SEL = $Cxxx & !A11 & A9 & A4` (mirrors across `$C2/$C3/$C6/$C7xx`), latch
power-on indeterminate.

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

### 2026-06-12 — point-by-point PM reply (AppleFritter post #6)

Bernie answered the PM directly **and** sent a separate PDF (the *Apple-1 Graphics
Card* document) via PM that "should answer most of your questions." The PDF holds
the bit-exact detail for Q1 (soft-switch map), Q3 (HST flag model + code snippet),
Q4 (timing detail) and Q6 (mirror addresses) — **transcribe it into this file once
received**; the prose reply below pins the architectural decisions we can already
act on.

> _Note:_ Bernie remarked his PDF's `<hcnt>` H-counter and the 0…191 line counter
> "work the same way as I have assumed" — i.e. our `Gen2VideoScanner` cycle model
> (65×262, lines 0–191 live, higher = VBL) matches his hardware assumptions.

- **Q1 (soft-switch map):** "answered in the text" → **see the PDF**. Not restated
  in the prose; treat the `$C250–$C257 = $C050–$C057` 1:1 assumption as *likely
  correct* but **transcribe from the PDF before dropping the `// SPEC PENDING`
  marker**.
- **Q2 (read toggle vs status-only):** **ANSWERED — decisive.** *"only a read can
  change a soft switch. And the HST status appears in the MSB of the byte read.
  Write operations to the soft switches are ignored."* So the model is the
  **inverse split** of the naïve guess: **reads toggle the mode AND return the HST
  flag in bit 7; writes are ignored (no-op).** Rationale: a non-R/W-decoded write
  would flip the latch *and* clash the D7 bus driver (same failure mode he blames
  for dead IWMs). → `PeripheralBus` `$C25x` handler: `onRead` performs the toggle
  + returns `(HST<<7)|floating`; `onWrite = {}` **explicit no-op (block), not
  pass-through.** This resolves the "single most important point."
- **Q3 (MSB blank flag):** HST model + code snippet are **in the PDF**. Prose adds:
  the **low 7 bits read the floating data bus**, *"typically the 'bit vapors' of
  the H-Byte of the address of the soft switch being read"* — he writes `($D2)`,
  which looks like a slip for `$C2` (high byte of `$C25x`); **confirm in the PDF**.
  *"But this is not reliable. So in the pdf I recommend to put random data there"*
  to discourage rookies relying on it. → POM1 already seeds the floating bus; for
  the `$C25x` read we may either return the scanner's fetched byte (our current
  `floatingBus()` model) or deliberate noise — Bernie's intent is "garbage, don't
  rely on it," so either is defensible. **Polarity of bit 7 still only in the PDF.**
- **Q4 (timing):** *"Timing seems identical to what you wrote in the question.
  More details in the pdf."* → 65 cyc/line, 262 lines, h 25–64 visible, 192 live
  lines **confirmed**; exact VBL boundary detail awaits the PDF.
- **Q5 (PAGE 2 buffer):** **ANSWERED — yes.** *"Primary and Secondary pages both
  for the TEXT and the HIRES mode are fully supported and work identical to the
  Apple II graphics subsystem."* → PAGE 2 HIRES exists and `$C254/$C255` select it
  exactly like the Apple II. Address still to confirm against the PDF/Q9, but
  `$4000–$5FFF` (Apple-II-relative) is the safe assumption. `floatingBus()` page-2
  read at `$4000` is justified.
- **Q6 (`$C2xx` decode width):** *"It's more complex, as there are many 'mirror'
  addresses. Details in the pdf."* → there **are** mirrors/aliases within `$C2xx`
  (like the Apple-1 PIA `$D0xx`). Decode the `$C25x` handler **wider than 8 bytes**
  once the PDF gives the mask. **Pending PDF.**
- **Q7 (ACI coexistence):** **ANSWERED — clean.** *"ACI will co-exist with the
  graphics card with no side effects and no clashes. I had to move the soft switch
  addresses for the graphics card out and away from $C0xx for that reason. And
  'vaporlock' is really dead."* → no `$C0xx`/`$C25x` collision, no interference
  model needed; vaporlock path stays unimplemented. Honours the Parmigiani "one
  board" rule cleanly (ACI + GEN2 genuinely coexist on real hw).
- **Q8 (power-on latch state):** **ANSWERED — indeterminate, must be SW-init.**
  *"Apple-1 RESET will not affect the soft switch status."* Power-up state depends
  on the PLD type (GAL vs PAL) and *"can't be trusted"* — the POR circuits in those
  PLDs are unreliable. **Software using the card must initialize the soft
  switches.** → POM1 should **not** force TEXT/PAGE1/LORES on reset; pick a fixed
  but documented arbitrary cold-state (and never reset it on Apple-1 RESET). This
  contradicts the original Q8 guess of "Apple II cold start defaults."

**Logistics nugget:** Bernie's parallel-port keyboard-emulator dev rig is dead
(last notebook's printer port D3 failed) — he has **no working way to develop GEN2
software on real hardware right now** and is actively waiting on a Linux POM1 build
with the GEN2 card to continue. Raises the priority of landing Phase 2/3. He
offered to sanity-check a test build against the real board.

### 2026-06-12 — PDF: *Apple-1 Color Graphics Card, 2nd Gen — Documentation for Arnaud*

File: `doc/ColorGraphicsCard_doc_for_Arnaud.pdf` (7 pp., written for POM1).
This is the bit-exact reference the post-#6 prose deferred to. **All Phase-2
`// SPEC PENDING BERNIE` markers can now be resolved from the facts below.**
Bernie's contact for follow-ups: `appleonedoc@gmail.com`. He wants a **Linux Mint
19/22** POM1 build with the card so he can resume GEN2 software development.

**Card technical spec (verbatim list).**
- RAM expansion to **54 KB**: `$0000–$BFFF` + `$E000–$EFFF` on motherboard → **answers Q9.**
- TEXT 40×25, B&W, lowercase available.
- LORES 40×50, 16 colors.
- HIRES 280×192, 6 colors.
- MIXED graphics/text.
- CPU can read horizontal **and** vertical blank flag (HST0).
- NTSC color (PAL color not supported), but **selectable 50 Hz or 60 Hz** vertical.
- Power: 2.3 W (GALs) / 3.7 W (MMI PALs).

**Q1 — soft-switch map (RESOLVED, Table 1).** `$C250–$C257` **is** a 1:1 port of
Apple II `$C050–$C057`. Bernie's symbol names (note the inverted labelling vs the
mode they *enable*):

```
$C250  TEXT_OFF   Turns TEXT mode off, enables all other modes      (= GRAPHICS)
$C251  TEXT_ON    Turns TEXT mode on, disables all other modes
$C252  MIX_OFF    Full graphics screen                              (MIXED off)
$C253  MIX_ON     Split screen, last four lines are TEXT mode       (MIXED on)
$C254  PAGE_ONE   Primary page  — TEXT/LORES $400–$7FF, HIRES $2000–$3FFF
$C255  PAGE_TWO   Secondary page — TEXT/LORES $800–$FFF, HIRES $4000–$5FFF
$C256  LORES_ON   LORES graphics (only if TEXT off)                 (HIRES off)
$C257  HIRES_ON   HIRES graphics (only if TEXT off)                 (HIRES on)
```

**Q2 — read/write (RESOLVED, confirms #6).** Soft switches are **read-only**:
*"they don't react when being written to."* A read on any decoded location changes
the switch **and** returns HST0 in D7. Writes are no-ops by design (avoids the D7
bus-driver clash). → `$C25x` `PeripheralBus`: `onRead` = toggle + `(HST0<<7)|noise`,
`onWrite = {}` **block**.

**Q3 — HST0 flag (RESOLVED, Appendix 1 + Listing 2).** HST0 is one of two bits of
the card's "horizontal timing state machine."
- **Polarity: HST0 = 1 while blanking (H-blank OR V-blank); = 0 during live video.**
- **Exception:** during the **color-burst period (3 CPU cycles)** HST0 reads **0**
  even though it's inside H-blank. Robust code double-samples and ORs the two reads
  (see Listing 1) to mask the burst notch. This is *why* HST0 is harder to use than
  the IIe/IIc `$C019` VBI bit — it carries H **and** V info, and the burst hole.
- **Low 7 bits = floating data bus**, intentionally unreliable; Bernie recommends
  POM1 **return random noise** in bits 0–6 to discourage rookies relying on it.
  (The `$D2` in post #6 was a slip — high byte of `$C25x` is `$C2`; the PDF settles
  it: just garbage, randomize it.)

Verbatim behavioral model (port directly — `line` 0…261@60 Hz / 0…311@50 Hz,
`hcnt` 0…64):

```c
int hst0_state(int line, int hcnt)
{
  if((hcnt > 12) && (hcnt < 16)) return 0; // in the BURST period
  if(line > 191) return 1;                 // in VBLANK
  if(hcnt > 24) return 0;                  // in live scan
  return 1;                                // in HBLANK
}
```

**Q4 — timing (RESOLVED).** 65 CPU cycles/scanline; **262 lines @ 60 Hz, 312 @
50 Hz** (Apple-1 emulators *should expose a 50/60 Hz option*). `hcnt` 25–64 ↔ visible
byte columns 0–39; lines 0–191 live, 192+ VBL. At VBL start ~**4200 CPU cycles**
remain before the beam re-lights the new page (useful budget for page-flip draws).

**Q5 — PAGE 2 (RESOLVED).** Both primary and secondary pages exist for TEXT **and**
HIRES, identical to Apple II. **HIRES page 2 = `$4000–$5FFF`**, selected by `$C255`
(`$C254` = page 1 `$2000–$3FFF`). → `floatingBus()` page-2 read at `$4000` confirmed.

**Q6 — decode width / mirrors (RESOLVED).** Decoder equation, verbatim:

```
SEL = $Cxxx & !A11 & A9 & A4;
```

So the eight switches **mirror every 8 locations** throughout `$C2xx, $C3xx, $C6xx,
$C7xx`, with the extra rule **A4 = 1** (third hex nibble ∈ {1,3,5,7,9,B,D,F}). The
recommended canonical addresses are `$C250–$C257`, but POM1 must decode the full
`SEL` mask to reproduce stray/mirror accesses. (A4=1 was reserved to leave room for
future Apple-1 expansion cards.)

**Q7 — ACI (RESOLVED, confirms #6).** No collision; switches were moved off `$C0xx`
exactly so the ACI keeps `$C0xx`. Vaporlock dead → use HST0. **Porting note for
games:** rewrite screen-soft-switch H-bytes `$C0`→`$C2`; neutralize other `$C0xx`
soft-switch accesses **except `$C030–$C03F`** (Apple II SPEAKER) — leave those
intact so the ACI produces the same sound effects via its TAPE OUT jack. POM1 needs
no ACI code change; optionally map TAPE-OUT-toggle → Apple-II-style click sound.

**Q8 — power-on (RESOLVED, confirms #6).** Apple-1 RESET does **not** affect the
latch; PLD POR is untrustworthy; software must initialize the switches. POM1: fixed
documented arbitrary cold-state, never cleared on RESET.

**Beam-racing complications (Bernie says: defer — "too soon to implement").**
- The Apple-1's 65 cyc/line includes **4 DRAM-refresh "stolen" cycles**, so pure
  CPU-cycle loops miscount vertical frequency by 61/65 ≈ 0.938. Modellable later by
  a mod-65 counter that bumps `hpos` on each refresh cycle before a memory read.
- The motherboard video scanner and the card's scanner are **not genlocked**, but
  their phase relationship is fixed after power-up.
- For HIRES↔LORES / HIRES↔TEXT mid-screen splits, the SS_TEXT switch must be turned
  **off again at each split scanline's end** to keep the color burst alive (else the
  next line may drop to B&W via the TV's unpredictable "color killer").

**Other / future hooks (not blocking Phase 2):**
- **Char set (Table 2):** 2716 EPROM = full ASCII like the IIe. Bits 7/6/5 of the
  screen byte = display attribute (inverted/flashing/normal); low 6 bits = Apple-II
  encoding. Relevant only once GEN2 TEXT/LORES rendering lands (POM1 is HGR-only today).
  Bernie can send a parseable char-set template.
- **Improved GEN1/GEN2 ACI** is available by adding a **`$C5xx` PROM page** (fast
  ACI routines, BASIC load in ~2 s). Bernie can send the binary — possible future
  POM1 enhancement, separate from the graphics card.
- Bernie's own switch-level Visual-6502 emulator **EXACTA-1** runs ~10× slower than
  real hardware; he relies on POM1 for game dev — hence priority on Phase 2/3.

### New questions raised by the thread (next PM round)

- **Q9 — 48 KB DRAM exposure.** How much of the card's 48 KB is CPU-addressable,
  and where? Is it an Apple-II-style 48 K RAM expansion, or only the video pages
  (HGR `$2000`/`$4000`, TEXT/LORES in the low 4 K)? This directly affects POM1's
  memory map, not just the renderer.
- **Q10 — VMA / write-through visibility.** Does the write-through latch make any
  CPU-visible difference (timing, a readable status), or is it purely internal?
  (Almost certainly internal — confirm so we can ignore it.)

### Per-question status

**ALL RESOLVED as of 2026-06-12** via the PDF (`ColorGraphicsCard_doc_for_Arnaud.pdf`).
Phase 2 is fully unblocked; drop every `// SPEC PENDING BERNIE` marker.

- **Q1 (soft-switch map):** **RESOLVED.** 1:1 port of `$C050–$C057`; see Table 1
  transcription. `$C250` TEXT_OFF … `$C257` HIRES_ON.
- **Q2 (read toggle vs status-only):** **RESOLVED.** Reads toggle **and** return
  HST0 in D7; writes are no-ops. `onWrite = {}` block.
- **Q3 (MSB blank: H/V, polarity, low 7 bits):** **RESOLVED.** HST0 = **1 while
  H/V-blank, 0 live**, with a **0 notch during the 3-cycle color burst**; low 7
  bits = random noise. Behavioral `hst0_state(line,hcnt)` transcribed above.
- **Q4 (video timing / blank boundaries):** **RESOLVED.** 65 cyc/line; 262 @60 Hz
  / 312 @50 Hz (expose a toggle); `hcnt` 25–64 ↔ cols 0–39; lines 0–191 live.
- **Q5 (PAGE 2 HIRES buffer):** **RESOLVED.** Yes — HIRES page 2 = `$4000–$5FFF`,
  `$C255` selects.
- **Q6 (`$C2xx` decode width):** **RESOLVED.** `SEL = $Cxxx & !A11 & A9 & A4`;
  mirrors every 8 across `$C2xx/$C3xx/$C6xx/$C7xx`, A4=1.
- **Q7 (ACI coexistence / bus interference):** **RESOLVED.** No clash; keep
  `$C030–$C03F` SPEAKER accesses for ACI sound.
- **Q8 (power-on latch state):** **RESOLVED.** Indeterminate; software must init;
  RESET does not touch it.
- **Q9 (48 KB DRAM exposure):** **RESOLVED.** Card is a **54 KB RAM expansion**
  `$0000–$BFFF` + `$E000–$EFFF`; graphics pages live in the low DRAM (TEXT/LORES
  `$400/$800`, HIRES `$2000/$4000`) via write-through.
- **Q10 (VMA / write-through visibility):** **RESOLVED — internal only.** No
  CPU-visible side effect; POM1's passive-read model is faithful.
