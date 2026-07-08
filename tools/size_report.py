#!/usr/bin/env python3
"""size_report.py -- "what does this binary pay?" for ld65 map files.

Parses an ld65 map (``ld65 -m``; add ``-vm`` for the --why import graph) and,
optionally, the linker cfg, then reports:

  * per-MEMORY-region usage + headroom (bytes left before the region is full),
    with the cc65 C-stack window (__STACKSTART__/__STACKSIZE__) subtracted when
    it sits inside the region -- on the GEN2 cfg the stack lives at the top of
    RAM, so raw headroom overstates the real budget;
  * a per-module byte table (CODE/RODATA/DATA/BSS + zero page), sorted by
    cost -- the pay-per-use view that the DevBench archive link (rt.lib)
    made meaningful: a module in this list is there because something
    referenced it, not because it was force-linked;
  * ``--why <module|symbol>``: the shortest reference chain from a root object
    (one listed directly on the link line, i.e. NOT pulled from a .lib) to the
    module -- "why is this in my binary?". Needs a ``-vm`` map.

Usage:
  size_report.py prog.map [--cfg dev/cc65/apple1_gen2_c.cfg]
                          [--top N] [--why gen2_text.o] [--json]
                          [--min-headroom N] [--label NAME]

Exit codes: 0 ok / 1 parse or lookup error / 2 headroom below --min-headroom
(the CI hook: ld65 already hard-fails a real overflow, --min-headroom turns
"quietly almost full" into a red build before it becomes an overflow).

Pinned by ctest ``size_report_smoke`` (tools/test_size_report.py), which also
re-verifies the DevBench archive-link model end-to-end.
"""

import argparse
import json
import re
import sys
from collections import deque

# ---------------------------------------------------------------------------
# Map parsing
# ---------------------------------------------------------------------------

# Segments that occupy address space in the output image / RAM. ZEROPAGE is
# reported separately (its own region); NULL is ld65's /dev/null.
ADDR_SEGS_SKIP = {"NULL"}


def parse_map(text):
    """Return (modules, segments, imports).

    modules : {display_name: {"member": str, "lib": str|None, "segs": {seg: size}}}
    segments: {seg: {"start": int, "end": int, "size": int}}  (End inclusive)
    imports : {definer_member: [(symbol, importer_member), ...]} edges, and
              {symbol: definer_member} for --why by symbol. Empty without -vm.
    """
    modules, segments = {}, {}
    sym_def = {}
    edges = {}          # importer_member -> [(symbol, definer_member)]

    lines = text.splitlines()
    section = None
    cur_mod = None
    cur_import_sym = None
    cur_import_def = None

    mod_re = re.compile(r"^(\S+?)(?:\((\S+?)\))?:\s*$")
    seg_in_mod_re = re.compile(r"^\s+(\S+)\s+Offs=\S+\s+Size=([0-9A-Fa-f]+)")
    seg_list_re = re.compile(
        r"^(\S+)\s+([0-9A-Fa-f]{6})\s+([0-9A-Fa-f]{6})\s+([0-9A-Fa-f]{6})\s+[0-9A-Fa-f]+\s*$")
    import_head_re = re.compile(r"^(\S+)\s+\((.+?)\):\s*$")
    importer_re = re.compile(r"^\s+(\S+)")

    for line in lines:
        if line.startswith("Modules list:"):
            section = "modules"; continue
        if line.startswith("Segment list:"):
            section = "segments"; cur_mod = None; continue
        if line.startswith("Exports list"):
            section = "exports"; continue
        if line.startswith("Imports list:"):
            section = "imports"; continue
        if section == "modules":
            m = mod_re.match(line)
            if m:
                lib, member = (m.group(1), m.group(2)) if m.group(2) else (None, m.group(1))
                display = f"{member} ({lib})" if lib else member
                cur_mod = {"member": member, "lib": lib, "segs": {}}
                modules[display] = cur_mod
                continue
            m = seg_in_mod_re.match(line)
            if m and cur_mod is not None:
                size = int(m.group(2), 16)
                if size:
                    cur_mod["segs"][m.group(1)] = cur_mod["segs"].get(m.group(1), 0) + size
        elif section == "segments":
            m = seg_list_re.match(line)
            if m:
                segments[m.group(1)] = {
                    "start": int(m.group(2), 16),
                    "end": int(m.group(3), 16),
                    "size": int(m.group(4), 16),
                }
        elif section == "imports":
            m = import_head_re.match(line)
            if m:
                cur_import_sym, cur_import_def = m.group(1), m.group(2)
                sym_def[cur_import_sym] = cur_import_def
                continue
            m = importer_re.match(line)
            if m and cur_import_sym is not None:
                importer = m.group(1)
                edges.setdefault(importer, []).append((cur_import_sym, cur_import_def))

    return modules, segments, {"sym_def": sym_def, "edges": edges}


