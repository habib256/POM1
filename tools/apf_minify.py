#!/usr/bin/env python3
"""apf_minify.py — shrink an Applesoft .apf listing while keeping it runnable on
real Apple hardware (and on POM1's GEN2 / TMS9918 + CodeTank Applesoft builds).

Three size optimisations, all valid on a genuine Apple II / Apple-1:

  1. Strip REM lines and bare ":" lines — but ONLY when no GOTO/GOSUB/THEN/ON
     targets them (a jump-target scan runs first; targeted lines are kept).
  2. Strip leading indentation. Spaces before the line number are dropped by
     Applesoft's line-number scan anyway, so this costs nothing semantically; it
     mainly shrinks the keystroke stream the POM1 Bench injects.
  3. Pack consecutive DATA statements into fewer lines joined by ":DATA ", bounded
     by a 127-char line limit (the Apple-1 WOZ GETLN input buffer; real Apple II
     allows 255). READ walks DATA across lines and statements identically, and
     DATA line numbers are never jump/RESTORE targets, so this is safe. Each
     packed line reuses one of the original DATA line numbers, so numbering stays
     ascending and every preserved GOTO/GOSUB target keeps its line number.

Numbers and program logic are never altered, so the minified program renders the
same image / behaves identically. Usage:

    python3 tools/apf_minify.py IN.apf [-o OUT.apf] [--max-line N]

With no -o, writes IN.min.apf next to the input.
"""
import argparse
import re
import sys

# Keywords that are followed by a line number (or a comma-list of them).
_JUMP_RE = re.compile(r'\b(?:GOTO|GOSUB|THEN)\b\s*([0-9][0-9,\s]*)', re.IGNORECASE)
# A program line: optional leading spaces, a line number, the rest of the line.
_LINE_RE = re.compile(r'^\s*([0-9]+)\s?(.*)$')


def parse_lines(text):
    """Return a list of (lineno:int, body:str) for each numbered BASIC line.
    Non-numbered / blank source lines are skipped (the .apf convention double-
    spaces with blank lines between statements)."""
    out = []
    for raw in text.splitlines():
        m = _LINE_RE.match(raw)
        if not m:
            continue
        out.append((int(m.group(1)), m.group(2).rstrip()))
    return out


def collect_targets(lines):
    """Every line number referenced by a GOTO/GOSUB/THEN/ON..GOTO/GOSUB. Over-
    collecting is safe (we only ever KEEP extra lines); under-collecting would be
    dangerous, so we scan the whole body including any string text."""
    targets = set()
    for _, body in lines:
        for m in _JUMP_RE.finditer(body):
            for tok in m.group(1).replace(' ', '').split(','):
                if tok.isdigit():
                    targets.add(int(tok))
    return targets


def is_rem_or_empty(body):
    stripped = body.strip()
    if stripped == '' or stripped == ':':
        return True
    # leading REM (optionally after stray colons)
    return bool(re.match(r'^:*\s*REM\b', stripped, re.IGNORECASE))


def data_payload(body):
    """If the line body is a single DATA statement, return its payload (text after
    'DATA '); else None. Only single-statement DATA lines are packed, to avoid
    disturbing mixed lines."""
    m = re.match(r'^\s*DATA\s?(.*)$', body, re.IGNORECASE)
    if not m:
        return None
    # refuse if it contains another statement separator outside quotes — keep it
    # simple: SteveJobs-style DATA is pure numbers, no ':' or quotes.
    if ':' in m.group(1) or '"' in m.group(1):
        return None
    return m.group(1).strip()


def minify(text, max_line=127):
    lines = parse_lines(text)
    targets = collect_targets(lines)

    # Pass 1: drop non-target REM / empty lines.
    kept = [(n, b) for (n, b) in lines if not (is_rem_or_empty(b) and n not in targets)]

    # Pass 2: pack runs of consecutive, non-target, single-statement DATA lines.
    out = []
    i = 0
    while i < len(kept):
        n, b = kept[i]
        payload = data_payload(b)
        if payload is None or n in targets:
            out.append((n, b))
            i += 1
            continue
        # start a packed line at this line number
        cur_no = n
        cur_text = 'DATA ' + payload
        i += 1
        while i < len(kept):
            n2, b2 = kept[i]
            if n2 in targets:
                break
            p2 = data_payload(b2)
            if p2 is None:
                break
            cand = cur_text + ':DATA ' + p2
            if len(str(cur_no)) + 1 + len(cand) > max_line:
                break
            cur_text = cand
            i += 1
        out.append((cur_no, cur_text))

    # Emit. One numbered line per row, no indentation, no blank lines.
    return '\n'.join(f'{n} {b}' for (n, b) in out) + '\n'


def main(argv):
    ap = argparse.ArgumentParser(description='Minify an Applesoft .apf listing.')
    ap.add_argument('input')
    ap.add_argument('-o', '--output')
    ap.add_argument('--max-line', type=int, default=127,
                    help='max chars per line incl. line number (Apple-1 GETLN = 127)')
    args = ap.parse_args(argv)

    with open(args.input, 'r', newline='') as f:
        src = f.read()
    out = minify(src, args.max_line)

    dst = args.output or re.sub(r'\.apf$', '', args.input) + '.min.apf'
    with open(dst, 'w') as f:
        f.write(out)

    longest = max((len(l) for l in out.splitlines()), default=0)
    print(f'{args.input}: {len(src)} bytes -> {dst}: {len(out)} bytes '
          f'({len(src) - len(out)} saved); longest line {longest} chars')
    if longest > args.max_line:
        print(f'WARNING: longest line {longest} > {args.max_line}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
