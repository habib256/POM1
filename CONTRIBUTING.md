# Contributing to POM1

*[← README](README.md) · [Architecture](ARCHITECTURE.md) · [Doc map](doc/README.md)*

POM1 is an Apple 1 emulator plus a set of expansion cards, written in C++17
against Dear ImGui. This file is the practical half: how to build it, what the
automated gates want from you, and the handful of house rules that are not
obvious from the code.

**Read [`ARCHITECTURE.md`](ARCHITECTURE.md) first.** It is short (130 lines) and
it is where the shape of the program lives — including §5, a table saying where
a given kind of change belongs, and §6, an honest list of what is deliberately
not clean. Everything below assumes you have read it.

## Build and test

```bash
./setup_pom1.sh                 # deps + Dear ImGui + a configured build/
cd build && make -j && ctest    # ~2 min
```

Dear ImGui is **not vendored** — `.gitignore`d, cloned per machine by
[`tools/ensure_imgui.sh`](tools/ensure_imgui.sh), which enforces both the
docking branch and a version floor from the root `IMGUI_VERSION` file. If setup
fails on a network error, run it again; it retries three times on its own.

`cc65` is optional. Without it the emulator still builds and the release gate is
still green — the toolchain-dependent tests **skip** (exit 77) rather than fail.

### The two test lanes

| Lane | What | Count |
|---|---|---|
| `ctest -L emulator` | the release gate — must be green with no cc65 and no editors | 98 |
| `ctest -L devtools` | the paint/sprite editors, the DevBench/cc65 pipeline, the BASIC compilers | 28 |

A test is `devtools` only when its *subject* is the development environment.
Anything unlabelled defaults to `emulator`, deliberately: an unlabelled devtools
test turns the release gate red on a cc65-less machine, which is visible — the
other default would drop a test out of the gate in silence.

The emulator can also be driven without a window:

```bash
./POM1 --headless --preset 12 --exit-after-cycles 2000000
./POM1 --list-presets
```

## The automated gates

Twelve tests guard properties no compiler can. Each explains *why it exists* at
the top of its own script — read that before working around one.

| Gate | Refuses |
|---|---|
| `architecture_check` | a new include from core/devices toward ImGui or `MainWindow`; growth of the frozen facades |
| `window_registry_sync` | a window without a registry row (no persistence, no menu entry) |
| `shortcuts_sync` | a `Ctrl`+letter binding — it would shadow that letter's Apple-1 control code |
| `version_sync` | a hardcoded version string disagreeing with the root `VERSION` |
| `imgui_pin_sync` | an ImGui tag literal disagreeing with `IMGUI_VERSION` |
| `doc_paths_sync` | a backticked source path in the docs that does not exist |
| `resource_probes_sync` | a hand-rolled `"x", "../x", "../../x"` walk instead of `ResourceLocator` |
| `action_pins_sync` | a GitHub Action referenced by tag instead of a pinned SHA |
| `cli_flags_sync`, `crt_params_sync`, `zp_map_check`, `codetank_claudio_gate` | drift between a table and its documentation or its ROM |

### The one rule that matters most

`tools/architecture_baseline.json` holds **ceilings, not targets**: line counts
and public-method counts that may only go *down*.

> **Never raise a ceiling to make CI green.** Lower one when a refactor earns
> it. The only acceptable raise is one that *names what bought it* — in the
> commit message and in the changelog. There are worked examples of both in
> `CHANGELOG.md`.

## House rules

These are the ones a newcomer trips over. The reasoning behind each is in
[`CLAUDE.md`](CLAUDE.md), which is the exhaustive reference — grep it whenever
something surprises you.

- **Decisions leave the UI.** `MainWindow_*` is ~17 000 lines with almost no
  direct test coverage, and it is where the known historical defects lived.
  Anything that is a *decision* rather than a *draw call* goes into a pure
  header — no ImGui, no GLFW, no `MainWindow` — with its own smoke test. There
  are nine such seams; `src/LayoutDecisions.h` is a representative one.
- **One board at a time** (Parmigiani's rule). On real hardware exactly one
  P-LAB card is plugged, and several overlap address windows. POM1 breaks this
  only in the two "Fantasy" presets, on purpose. Conflicts are declared as data
  in `src/BusConflicts.h`, never as ad-hoc checks.
- **A new window is one row** in `windowRegistry()` — persistence, the menus,
  the dock layout, per-preset presence and the command palette all derive from
  it.
- **Comments say *why*, not *what*.** The valuable comments in this tree record
  a defect that was paid for once. Keep that; delete nothing that names a scar.
- **`dev/` is English-only** — sources, comments and READMEs for the 6502 side.
  Pre-existing French there stays; new material is English.
- **Setup scripts speak English** (`setup_pom1.sh`, `tools/ensure_imgui.sh`):
  their output is the first thing a contributor ever sees, and the docs are in
  English.
- Prose language elsewhere is mixed: the reference docs are English,
  `TODO.md` and `CHANGELOG.md` are French. Commit messages have historically
  been French; either language is fine.

## Writing a test

No framework — `<cassert>` plus an `add_test` line. Follow
`tests/peripheral_bus_smoke_test.cpp` for an integration test, or
`tests/layout_decisions_smoke_test.cpp` for a pure one that links nothing.

Two things worth knowing:

- A smoke test for a pure seam should link *nothing but its header*. That is the
  property that makes the seam worth having.
- If your subject is `constexpr`, assert each property **twice** — once folded,
  once through a runtime value. Coverage instrumentation cannot see a
  compile-time evaluation, so a `static_assert`-only test reads as uncovered
  while being stronger than a runtime one. `tests/shortcut_table_smoke_test.cpp`
  shows the pattern.

Coverage is measured **per module**, never as one percentage:

```bash
tools/coverage.py --gate     # nightly CI job; needs clang + llvm-cov
```

`cpu`, `parsers`, `topology` and `snapshot` are gated. `ui` is reported and not
gated — a floor under 3.9 % would be a number pretending to be a promise.

## Sending a change

Branch, commit, merge with `--no-ff`. There is no PR template and no CODEOWNERS;
this is a small project. Before you push:

```bash
cd build && make -j && ctest
```

CI runs the same suite on Linux, macOS (Metal *and* OpenGL), Windows and WASM,
all four with warnings-as-errors, plus nightly sanitizer, fuzz and coverage jobs.

**One known flake:** `concurrent_frontends_smoke` measures lock hold and wait
times, and a shared CI runner can blow them under load without anything being
wrong (measured once in two runs). It is declared with `REPEAT UNTIL_PASS:3` for
that reason — if it fails all three times, that is real.
