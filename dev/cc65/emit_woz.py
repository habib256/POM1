#!/usr/bin/env python3
"""Shared Woz-hex emitter for cc65 6502 projects.

Replaces the ~10 near-identical `emit_<PROJECT>_txt.py` scripts under
`dev/projects/`. Each project becomes a 5-line caller that imports
`emit_woz` and invokes `emit()` with project-specific parameters.

Usage as a module (typical project script):

    from emit_woz import emit
    emit(
        asm_files=["MyProject.asm"],
        lib_dirs=["apple1", "hgr"],
        cfg="apple1_gen2.cfg",          # relative to project OR cc65/
        out_bin="MyProject.bin",
        out_txt="MyProject.txt",
        out_dir_software="Graphic HGR",  # software/<dir>/
        start_addr=0x0280,
        header_lines=["// optional banner"],
    )

Usage as a CLI (one-shot projects without a custom script):

    python3 ../../cc65/emit_woz.py \\
        --asm MyProject.asm \\
        --lib apple1 --lib hgr \\
        --cfg apple1_gen2.cfg \\
        --out-software hgr \\
        --start 0x0280

Multi-module projects (e.g. TMS_Logo with TMS_Logo.asm + math.asm +
tms9918m2.asm) pass multiple `asm_files`. Sources outside the project
directory may be specified as `lib/m6502/math.asm` style relative paths
or absolute paths.

Layout assumptions (matches the repo today):
- Repo root is identified by walking up from this file (`dev/cc65/`).
- Library include paths resolve to `<root>/dev/lib/<name>/`.
- Linker config `<name>.cfg` resolves to `<project>/<name>.cfg` first
  then `<root>/dev/cc65/<name>.cfg`.
- Build outputs (.o, intermediate .bin) go to `<root>/build/`.
- Final `.bin` and `.txt` land under `<root>/software/<out_dir>/` if
  `out_dir_software` is given, otherwise next to the asm.
- Every line of `.txt` is `AAAA: BB BB ...` (8 bytes per line) followed
  by `<START>R` so File > Load Memory + Wozmon's R command Just Works.

Idempotency: re-running with no source change still produces the same
binary (cc65 is deterministic). The `.txt` is regenerated from the
`.bin`. Use `make` to gate.
"""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
from typing import Iterable, Sequence


def _find_root() -> pathlib.Path:
    """Repo root = three levels up from this file (dev/cc65/emit_woz.py)."""
    return pathlib.Path(__file__).resolve().parents[2]


def _resolve_cfg(cfg: str | pathlib.Path, project_dir: pathlib.Path,
                 root: pathlib.Path) -> pathlib.Path:
    p = pathlib.Path(cfg)
    if p.is_absolute() and p.exists():
        return p
    # Relative: try project dir, then dev/cc65/, then as-is.
    for candidate in (project_dir / p, root / "dev" / "cc65" / p, p):
        if candidate.exists():
            return candidate.resolve()
    raise FileNotFoundError(f"linker config not found: {cfg}")


def _resolve_asm(name: str, project_dir: pathlib.Path,
                 root: pathlib.Path) -> pathlib.Path:
    p = pathlib.Path(name)
    if p.is_absolute() and p.exists():
        return p
    # Relative: project dir first, then root-relative (e.g. "lib/m6502/math.asm").
    for candidate in (project_dir / p, root / "dev" / p, root / p, p):
        if candidate.exists():
            return candidate.resolve()
    raise FileNotFoundError(f"asm source not found: {name}")


