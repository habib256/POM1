#!/usr/bin/env python3
"""Build GEN2Bounces (C / GEN2 HGR — vector primitives + HUD + double buffer)
and emit the Woz-hex sidecar.

Thin wrapper around dev/cc65/emit_woz.emit_cl65.
"""
import pathlib
import sys

PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[2] / "cc65"))
from emit_woz import emit_cl65  # noqa: E402


def main() -> int:
    GEN2C = "lib/gen2c"
    APPLE1C = "lib/apple1c"
    GFX = "lib/gfx"
    GFXLIB = pathlib.Path(GFX) / "gfx-gen2.lib"

    root = PROJ.parents[3]
    if not (root / "dev" / GFXLIB).is_file():
        import subprocess
        subprocess.run(["make", "-C", str(root / "dev" / GFX), "gen2"], check=True)

    emit_cl65(
        c_files=["GEN2Bounces.c"],
        extra_sources=[
            f"{GEN2C}/gen2_init.c",
            f"{GEN2C}/gen2_pixel.c",
            f"{GEN2C}/gen2_rect.c",
            f"{GEN2C}/gen2_text.c",
            f"{GEN2C}/gen2_sprites.c",
            f"{GEN2C}/gen2_geom.c",
            f"{GEN2C}/gen2_lores.c",
            f"{GEN2C}/gen2_blit.s",
            f"{APPLE1C}/apple1io.c",
            f"{APPLE1C}/apple1io_asm.s",
        ],
        lib_dirs=["gen2c", "apple1c", "gfx"],
        cfg="apple1_gen2_c.cfg",
        out_dir_software="Graphic HGR",
        extra_libs=[f"dev/{GFXLIB}"],
        start_addr=0x6000,
        project_dir=PROJ,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
