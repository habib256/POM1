# lib/a1io — P-LAB A1-IO & RTC card primitives

*[← dev/lib index](../README.md)* · sibling VIA card: [`../sd/`](../sd/) (microSD)

The A1-IO card pairs a 65C22 VIA at `$2000-$200F` with an emulated
ATMEGA32 driving a DS3231 RTC, optional DS18B20 probe, 8 analog inputs,
4 digital inputs, and a cascaded 74HC164 shift register output. The
ATMEGA continuously broadcasts 24 virtual registers via PORTA STROBE
+ PORTB DATA on a 100-cycle period.

Replaces the inline `read_rtc_reg` in `sketchs/apple1/io_rtc_clock`,
unblocks a future **sensor logger** project ([`../../TODO6502.md`](../../TODO6502.md)).

## Files

- **`a1io.inc`** — VIA register addresses, reg-ID equates
  (`A1IO_REG_HOURS`..`A1IO_REG_DIN3`), shift-register output port.
  Pure equates, idempotent.
- **`a1io.asm`** — `a1io_read_reg` (spin-on-broadcast read).

## Public symbols

| Symbol | Type | Description |
|---|---|---|
| `VIA_IRB`, `VIA_IRA` | equate | $2000 / $2001 — PORTB data, PORTA strobe+id |
| `VIA_SR` | equate | $200A — shift register (16-bit output via 74HC164 cascade) |
| `A1IO_REG_HOURS`..`A1IO_REG_DIN3` | equate | 24 broadcast register IDs (0..23) |
| `a1io_target` | ZP | 1-byte scratch slot owned by `a1io_read_reg` |
| `a1io_read_reg` | routine | X = reg ID → A = value |

## The 24-register broadcast map

| ID | Symbol | Meaning |
|---|---|---|
| 0  | `A1IO_REG_HOURS`    | 0..23 (24-hour) |
| 1  | `A1IO_REG_MINUTES`  | 0..59 |
| 2  | `A1IO_REG_SECONDS`  | 0..59 |
| 3  | `A1IO_REG_DAY`      | 1..31 |
| 4  | `A1IO_REG_MONTH`    | 1..12 |
| 5  | `A1IO_REG_YEAR`     | year − 2000 (0..255) |
| 6  | `A1IO_REG_DS3231_T` | DS3231 die temp (°C) |
| 7  | `A1IO_REG_PROBE_HI` | DS18B20 high byte (0 = probe disabled) |
| 8  | `A1IO_REG_PROBE_LO` | DS18B20 low byte |
| 9  | reserved | 0 |
| 10..17 | `A1IO_REG_ADC0..ADC7` | 8 analog inputs |
| 18..19 | reserved | 0 |
| 20..23 | `A1IO_REG_DIN0..DIN3` | 4 digital inputs (0 or 1) |

## Use

```asm
.include "apple1.inc"
.include "a1io.inc"
.include "a1io.asm"

read_clock:
        LDX #A1IO_REG_HOURS
        JSR a1io_read_reg
        STA hh
        LDX #A1IO_REG_MINUTES
        JSR a1io_read_reg
        STA mm
        LDX #A1IO_REG_SECONDS
        JSR a1io_read_reg
        STA ss
        RTS

sample_all_adc:
        ; Reads ADC 0..7 into adc_buf[8] using a tight loop.
        LDX #A1IO_REG_ADC0
        LDY #0
@lp:    JSR a1io_read_reg
        STA adc_buf,Y
        INX
        INY
        CPY #8
        BNE @lp
        RTS
```

In your project Makefile:

    LIB := -I ../../lib/apple1 -I ../../lib/a1io

## Required preset

A1-IO must be plugged. POM1:

- `--enable rtc` — A1-IO & RTC alone
- `--enable a1io-rtc` — adds A1-IO to any other preset (mutex with GEN2
  HGR which also lives at `$2000+`)

For deterministic time in tests:

    --rtc-freeze "2026-04-28 14:30:00"

freezes the RTC at that wall-clock time at boot (host-rate ticks
continue from there).

## Performance note

Each `a1io_read_reg` waits up to one full broadcast cycle (~24 × 100 =
2400 cycles ≈ 2.3 ms at 1.022 MHz). Reading all 24 regs sequentially
takes ~55 ms worst case. For a sensor logger sampling every second,
this is well within budget. For tight game loops, hoist the reads out
of the per-frame path.

## Migration path

`sketchs/apple1/io_rtc_clock/RtcClock.asm` already has an inline
`read_rtc_reg` (lines 65-74) identical to `a1io_read_reg`. Migration:

1. `.include "a1io.inc"` and `.include "a1io.asm"`.
2. Delete the local `read_rtc_reg` and the inline `VIA_IRB / VIA_IRA`
   equates (now in `a1io.inc`).
3. Replace each `JSR read_rtc_reg` with `JSR a1io_read_reg` (same input
   convention: X = reg ID).
4. Optionally rename the inline reg ID literals (`#0`, `#1`, …) to the
   new equates (`#A1IO_REG_HOURS`, etc.) for readability.

Project loses ~12 lines and gains a documented contract.
