; ============================================================================
; telemetry.inc — POM1 dev telemetry side channel ($C440-$C443)
;
; The game writes its per-frame state to TELE_DATA and closes the frame with
; TELE_FRAME; an external test harness reads the frame over TCP, decides the
; input, and (in lock-step) sends an ACK to advance exactly one frame. Harness
; input reaches the game through TELE_IN.
;
; Dev-only virtual device (not real hardware). Full design + protocol:
; doc/TELEMETRY_SIDE_CHANNEL.md. Python harness: tools/pom1_telemetry.py.
; ============================================================================

; --- Registers ---
TELE_DATA   = $C440        ; W : push one byte into the outbound frame
TELE_CTRL   = $C441        ; W : control opcode (see below)
TELE_STAT   = $C441        ; R : status bits (see below)
TELE_IN     = $C442        ; R : pop one inbound byte (0 if none)
TELE_INLEN  = $C443        ; R : inbound bytes pending (saturates at 255)

; --- TELE_CTRL opcodes (write) ---
TELE_END      = $01        ; end-of-frame: delimit + flush (parks in lock-step)
TELE_LOCKSTEP = $02        ; arm deterministic lock-step
TELE_FREERUN  = $00        ; disarm lock-step (free-running fire-hose tap)
TELE_SCHEMA   = $03        ; end-of-SCHEMA-frame: delimit + flush (0xA5 sentinel)

; --- Self-describing schema frames (v1) ---
; Two frame kinds share this window. A DATA frame (TELE_END/0xAA) carries the
; field VALUES; a SCHEMA frame (TELE_SCHEMA/0xA5) carries field DESCRIPTORS so
; a harness can decode the values by NAME. Emit the schema ONCE at startup, then
; one DATA frame per tick. A schema frame's payload is a run of descriptors,
; each = [type:1 byte][field name: ASCII bytes][$00 terminator]. Field type
; codes are below; the harness keeps the last schema and decodes later DATA
; frames field-by-field (raw-byte fallback if it never sees a schema). Schema
; frames never park lock-step.

; --- Schema field type codes ---
TELE_T_U8   = 1            ; 1 byte unsigned
TELE_T_S8   = 2            ; 1 byte signed
TELE_T_U16  = 3            ; 2 bytes little-endian unsigned
TELE_T_S16  = 4            ; 2 bytes little-endian signed
TELE_T_BOOL = 5            ; 1 byte, 0 = false, else true
TELE_T_CHAR = 6            ; 1 byte ASCII

; --- TELE_STAT bits (read) ---
TELE_CONNECTED = $80       ; b7: a harness is connected
TELE_INAVAIL   = $01       ; b0: at least one inbound byte waiting

; The harness's lock-step release token (consumed by POM1, never delivered to
; TELE_IN). For reference; the game never needs to read it.
TELE_ACK = $06

; ---------------------------------------------------------------------------
; Macros — all clobber A.
; ---------------------------------------------------------------------------

; Arm deterministic lock-step (call once at startup). After this, every
; TELE_FRAME parks the CPU until the harness ACKs — reproducible regardless of
; host speed.
.macro TELE_ARM
    lda #TELE_LOCKSTEP
    sta TELE_CTRL
.endmacro

; Push the byte currently in A into the outbound frame.
.macro TELE_PUTA
    sta TELE_DATA
.endmacro

; Push the byte at `src` (zp/abs) into the outbound frame.
.macro TELE_PUT src
    lda src
    sta TELE_DATA
.endmacro

; Push the immediate `val` into the outbound frame.
.macro TELE_PUTI val
    lda #(val)
    sta TELE_DATA
.endmacro

; Close the current frame: flush it to the harness. In lock-step this parks the
; CPU on this very instruction until the harness sends one ACK.
.macro TELE_FRAME
    lda #TELE_END
    sta TELE_CTRL
.endmacro

; Push one schema field descriptor: a type byte, then the field-name string,
; then a $00 terminator. `type` is a TELE_T_* immediate; `name` is a quoted
; string literal (e.g. TELE_FIELD TELE_T_U8, "head_x"). Call once per field, in
; declaration order, then TELE_SCHEMA_FRAME.
.macro TELE_FIELD type, name
    lda #(type)
    sta TELE_DATA
    .repeat .strlen(name), i
        lda #.strat(name, i)
        sta TELE_DATA
    .endrepeat
    lda #$00
    sta TELE_DATA
.endmacro

; Close the schema frame: delimit (0xA5) + flush. Never parks lock-step.
.macro TELE_SCHEMA_FRAME
    lda #TELE_SCHEMA
    sta TELE_CTRL
.endmacro
