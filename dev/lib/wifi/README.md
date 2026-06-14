# lib/wifi — Wi-Fi Modem (W65C51N ACIA + ESP8266) primitives

Byte-level + AT-command helpers for the P-LAB Wi-Fi Modem card. The
card pairs a 65C51 ACIA at `$B000-$B003` with an ESP8266 doing TCP
transport and Hayes-style AT framing. From the 6502's view it's a
plain serial UART.

Until now no asm in this tree talked to the ACIA directly — the only
shipped consumer is `software/net/ATmodem.txt`, a pre-assembled ROM.
This lib unblocks scripted BBS sessions and a future minimal Telnet
client in pure 6502.

## Files

- **`acia.inc`** — register addresses (`ACIA_DATA`, `_STATUS`,
  `_COMMAND`, `_CONTROL`), status/command bit masks, baud rate codes
  (`ACIA_BAUD_300`..`ACIA_BAUD_19200`). Idempotent.
- **`acia.asm`** — byte-level send/receive: `acia_init`,
  `acia_send_byte`, `acia_recv_byte`, `acia_recv_avail`,
  `acia_send_str`.
- **`at.asm`** — Hayes-style AT framing: `at_send_cmd`, `at_send_dial`,
  `at_send_cr`, `at_recv_until_cr`.

## Public symbols

| Symbol | Type | Description |
|---|---|---|
| `ACIA_DATA`, `_STATUS`, `_COMMAND`, `_CONTROL` | equate | `$B000-$B003` |
| `ACIA_ST_RDRF`, `_TDRE`, `_DCD`, `_OVERRUN`, … | equate | status bits |
| `ACIA_CMD_DTR`, `_IRQ_DIS`, `_TX_BREAK` | equate | command bits |
| `ACIA_CTL_8N1_INT` | equate | 8N1 + internal clock framing |
| `ACIA_BAUD_50`..`ACIA_BAUD_19200` | equate | baud rate selectors |
| `acia_init`, `acia_send_byte`, `acia_recv_byte`, `acia_recv_avail`, `acia_send_str` | routine | byte-level |
| `at_send_cmd`, `at_send_dial`, `at_send_cr`, `at_recv_until_cr` | routine | AT framing |
| `acia_str_lo`, `acia_str_hi` | ZP | pointer used by `acia_send_str` and `at_recv_until_cr` |

ZP usage: 2 bytes (`acia_str_lo / acia_str_hi`), declared with `.ifndef`
so a tight-ZP project can alias to existing pointer slots before the
include.

## Use — minimal AT ping

```asm
.include "apple1.inc"
.include "acia.inc"
.include "acia.asm"
.include "at.asm"

main:
        LDA #ACIA_BAUD_9600
        JSR acia_init

        ; Send "ATZ\r" and read the OK reply
        LDA #<cmd_z
        LDX #>cmd_z
        JSR at_send_cmd

        LDA #<reply_buf
        STA acia_str_lo
        LDA #>reply_buf
        STA acia_str_hi
        JSR at_recv_until_cr        ; Y = length, buffer NUL-terminated

        ; Print the reply via Wozmon ECHO
        LDA #<reply_buf
        LDX #>reply_buf
        JSR print_str_ax            ; lib/apple1/print.asm

        JMP WOZMON

cmd_z:      .byte "Z", 0
reply_buf:  .res  64
```

Wire output:

```
ATZ\r
OK\r
```

## Use — dial a BBS

```asm
        LDA #<host_str
        LDX #>host_str
        JSR at_send_dial

        ; Read reply line ("CONNECT 9600" on success, "NO CARRIER" on
        ; failure). Hand off to a raw-byte loop after CONNECT.

host_str:   .byte "BBS.FOZZTEXX.COM:23", 0
```

In your project Makefile:

    LIB := -I ../../lib/apple1 -I ../../lib/wifi

## Required preset

The Wi-Fi Modem must be plugged. POM1 presets:

- `--preset 9` — Wi-Fi Modem alone
- `--enable wifi` — adds the modem to any other preset

POM1 implements only desktop builds; WASM stubs return `NO CARRIER`.

## Send/receive timing

The ACIA's `cyclesPerByte` is computed from the baud rate selector. At
9600 baud (10 bits per char including start/stop): ~1066 cycles per
byte = ~1.04 ms. `acia_send_byte` blocks on TDRE which on POM1 is
always set (mirrors the W65C51N silicon bug) — Tx is effectively
fire-and-forget at 9600 baud or below. Above 9600, real silicon would
back-pressure but POM1 does not.

`acia_recv_byte` blocks indefinitely. For UI-friendly reads, use
`acia_recv_avail` in a polling loop with `delay_ms_a`.

## What's NOT in this lib

- **TELNET IAC handling** — the modem strips IAC sequences itself, so
  the 6502 sees plain ASCII. If you target a non-IAC-stripping path
  (raw TCP), implement IAC peeling in your project.
- **+++ command-mode escape** — needs a 1 s silence on Tx, then `+++`,
  then 1 s silence again. Project-specific timing; build on
  `acia_send_byte` + `delay_ms_a` (lib/apple1/delay.asm).
- **Connection-state machine** — this lib provides bytes. Tracking
  CONNECT/NO CARRIER/disconnect transitions is the project's job.

## Future enhancements

- Telnet client (parse IAC if needed; line-mode command shell).
- BBS terminal program with ANSI/VT100 cursor handling.
- HTTP-over-TCP fetcher (requires the modem firmware to support raw
  TCP setup, which the ESP8266 AT firmware does via `AT+CIP*`
  commands not yet wrapped here).
