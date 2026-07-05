# SD CARD OS 1.3 — Nippur72 reference source (vendored for study)

Upstream, unmodified copy of P-LAB's **SD CARD OS 1.3** firmware, kept here as a
reference for building "an excellent Apple-1 command line". **Not compiled by
POM1** — study material only.

## Provenance

- **Project:** APPLE-1 SD CARD (+ IEC Commodore-bus adapter), by P-LAB.
- **Author:** Antonino "Nippur72" Porcino.
- **Repo:** https://github.com/nippur72/apple1-sdcard
- **Commit:** `8adb29c` — "allow patterns in DIR" (2023-11-08).
- **Version:** 1.3 (banner `*** SD CARD OS 1.3` in `sdcard/console.h`).
- **License:** CC BY 4.0 (per the P-LAB project pages https://p-l4b.github.io/sdcard/
  and https://p-l4b.github.io/iec/). Attribution retained via this file and the
  original `README.md` / source headers. Fetched 2026-07-05.

Only the *source* was vendored. The upstream `docs/` folder (23 MB of datasheet
PDFs, gerbers and board photos — all third-party reference material, not
Nippur72's code) was intentionally omitted.

## Layout

| Dir          | What it is |
|--------------|------------|
| `sdcard/`    | The command-line shell itself, written in C for **KickC** (`sdcard.c` + one `cmd_*.h` per command: `DIR/LS`, `CD`, `LOAD/SAVE`, `MKDIR/RMDIR`, `DEL/RM`, `TYPE`, `DUMP`, `READ/WRITE`, `MOUNT`, `BAS`, plus the `cmd_iec_*` bridges). `help_dir/` holds the per-command help text files. |
| `iec/`       | The IEC (serial-bus) kernel, hand-written 6502 in `.lm` "literate macro" assembly ported from the C64 Kernal: `kernal_serial.lm` (bit-bang bus timing), `kernal_load/save/dir/cmd/err.lm`, `iec_lib.lm`. This is the direct counterpart of POM1's `IECCard` + `Drive1541`. |
| `lib/`       | Small C headers for the Apple-1 target (`apple1.h`, `via.h`, `interrupt.h`, `utils.h`). |
| `kickc/`     | KickC linker/target configs (`apple1.tgt`, `apple1.ld`, jukebox variants). |
| `arduino/`   | The ATMEGA companion sketch (SD-card <-> 65C22 side). POM1 emulates this in `MicroSD`. |
| `IEC_PROGRAMMING.md` | Nippur72's own notes on the IEC command surface. |

## Why it's here — mapping to POM1

POM1 already emulates the *hardware* side of this firmware:

- `src/MicroSD.*`   ⇄ the microSD card (65C22 + ATMEGA) — `arduino/` above.
- `src/IECCard.*`   ⇄ the IEC daughterboard bus timing — `iec/kernal_serial.lm`.
- `src/Drive1541.*` ⇄ the virtual 1541 answering the kernel — `iec/kernal_*.lm`.

The **`@ERR` status-channel contract fixed in POM1** (`Drive1541::openChannel`
resets the read cursor so every `@ERR` restarts from byte 0) is defined by
`iec/kernal_err.lm` (`IECERR`: a `LISTEN` / `TALK` / `TKSA $6F` sequence, then an
`ACPTR`/`ECHO` loop until `STATUS` != 0 / EOI). Read that file to see the exact
handshake POM1's drive must satisfy.

The **`sdcard/` shell** is the design reference for a future first-class Apple-1
command line: how commands are dispatched, how help is served from files, how the
directory walker and path model work, and how DOS commands are bridged onto the
IEC bus (`cmd_iec_*.h`).