# ---------------------------------------------------------------------------
# cfg parsing (tolerant: the in-tree cc65 cfg dialect, values $HEX or decimal)
# ---------------------------------------------------------------------------

def _cfg_value(expr):
    expr = expr.strip().rstrip(";").strip()
    if expr.startswith("$"):
        return int(expr[1:], 16)
    if re.fullmatch(r"0x[0-9A-Fa-f]+", expr):
        return int(expr, 16)
    if re.fullmatch(r"\d+", expr):
        return int(expr)
    return None


def parse_cfg(text):
    """Return (regions, seg_region, symbols).

    regions   : {name: {"start": int, "size": int}}
    seg_region: {segment: {"load": region, "run": region|None}}
    symbols   : {name: int} (only numeric `value =` entries)
    """
    # strip # comments
    text = re.sub(r"#.*", "", text)
    regions, seg_region, symbols = {}, {}, {}

    def block(name):
        m = re.search(name + r"\s*\{(.*?)\}", text, re.S)
        return m.group(1) if m else ""

    for m in re.finditer(r"(\w+)\s*:\s*([^;]*);", block("MEMORY")):
        name, attrs = m.group(1), m.group(2)
        d = {}
        for a in attrs.split(","):
            if "=" in a:
                k, v = a.split("=", 1)
                d[k.strip()] = v.strip()
        start, size = _cfg_value(d.get("start", "")), _cfg_value(d.get("size", ""))
        if start is not None and size is not None:
            regions[name] = {"start": start, "size": size}

    for m in re.finditer(r"(\w+)\s*:\s*([^;]*);", block("SEGMENTS")):
        name, attrs = m.group(1), m.group(2)
        d = {}
        for a in attrs.split(","):
            if "=" in a:
                k, v = a.split("=", 1)
                d[k.strip()] = v.strip()
        if "load" in d:
            seg_region[name] = {"load": d["load"], "run": d.get("run")}

    for m in re.finditer(r"(\w+)\s*:\s*([^;]*);", block("SYMBOLS")):
        name, attrs = m.group(1), m.group(2)
        for a in attrs.split(","):
            if "=" in a:
                k, v = a.split("=", 1)
                if k.strip() == "value":
                    val = _cfg_value(v)
                    if val is not None:
                        symbols[name] = val
    return regions, seg_region, symbols


# ---------------------------------------------------------------------------
# Headroom
# ---------------------------------------------------------------------------

def region_report(segments, regions, seg_region, symbols):
    """Per-region usage. A segment occupies its run (== map) addresses in the
    run||load region; a load!=run segment ALSO consumes `size` raw bytes in its
    load region (the map only knows run addresses, so that part is a byte
    count, not an address range -- honest for ROM-budget cfgs like CodeTank).
    """
    out = {}
    for rname, r in regions.items():
        out[rname] = {"start": r["start"], "size": r["size"],
                      "used_top": r["start"], "load_extra": 0, "segs": []}
    for seg, s in segments.items():
        if seg in ADDR_SEGS_SKIP or s["size"] == 0:
            continue
        sr = seg_region.get(seg)
        if not sr:
            continue
        run_region = sr["run"] or sr["load"]
        if run_region in out:
            o = out[run_region]
            o["used_top"] = max(o["used_top"], s["end"] + 1)
            o["segs"].append(seg)
        if sr["run"] and sr["load"] != sr["run"] and sr["load"] in out:
            out[sr["load"]]["load_extra"] += s["size"]
            out[sr["load"]]["segs"].append(seg + "(load)")

    stack_start = symbols.get("__STACKSTART__")
    stack_size = symbols.get("__STACKSIZE__")
    rows = []
    for rname, o in out.items():
        if not o["segs"]:
            continue
        used = (o["used_top"] - o["start"]) + o["load_extra"]
        headroom = o["size"] - used
        eff = None
        if stack_start is not None and stack_size is not None:
            lo = stack_start - stack_size
            if o["start"] <= lo and stack_start <= o["start"] + o["size"]:
                eff = lo - o["used_top"]
        rows.append({"region": rname, "start": o["start"], "size": o["size"],
                     "used": used, "headroom": headroom,
                     "headroom_minus_stack": eff, "segments": o["segs"]})
    rows.sort(key=lambda r: r["start"])
    return rows


# ---------------------------------------------------------------------------
# --why: shortest reference chain root -> module
# ---------------------------------------------------------------------------