def emit(
    asm_files: Sequence[str | pathlib.Path],
    lib_dirs: Sequence[str],
    cfg: str | pathlib.Path,
    out_bin: str | pathlib.Path | None = None,
    out_txt: str | pathlib.Path | None = None,
    out_dir_software: str | None = None,
    start_addr: int = 0x0280,
    header_lines: Iterable[str] = (),
    bytes_per_line: int = 8,
    project_dir: pathlib.Path | None = None,
    quiet: bool = False,
    extra_zones: Sequence[tuple[int, "str | pathlib.Path"]] = (),
) -> pathlib.Path:
    """Build a cc65 project and emit its Wozmon-hex `.txt` sidecar.

    Returns the path of the written `.txt` file.

    `lib_dirs` is a list of library names (`"apple1"`, `"hgr"`, …) — they
    are looked up in `dev/lib/<name>/` for the `-I` include path. Pass
    `lib_dirs=[]` for projects that use no library.

    `extra_zones` is a list of `(addr, path)` raw binary blobs appended to
    the `.txt` as additional hex zones (multi-zone dumps are supported by
    POM1's `Memory::loadHexDump`). Used e.g. by a1_crazycycle to bundle an
    8 KB HGR image at $2000 alongside the $E000 code — without bloating the
    flat `.bin`. Paths resolve relative to the repo root.
    """
    if project_dir is None:
        # Caller didn't specify; assume CWD or the calling script's dir.
        project_dir = pathlib.Path.cwd()
    project_dir = pathlib.Path(project_dir).resolve()
    root = _find_root()

    # Auto-link tms9918_pad.asm whenever any source references the silicon-
    # strict pad helper (injected by tools/silicon_strict_patch.py). Saves
    # every per-project emit_*_txt.py from listing lib/tms9918/tms9918_pad.asm
    # explicitly.
    pad_asm = root / "dev" / "lib" / "tms9918" / "tms9918_pad.asm"
    if pad_asm.is_file():
        already_listed = False
        needs_pad = False
        for asm in asm_files:
            try:
                src_path = _resolve_asm(asm, project_dir, root)
            except FileNotFoundError:
                continue
            if src_path.resolve() == pad_asm.resolve():
                already_listed = True
                break
            try:
                content = src_path.read_text(errors="ignore")
            except OSError:
                continue
            if ("tms9918_pad12" in content
                    or "tms9918_pad24" in content
                    or "tms9918_pad40" in content):
                needs_pad = True
        if needs_pad and not already_listed:
            asm_files = list(asm_files) + [str(pad_asm)]

    build_dir = root / "build"
    build_dir.mkdir(parents=True, exist_ok=True)

    # Resolve library include paths.
    inc_args: list[str] = []
    for lib in lib_dirs:
        lib_path = root / "dev" / "lib" / lib
        if not lib_path.is_dir():
            raise FileNotFoundError(f"lib not found: dev/lib/{lib}")
        inc_args.extend(["-I", str(lib_path)])

    # Assemble each .asm to a .o under build/.
    obj_paths: list[pathlib.Path] = []
    for asm_name in asm_files:
        asm_path = _resolve_asm(asm_name, project_dir, root)
        obj_path = build_dir / (asm_path.stem + ".o")
        cmd = ["ca65", *inc_args, "-o", str(obj_path), str(asm_path)]
        if not quiet:
            print("$ " + " ".join(cmd), file=sys.stderr)
        subprocess.run(cmd, check=True, cwd=str(root))
        obj_paths.append(obj_path)

    # Determine output paths.
    primary_stem = pathlib.Path(asm_files[0]).stem
    if out_bin is None:
        out_bin = build_dir / f"{primary_stem}.bin"
    out_bin = pathlib.Path(out_bin)
    if not out_bin.is_absolute():
        out_bin = build_dir / out_bin

    if out_txt is None:
        if out_dir_software:
            out_txt = root / "software" / out_dir_software / f"{primary_stem}.txt"
            out_bin_final = root / "software" / out_dir_software / f"{primary_stem}.bin"
        else:
            out_txt = project_dir / f"{primary_stem}.txt"
            out_bin_final = out_bin
    else:
        out_txt = pathlib.Path(out_txt)
        if not out_txt.is_absolute():
            out_txt = project_dir / out_txt
        out_bin_final = out_bin

    out_bin_final.parent.mkdir(parents=True, exist_ok=True)
    out_txt.parent.mkdir(parents=True, exist_ok=True)

    # Resolve cfg.
    cfg_path = _resolve_cfg(cfg, project_dir, root)

    # Link.
    cmd = ["ld65", "-C", str(cfg_path), "-o", str(out_bin_final),
           *(str(p) for p in obj_paths)]
    if not quiet:
        print("$ " + " ".join(cmd), file=sys.stderr)
    subprocess.run(cmd, check=True, cwd=str(root))

    # Emit .txt.
    data = out_bin_final.read_bytes()
    lines: list[str] = list(header_lines)
    addr = start_addr
    for i in range(0, len(data), bytes_per_line):
        chunk = data[i : i + bytes_per_line]
        lines.append(f"{addr:04X}: " + " ".join(f"{b:02X}" for b in chunk))
        addr += len(chunk)
    total = len(data)
    # Extra raw-binary zones (e.g. an HGR image at $2000) — loadHexDump
    # handles disjoint zones; the trailing run line still uses start_addr.
    for zone_addr, zone_path in extra_zones:
        zp = pathlib.Path(zone_path)
        if not zp.is_absolute():
            zp = root / zp
        zdata = zp.read_bytes()
        addr = zone_addr
        for i in range(0, len(zdata), bytes_per_line):
            chunk = zdata[i : i + bytes_per_line]
            lines.append(f"{addr:04X}: " + " ".join(f"{b:02X}" for b in chunk))
            addr += len(chunk)
        total += len(zdata)
    lines.append(f"{start_addr:04X}R")
    out_txt.write_text("\n".join(lines) + "\n", encoding="ascii")

    if not quiet:
        print(f"Wrote {out_txt} ({total} bytes)", file=sys.stderr)
    return out_txt


