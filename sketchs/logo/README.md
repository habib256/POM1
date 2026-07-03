# LOGO turtle sketches

*[← POM1 documentation index](../../doc/README.md)*

Machine-neutral **APPLE-1 LOGO V2.6** turtle programs for the POM1 DevBench — the
LOGO counterpart of `sketchs/basic_applesoft/`. Each `.logo` runs **unchanged on
both** LOGO cards: the **TMS9918** (CodeTank, `4000R`) and **Uncle Bernie's GEN2
HGR** (`6000R`). Open one in the Bench and click **Run** — `LogoProgramLoader` pokes
the `TO … END` procedures straight into the interpreter's procedure table and feeds
the entry line (no per-character typing). Language reference + tutorials:
[`APPLE-1_LOGO-2.6-MANUAL.md`](../tms9918/tool_logo/APPLE-1_LOGO-2.6-MANUAL.md).

| File | What it draws | Shows |
|------|---------------|-------|
| `Rosette.logo` | 40 squares spun 10° apart | nested `REPEAT` (one level) |
| `Hexagon.logo` | a hexagon | `REPEAT N [ FD s TR (360/N) ]` |
| `Star.logo` | 7-point star | over-turning past `360/N` |
| `Star8.logo` | 8-point star | star polygon `{8/3}` |
| `Squares.logo` | three growing squares | a procedure with a `:SIZE` parameter |
| `Flower.logo` | 36-petal flower | a proc calling another proc |
| `Spiral.logo` | right-angle spiral | tail recursion + `IF … [ STOP ]` |
| `Tree.logo` | recursive binary tree | non-tail (branching) recursion |
| `Rays.logo` | 36 rays from centre | `RANDOM`, `SETH`, `PU`/`PD` |
| `Meadow.logo` | scattered coloured petals | `RANDOM` args, `SETXY`, `SETPC`, recursion |

**Dialect notes** (see the manual §12–13): turns are **`TR`/`TL`** (turn right/left),
**not** `RT`/`LT`. Integers only — no floats, no negative literals (use `BK` to go
back). One arithmetic op per argument. Identifiers ≤ 6 chars, ≤ 2 params per
procedure, ≤ 60 chars per line. No comments.
