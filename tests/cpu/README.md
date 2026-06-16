# tests/cpu — CPU cycle-exact oracle fixture

`harte_6502.bin` is a compact binary distilled from the Tom Harte **"65x02
ProcessorTests"** suite (github.com/SingleStepTests/ProcessorTests, `6502/v1`)
— the real-hardware-validated 6502 reference. Built by
[`tools/gen_harte_fixture.py`](../../tools/gen_harte_fixture.py): the first
**100 cases** of each of the **151 documented opcodes** (15,100 cases), keeping
only `{initial regs+RAM, final regs+RAM, cycle count}`.

Consumed by [`cpu_harte_test.cpp`](../cpu_harte_test.cpp) (ctest
`cpu_harte_smoke`): for each case it seeds the CPU + RAM, steps **one**
instruction, and asserts the final registers, the touched RAM, **and the cycle
count** — a cycle-exact oracle on top of Klaus's functional test. Asynchronous
IRQ/NMI line timing is out of Harte's scope (opcode cases only).

The binary is committed so the oracle pins a fixed reference (no network at test
time). Regenerate / widen coverage:

```sh
python3 tools/gen_harte_fixture.py --cases 100 --out tests/cpu/harte_6502.bin
```

Format (little-endian): `"HRT1"`, `u32 count`, then per case `u8 op`, initial
state, final state, `u8 cycles` — where state = `u16 pc, u8 s,a,x,y,p, u16 ramN,
ramN×(u16 addr, u8 val)`.