def emit_cl65(
    c_files: Sequence[str | pathlib.Path],
    extra_sources: Sequence[str | pathlib.Path] = (),
    lib_dirs: Sequence[str] = (),
    cfg: str | pathlib.Path = "",
    out_dir_software: str | None = None,
    extra_libs: Sequence[str | pathlib.Path] = (),
    start_addr: int = 0x6000,
    cflags: Sequence[str] = ("-t", "none", "-Oirs"),
    project_dir: pathlib.Path | None = None,
    quiet: bool = False,
    header_lines: Iterable[str] = (),
) -> pathlib.Path:
    """Compile + link a cc65 C project via cl65, then emit Wozmon-hex .txt.

    The companion of ``emit()``: same Wozmon-hex serialisation, but invokes
    ``cl65`` (one-shot compile + assemble + link) instead of separate
    ``ca65`` / ``ld65`` calls. Used by gen2c / tms9918c demos that mix
    C and asm sources.

    Args:
        c_files: primary sketch sources (relative to project_dir).
        extra_sources: runtime sources (apple1c, gen2c, asm blitters…),
            resolved against repo root like ``emit()``'s asm_files.
        lib_dirs: library include names (resolved to ``dev/lib/<name>/``).
        cfg: linker config (relative to project, dev/cc65/, or absolute).
        out_dir_software: ``software/<dir>/`` subfolder for final .bin/.txt.
        extra_libs: pre-built .lib archives to link (e.g. gfx-gen2.lib).
        start_addr: load address used in the trailing ``AAAA R`` line.
        cflags: extra cl65 flags (default ``-t none -Oirs``).
        project_dir: project root (default: caller's CWD).
        quiet: suppress the echoed command line.
        header_lines: comment lines to prepend to the .txt.

    Returns the path of the written ``.txt`` file.
    """
    if project_dir is None:
        project_dir = pathlib.Path.cwd()
    project_dir = pathlib.Path(project_dir).resolve()
    root = _find_root()

    inc_args: list[str] = []
    for lib in lib_dirs:
        lib_path = root / "dev" / "lib" / lib
        if not lib_path.is_dir():
            raise FileNotFoundError(f"lib not found: dev/lib/{lib}")
        inc_args.extend(["-I", str(lib_path)])

    cfg_path = _resolve_cfg(cfg, project_dir, root)

    src_paths: list[pathlib.Path] = []
    primary_stem = pathlib.Path(c_files[0]).stem
    for src in list(c_files) + list(extra_sources):
        sp = pathlib.Path(src)
        if not sp.is_absolute():
            # c_files are project-local, extras are root-or-absolute.
            for candidate in (project_dir / sp, root / "dev" / sp, root / sp, sp):
                if candidate.exists():
                    sp = candidate.resolve()
                    break
        if not sp.exists():
            raise FileNotFoundError(f"source not found: {src}")
        src_paths.append(sp)

    lib_paths: list[pathlib.Path] = []
    for archive in extra_libs:
        ap = pathlib.Path(archive)
        if not ap.is_absolute():
            for candidate in (project_dir / ap, root / "dev" / ap, root / ap, ap):
                if candidate.exists():
                    ap = candidate.resolve()
                    break
        lib_paths.append(ap)

    if out_dir_software:
        out_dir = root / "software" / out_dir_software
        out_bin = out_dir / f"{primary_stem}.bin"
        out_txt = out_dir / f"{primary_stem}.txt"
    else:
        out_bin = project_dir / f"{primary_stem}.bin"
        out_txt = project_dir / f"{primary_stem}.txt"
    out_bin.parent.mkdir(parents=True, exist_ok=True)

    cmd = ["cl65", *cflags, "-C", str(cfg_path), *inc_args,
           *(str(p) for p in src_paths), *(str(p) for p in lib_paths),
           "-o", str(out_bin)]
    if not quiet:
        print("$ " + " ".join(cmd), file=sys.stderr)
    subprocess.run(cmd, check=True, cwd=str(root))

    data = out_bin.read_bytes()
    lines: list[str] = list(header_lines)
    addr = start_addr
    for i in range(0, len(data), 8):
        chunk = data[i : i + 8]
        lines.append(f"{addr:04X}: " + " ".join(f"{b:02X}" for b in chunk))
        addr += len(chunk)
    lines.append(f"{start_addr:04X}R")
    out_txt.write_text("\n".join(lines) + "\n", encoding="ascii")
    if not quiet:
        print(f"Wrote {out_bin} ({len(data)} bytes)", file=sys.stderr)
        print(f"Wrote {out_txt}", file=sys.stderr)
    return out_txt


