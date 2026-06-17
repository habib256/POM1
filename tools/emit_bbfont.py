#!/usr/bin/env python3
"""DEPRECATED shim — superseded by tools/build_shared_font.py.

emit_bbfont.py only emitted the HGR slice (dev/lib/gen2c/gen2_bbfont.inc). The
unified generator build_shared_font.py emits that *and* the TMS9918 pattern
tables (bbfont_tms.inc, font_hud8x8.inc) from the same Beautiful Boot glyph
master — "one master → emitters per format", the same pattern as
tools/build_hgr_sprites.py.

This shim forwards to it (HGR target only) so existing invocations keep working.
Prefer:  python3 tools/build_shared_font.py
"""
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
print("[emit_bbfont.py] deprecated — forwarding to build_shared_font.py --only hgr",
      file=sys.stderr)
raise SystemExit(subprocess.call(
    [sys.executable, str(HERE / "build_shared_font.py"), "--only", "hgr", *sys.argv[1:]]))
