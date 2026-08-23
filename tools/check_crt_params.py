#!/usr/bin/env python3
"""check_crt_params.py — pin the CRT effect stack's three hand-kept copies.

Twelve floats and one enum drive POM1's universal CRT post-process, and they
exist in THREE places that nothing was checking against each other:

  1. src/CrtParams.h            — the C++ struct the UI edits and ini/ persists
  2. src/CrtEffectStack.cpp     — the GLSL 150 / 300-es shader (uBrightness…)
  3. src/CrtEffectStackMetal.mm — the MSL shader (brightness…)

CLAUDE.md states the rule outright — "a knob added to one must be added to the
other or macOS silently diverges" — and until now nothing enforced it: no test
covered the CRT stack at all. The names do not correspond one-to-one either
(shadowMaskStrength -> uShadowStrength -> shadowStrength), so the mapping table
below IS the missing documentation as much as it is the check.

Checked per knob, in both directions:
  * declared   — the GLSL `uniform` exists, and the MSL struct field exists
                 (in BOTH MSL structs: the C++ mirror CrtUniformsMetal, whose
                 static_assert pins its size, and the shader-side CrtUniforms)
  * fed        — the C++ upload actually reads `params.<field>` / `params_.<field>`.
                 A uniform that is declared but never written is exactly the
                 silent divergence this guard exists to catch.
  * no strays  — every `u*` uniform and every MSL struct field maps back to a
                 CrtParams field, modulo the known non-knob plumbing below.

Exit 0 = in sync. Exit 1 = drift (prints what and where). Run from anywhere:
paths resolve against the repo root inferred from this file's location.
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PARAMS = REPO / "src" / "CrtParams.h"
GLSL = REPO / "src" / "CrtEffectStack.cpp"
MSL = REPO / "src" / "CrtEffectStackMetal.mm"

# CrtParams field -> (GLSL uniform name, MSL struct field name).
# Everything follows `foo` -> `uFoo` / `foo` except the one historical mismatch.
RENAMED = {"shadowMaskStrength": ("uShadowStrength", "shadowStrength")}

# Uniforms / struct fields that are plumbing, not knobs: geometry the pass needs
# and the samplers. They have no CrtParams counterpart by design.
GLSL_NON_KNOB = {"uSrc", "uPrev", "uSrcSize", "uOutSize"}
MSL_NON_KNOB = {"srcSize", "outSize", "pad0", "pad1", "pad2"}


def die(problems):
    print("check_crt_params: CRT knob sets have DRIFTED\n", file=sys.stderr)
    for p in problems:
        print(f"  - {p}", file=sys.stderr)
    print(
        "\nThe three copies (CrtParams.h / CrtEffectStack.cpp GLSL / "
        "CrtEffectStackMetal.mm MSL)\nmust carry the same knobs, or the Metal "
        "backend silently renders a different picture\nfrom the GL one. See "
        "tools/check_crt_params.py for the name mapping.",
        file=sys.stderr,
    )
    sys.exit(1)


def main():
    for f in (PARAMS, GLSL, MSL):
        if not f.is_file():
            die([f"missing source file: {f.relative_to(REPO)}"])

    params_src = PARAMS.read_text(encoding="utf-8")
    glsl_src = GLSL.read_text(encoding="utf-8")
    msl_src = MSL.read_text(encoding="utf-8")

    # -- 1. the knob list, straight out of the struct ------------------------
    body = re.search(r"struct CrtParams\s*\{(.*?)\n\};", params_src, re.S)
    if not body:
        die(["could not locate `struct CrtParams { ... };` in CrtParams.h"])
    fields = []
    for m in re.finditer(
        r"^\s*(?:float|int|ShadowMask)\s+(\w+)\s*=", body.group(1), re.M
    ):
        fields.append(m.group(1))
    if not fields:
        die(["parsed CrtParams but found no knobs — has the struct changed shape?"])

    problems = []

    # -- 2. forward: every knob is declared and fed on both backends ---------
    glsl_uniforms = set(
        re.findall(r"uniform\s+\w+\s+(\w+)\s*;", glsl_src)
    )
    msl_mirror = re.search(r"struct CrtUniformsMetal\s*\{(.*?)\n\};", msl_src, re.S)
    msl_shader = re.search(r"struct CrtUniforms\s*\{(.*?)\n\};", msl_src, re.S)
    if not msl_mirror or not msl_shader:
        die(["could not locate both MSL structs (CrtUniformsMetal / CrtUniforms)"])
    def struct_fields(block):
        """Field names of one struct body, comments and array bounds stripped.

        The MSL structs are heavily annotated (`float hue; // -0.5..+0.5 -> ...`),
        so the trailing comment must go BEFORE the declarator is split on commas
        — otherwise every comment word is read as another field name.
        """
        names = set()
        for line in block.split("\n"):
            line = re.sub(r"//.*$", "", line).strip()
            m = re.match(r"^(?:float|int|float2)\s+(.*);$", line)
            if not m:
                continue
            for decl in m.group(1).split(","):          # `int pad0, pad1, pad2;`
                name = decl.strip().split("[")[0].strip()  # `float srcSize[2];`
                if name:
                    names.add(name)
        return names

    mirror_fields = struct_fields(msl_mirror.group(1))
    shader_fields = struct_fields(msl_shader.group(1))

    for f in fields:
        g_name, m_name = RENAMED.get(f, ("u" + f[0].upper() + f[1:], f))

        if g_name not in glsl_uniforms:
            problems.append(
                f"CrtParams.{f}: no `uniform ... {g_name}` in CrtEffectStack.cpp (GLSL)"
            )
        if not re.search(rf"\bparams\.{f}\b", glsl_src):
            problems.append(
                f"CrtParams.{f}: declared in GLSL but never uploaded "
                f"(no `params.{f}` in CrtEffectStack.cpp)"
            )
        if m_name not in mirror_fields:
            problems.append(
                f"CrtParams.{f}: no `{m_name}` in CrtUniformsMetal "
                f"(C++ mirror, CrtEffectStackMetal.mm)"
            )
        if m_name not in shader_fields:
            problems.append(
                f"CrtParams.{f}: no `{m_name}` in CrtUniforms (MSL shader struct)"
            )
        if not re.search(rf"\bparams_?\.{f}\b", msl_src):
            problems.append(
                f"CrtParams.{f}: declared in MSL but never uploaded "
                f"(no `params_.{f}` in CrtEffectStackMetal.mm)"
            )

    # -- 3. reverse: no shader-side knob without a CrtParams field -----------
    expected_glsl = {
        RENAMED.get(f, ("u" + f[0].upper() + f[1:], f))[0] for f in fields
    } | GLSL_NON_KNOB
    for u in sorted(glsl_uniforms - expected_glsl):
        problems.append(
            f"GLSL uniform `{u}` has no CrtParams field "
            f"(add it to CrtParams.h + the MSL shader, or to GLSL_NON_KNOB here)"
        )

    expected_msl = {
        RENAMED.get(f, ("u" + f[0].upper() + f[1:], f))[1] for f in fields
    } | MSL_NON_KNOB
    for u in sorted((mirror_fields | shader_fields) - expected_msl):
        problems.append(
            f"MSL field `{u}` has no CrtParams field "
            f"(add it to CrtParams.h + the GLSL shader, or to MSL_NON_KNOB here)"
        )

    # -- 4. the two MSL structs must agree with each other -------------------
    for u in sorted(mirror_fields ^ shader_fields):
        problems.append(
            f"MSL field `{u}` is in only ONE of CrtUniformsMetal / CrtUniforms — "
            f"the C++ mirror and the shader struct must match byte for byte"
        )

    if problems:
        die(problems)

    print(
        f"check_crt_params: OK — {len(fields)} CRT knobs in sync across "
        f"CrtParams.h, GLSL and MSL"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
