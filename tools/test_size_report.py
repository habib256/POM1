#!/usr/bin/env python3
"""ctest driver: size_report_smoke — pins tools/size_report.py AND the
DevBench archive-link model end-to-end.

Rebuilds the GEN2-C runtime archive exactly like the DevBench does (module
list read from dev/bench/gen2c.json — so this also pins the spec: every
listed source must exist and compile), links a minimal text-only program
against it with a verbose map, then asserts:

  1. dead-strip works: unused families (gen2_lores / gen2_sprengine /
     gen2_preshift) are NOT in the linked module list;
  2. referenced families ARE there (gen2_text via gen2_hgr_puts);
  3. size_report --cfg computes a positive RAM headroom (both raw and
     minus the C-stack window);
  4. size_report --why answers with a chain rooted at the user object;
  5. the --min-headroom CI gate passes at 256 B and fails at an absurd bound.

Skips (exit 77) without cl65 + ar65 (PATH or POM1_CC65_DIR) — same gating
convention as the other cc65-dependent tests.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZE_REPORT = os.path.join(REPO, "tools", "size_report.py")

MAIN_C = """\
#include "gen2.h"
void main(void)
{
    gen2_hgr_init_clear();
    gen2_hgr_puts(2, 2, "SIZE REPORT SMOKE");
    for (;;) {}
}
"""


def which_tool(name):
    d = os.environ.get("POM1_CC65_DIR")
    if d:
        p = os.path.join(d, name + (".exe" if os.name == "nt" else ""))
        if os.path.isfile(p):
            return p
    return shutil.which(name)


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        sys.stderr.write("FAILED: " + " ".join(cmd) + "\n" + r.stdout + r.stderr)
        sys.exit(1)
    return r


def main():
    cl65, ar65 = which_tool("cl65"), which_tool("ar65")
    if not cl65 or not ar65:
        print("SKIP: cl65/ar65 not found (PATH or POM1_CC65_DIR)")
        return 77

    spec_path = os.path.join(REPO, "dev", "bench", "gen2c.json")
    spec = json.load(open(spec_path, encoding="utf-8"))
    cfg = os.path.join(REPO, spec["cfg"].lstrip("/"))
    inc_dirs = [os.path.join(REPO, d.lstrip("/")) for d in spec["incDirs"]]
    defines = spec.get("defines", [])
    sources = [os.path.join(REPO, s["path"].lstrip("/"))
               for s in spec.get("cSources", []) + spec.get("asmSources", [])]
    missing = [s for s in sources if not os.path.isfile(s)]
    if missing:
        sys.stderr.write("spec lists missing sources:\n  " + "\n  ".join(missing) + "\n")
        return 1

    flags = ["-t", "none", "-Oirs"]
    for d in defines:
        flags += ["-D" + d]
    incs = []
    for d in inc_dirs:
        incs += ["-I", d]

    tmp = tempfile.mkdtemp(prefix="pom1_size_report_")
    try:
        # 1) archive the runtime exactly like benchEnsureRtLib / buildC do
        objs = []
        for src in sources:
            obj = os.path.join(tmp, os.path.splitext(os.path.basename(src))[0] + ".o")
            run([cl65] + flags + ["-c"] + incs + ["-o", obj, src])
            objs.append(obj)
        rtlib = os.path.join(tmp, "gen2c_rt.lib")
        run([ar65, "a", rtlib] + objs)

        # 2) link the minimal program with a verbose map
        main_c = os.path.join(tmp, "main_small.c")
        open(main_c, "w", encoding="utf-8").write(MAIN_C)
        mapfile = os.path.join(tmp, "main_small.map")
        binfile = os.path.join(tmp, "main_small.bin")
        run([cl65] + flags + ["-C", cfg] + incs +
            [main_c, rtlib, "-vm", "-m", mapfile, "-o", binfile])

        # 3) size_report --json: dead-strip + headroom assertions
        r = run([sys.executable, SIZE_REPORT, mapfile, "--cfg", cfg, "--json"])
        rep = json.loads(r.stdout)
        linked = {m["module"].split(" ")[0] for m in rep["modules"]}
        for absent in ("gen2_lores.o", "gen2_sprengine.o", "gen2_preshift.o"):
            assert absent not in linked, f"{absent} linked despite being unused (dead-strip broken)"
        assert "gen2_text.o" in linked, "gen2_text.o missing (gen2_hgr_puts is called)"
        ram = next((x for x in rep["regions"] if x["region"] == "RAM"), None)
        assert ram, "RAM region not found in cfg report"
        assert ram["headroom"] > 0, f"RAM headroom not positive: {ram}"
        assert ram["headroom_minus_stack"] is not None and ram["headroom_minus_stack"] > 0, \
            f"stack-adjusted headroom missing/negative: {ram}"
        bin_size = os.path.getsize(binfile)
        print(f"PASS dead-strip + headroom (bin={bin_size} B, RAM used={ram['used']} B, "
              f"headroom={ram['headroom']} B, minus stack={ram['headroom_minus_stack']} B)")

        # 4) --why: chain rooted at the user object
        r = run([sys.executable, SIZE_REPORT, mapfile, "--why", "gen2_text.o"])
        assert r.stdout.strip().startswith("main_small.o"), f"--why chain not rooted: {r.stdout}"
        print("PASS --why:", r.stdout.strip())

        # 5) --min-headroom gate: passes at RAM:256, fails at an absurd bound
        run([sys.executable, SIZE_REPORT, mapfile, "--cfg", cfg, "--min-headroom", "RAM:256"])
        r = subprocess.run([sys.executable, SIZE_REPORT, mapfile, "--cfg", cfg,
                            "--min-headroom", "RAM:999999"], capture_output=True, text=True)
        assert r.returncode == 2, f"--min-headroom gate did not trip (rc={r.returncode})"
        print("PASS --min-headroom gate")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