def why(modules, imports, target):
    """BFS over importer->definer edges from root objects (not in a .lib)."""
    edges, sym_def = imports["edges"], imports["sym_def"]
    if not edges:
        return None, "map has no Imports list -- relink with `ld65 -vm` (cl65 -vm)"
    members = {m["member"]: disp for disp, m in modules.items()}
    tgt = target if target in members else sym_def.get(target)
    if tgt is None or tgt not in members:
        return None, f"'{target}' is not a linked module or exported symbol"
    roots = [m["member"] for m in modules.values() if m["lib"] is None]
    prev = {r: None for r in roots}
    q = deque(roots)
    while q:
        cur = q.popleft()
        if cur == tgt:
            chain = []
            while cur is not None:
                chain.append((cur, prev[cur][1] if prev[cur] else None))
                cur = prev[cur][0] if prev[cur] else None
            return list(reversed(chain)), None
        for sym, definer in edges.get(cur, []):
            if definer not in prev and definer in members:
                prev[definer] = (cur, sym)
                q.append(definer)
    return None, f"no reference chain from a root object to '{tgt}' (linker-generated pull?)"


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------

def module_table(modules):
    rows = []
    for disp, m in modules.items():
        addr = sum(sz for seg, sz in m["segs"].items()
                   if seg not in ADDR_SEGS_SKIP and seg != "ZEROPAGE")
        zp = m["segs"].get("ZEROPAGE", 0)
        if addr or zp:
            rows.append({"module": disp, "bytes": addr, "zp": zp, "segs": m["segs"]})
    rows.sort(key=lambda r: -r["bytes"])
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("map", help="ld65 map file (-m; -vm for --why)")
    ap.add_argument("--cfg", help="linker cfg -> MEMORY regions + headroom")
    ap.add_argument("--top", type=int, default=0, help="show only the N biggest modules")
    ap.add_argument("--why", metavar="MODULE|SYMBOL",
                    help="shortest reference chain from a root object")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--min-headroom", action="append", default=None, metavar="[REGION:]N",
                    help="exit 2 if a region's effective headroom is below N bytes; "
                         "prefix with a region name (RAM:256) to gate just that region, "
                         "bare N gates every region (repeatable)")
    ap.add_argument("--label", default=None, help="report title (default: map filename)")
    args = ap.parse_args(argv)

    try:
        text = open(args.map, encoding="utf-8", errors="replace").read()
    except OSError as e:
        print(f"size_report: {e}", file=sys.stderr)
        return 1
    modules, segments, imports = parse_map(text)
    if not modules or not segments:
        print("size_report: no Modules/Segment list found -- is this an ld65 -m map?",
              file=sys.stderr)
        return 1

    regions = []
    if args.cfg:
        try:
            cfg_text = open(args.cfg, encoding="utf-8", errors="replace").read()
        except OSError as e:
            print(f"size_report: {e}", file=sys.stderr)
            return 1
        mem, seg_region, symbols = parse_cfg(cfg_text)
        regions = region_report(segments, mem, seg_region, symbols)

    if args.why:
        chain, err = why(modules, imports, args.why)
        if err:
            print(f"size_report: {err}", file=sys.stderr)
            return 1
        parts = []
        for member, via in chain:
            parts.append(member if via is None else f"-[{via}]-> {member}")
        print(" ".join(parts))
        return 0

    mods = module_table(modules)
    label = args.label or args.map

    if args.json:
        print(json.dumps({"label": label, "regions": regions, "modules": mods,
                          "segments": segments}, indent=2))
    else:
        print(f"== {label} ==")
        if regions:
            print(f"{'region':<8} {'start':>6} {'size':>7} {'used':>7} "
                  f"{'headroom':>9}  {'-stack':>7}")
            for r in regions:
                eff = "" if r["headroom_minus_stack"] is None else f"{r['headroom_minus_stack']:>7}"
                print(f"{r['region']:<8} ${r['start']:04X} {r['size']:>7} {r['used']:>7} "
                      f"{r['headroom']:>9}  {eff:>7}")
        shown = mods[: args.top] if args.top else mods
        print(f"{'bytes':>6} {'zp':>4}  module   (address-space bytes: CODE+RODATA+DATA+BSS)")
        for m in shown:
            print(f"{m['bytes']:>6} {m['zp']:>4}  {m['module']}")
        if args.top and len(mods) > args.top:
            rest = sum(m["bytes"] for m in mods[args.top:])
            print(f"{rest:>6}       ... {len(mods) - args.top} more modules")
        total = sum(m["bytes"] for m in mods)
        print(f"{total:>6}       TOTAL")

    if args.min_headroom:
        rc = 0
        for spec in args.min_headroom:
            region, _, n = spec.rpartition(":")
            try:
                bound = int(n)
            except ValueError:
                print(f"size_report: bad --min-headroom '{spec}'", file=sys.stderr)
                return 1
            for r in regions:
                if region and r["region"] != region:
                    continue
                eff = r["headroom_minus_stack"]
                worst = min(x for x in (r["headroom"], eff) if x is not None)
                if worst < bound:
                    print(f"size_report: region {r['region']} headroom {worst} B "
                          f"< --min-headroom {bound} B", file=sys.stderr)
                    rc = 2
            if region and not any(r["region"] == region for r in regions):
                print(f"size_report: --min-headroom region '{region}' not in report",
                      file=sys.stderr)
                return 1
        return rc
    return 0


if __name__ == "__main__":
    sys.exit(main())
