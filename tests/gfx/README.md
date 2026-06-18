# tests/gfx — golden-image regression fixtures

*[← POM1 documentation index](../../doc/README.md)*

Frozen inputs + reference renders for POM1's headless graphics-regression
capture (`--dump-gen2-frame` / `--dump-tms-frame`, see [`doc/CLI.md`](../../doc/CLI.md)).
Driven by [`tools/test_gfx_regress.py`](../../tools/test_gfx_regress.py) and the
`gfx_regress_*` ctest entries.

## Why a *frozen* binary

The `.bin` here is committed so the regression tests the **emulator's renderer**,
not the cc65 toolchain: a CI box with a different cc65 would assemble slightly
different bytes and produce a different frame, which would be a false failure.
Loading a frozen `.bin` isolates the render path. The capture is deterministic
(`--dump-after-cycles`, a host-independent point in emulated time), so the golden
PNG matches byte-for-byte on any machine.

## Files

- `hgr_testcard.bin` — frozen GEN2 HGR test card (built from
  `dev/projects/hgr_testcard/`, loads + runs at `$E000`).
- `hgr_testcard_gen2.png` — golden render (280×192) of the above on preset 11
  (Uncle Bernie GEN2 HGR) after 2,000,000 emulated cycles.

## Regenerate a golden (after an intended renderer change)

```sh
python3 tools/test_gfx_regress.py --card gen2 --preset 11 \
    --load 0xE000:tests/gfx/hgr_testcard.bin --run 0xE000 \
    --golden tests/gfx/hgr_testcard_gen2.png --update
```

Review the new image before committing — a golden update is a deliberate "the
render changed on purpose" act.
