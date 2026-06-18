# 🍎 POM1 Quick Start — your first Apple 1 program in 5 minutes

New to the Apple 1? Start here. No assembly, no toolchain — just the emulator and
a few keystrokes. (Already built POM1? If not, see the [README](README.md)
*Quick Start*.) When you boot, you land at the WOZ Monitor's bare `\` prompt —
that's normal. Let's make it do something.

---

## Step 1 — Write your first program (BASIC, 30 seconds)

The friendliest way in. **BASIC needs no toolchain and runs instantly.**

1. Boot POM1 on **preset #4** (*Apple-1 with ACI & BASIC*) — the menu bar
   *Presets* list, or it may already be selected.
2. At the `\` prompt, type:  **`E000R`**  ↵   — this cold-starts **Integer BASIC**.
   The prompt changes to `>`.
3. Now type a program (BASIC numbers each line):

   ```basic
   10 PRINT "HELLO WORLD"
   20 GOTO 10
   RUN
   ```

4. It scrolls `HELLO WORLD` forever. Press **Ctrl-C** (or reset, `F5`) to stop.

🎉 You just programmed a 1976 computer. Change the text, the line numbers, add
`30 PRINT "FROM POM1"` — experiment.

> **Run a shipped classic instead?** From `>` go back to `\` (reset), then
> *File → Load Memory* → `software/Integer_basic/hamurabi.apl.txt`. Each listing
> ends with `E2B3R`, so it re-enters BASIC with the program loaded — just type
> `RUN`. (Cold-start once with `E000R` first if you just booted.)

---

## Step 2 — Read the WOZ Monitor prompt

The `\` prompt is the **WOZ Monitor**, Woz's 256-byte ROM. Its whole language:

| You type | It means |
|---|---|
| `E000R`        | **R**un the program at address `$E000` |
| `0`            | examine one byte at `$0000` |
| `0.20`         | examine the range `$0000`–`$0020` |
| `300: A9 0F`   | deposit bytes `A9 0F` starting at `$0300` |

Addresses are **hexadecimal** (`$0000`–`$FFFF`). That `xxxxR` = "run here" is how
you launch almost everything in POM1.

---

## Step 3 — Step up to assembly or C (the POM1 Bench)

Ready for real 6502 code? POM1 has a built-in editor — an Arduino-style sketch
IDE that compiles and runs without leaving the window. Full target reference:
[`doc/DEVBENCH.md`](doc/DEVBENCH.md).

1. **DevBench → POM1 Bench (sketch editor)…**
2. Click **New** (the file icon). Pick:
   - **Language:** *Assembly* or *C* — both fine to start.
   - **Target:** **Apple-1 dual 4K/8K (text) — start here**.
3. A `HELLO WORLD` starter appears. Click **Upload** (the ▶ arrow) — it
   assembles/compiles and runs on the emulator. Watch the screen.
4. Edit the message, Upload again. That's the whole loop.

> The Bench needs the **cc65** toolchain for asm/C (it tells you how to install
> it if missing). Prefer no toolchain at all? Pick the **Wozmon hex** target and
> Upload — it loads raw bytes with no compiler.

From here:
- **Assembly** → [`dev/Programming_Apple1_ASM.md`](dev/Programming_Apple1_ASM.md)
  (and the playbook [`dev/APPLE1DEV.md`](dev/APPLE1DEV.md)).
- **C** → [`dev/Programming_Apple1_C.md`](dev/Programming_Apple1_C.md).
- Browse 60+ ready-to-run programs in `software/` (*File → Load Memory*).

---

## Glossary

| Term | Meaning |
|---|---|
| **WOZ Monitor** | The `\`-prompt ROM you boot into. `xxxxR` runs code at `xxxx`. |
| **Preset** | A one-click machine config (RAM + expansion cards). 14 of them; see the [README](README.md#%EF%B8%8F-machine-presets) table. |
| **Integer BASIC** | Apple's 1976 BASIC at `$E000`. Cold-start `E000R`, warm re-entry `E2B3R`. Integers only. |
| **Applesoft Lite** | Floats + strings BASIC (`$6000`, `6000R`) on the microSD/CFFA1 presets. No `HOME`/`VTAB` — scroll-text only. |
| **Load Memory** | *File → Load Memory* — pastes a `.bin` or Woz-hex `.txt` into RAM. A `.txt` ending in `xxxxR` auto-runs. |
| **Woz hex / `.txt`** | A program as `AAAA: BB BB …` hex lines — exactly what you'd type at the Monitor, loadable in one go. |
| **bit 7 rule** | The Apple-1 keyboard/display use bit 7 as a "data valid" flag. Matters in asm; the libraries handle it for you. |
| **cc65** | The C/assembler toolchain (`ca65`/`ld65`/`cl65`) the Bench shells out to. `sudo apt install cc65` etc. |
| **POM1 Bench** | The in-app code editor (*DevBench* menu). Write asm/C, compile, run — desktop only. |

---

*Stuck at the `\` prompt and nothing happens? Type `E000R` for BASIC, or load a
program and run its address. The Apple 1 does nothing until you tell it to — that
was the 1976 experience.* Welcome aboard. 🍎