def _cli(argv: Sequence[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="Shared Woz-hex emitter.")
    p.add_argument("--asm", action="append", required=True,
                   help="Source .asm (repeat for multi-module projects)")
    p.add_argument("--lib", action="append", default=[],
                   help="Library include name (repeat). Resolves to dev/lib/<name>/")
    p.add_argument("--cfg", required=True,
                   help="Linker config (.cfg) — path relative to project, "
                        "dev/cc65/, or absolute")
    p.add_argument("--out-software", default=None,
                   help="software/<dir>/ subfolder for the final .bin/.txt")
    p.add_argument("--out-bin", default=None,
                   help="Override .bin path (default: <stem>.bin in software/)")
    p.add_argument("--out-txt", default=None,
                   help="Override .txt path (default: <stem>.txt in software/)")
    p.add_argument("--start", default="0x0280",
                   help="Load address (default: 0x0280)")
    p.add_argument("--header", action="append", default=[],
                   help="Header comment line for .txt (repeat)")
    p.add_argument("--bytes-per-line", type=int, default=8)
    p.add_argument("--quiet", action="store_true")
    p.add_argument("--project-dir", default=None,
                   help="Project directory (default: CWD)")
    args = p.parse_args(argv)

    start = int(args.start, 0)
    emit(
        asm_files=args.asm,
        lib_dirs=args.lib,
        cfg=args.cfg,
        out_bin=args.out_bin,
        out_txt=args.out_txt,
        out_dir_software=args.out_software,
        start_addr=start,
        header_lines=args.header,
        bytes_per_line=args.bytes_per_line,
        project_dir=args.project_dir,
        quiet=args.quiet,
    )
    return 0


if __name__ == "__main__":
    sys.exit(_cli())
