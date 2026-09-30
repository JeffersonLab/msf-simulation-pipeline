#!/usr/bin/env python3
"""parity.py — compares eic2ai outputs with the csv_convert macro outputs, role by role.

The macro output is the reference. Every CSV eic2ai writes must equal the reference byte for
byte after the header line, which is compared through the rename table (column_renames.py at
the eic2ai root: the macro-era names mapped to the unified vocabulary). Tolerated deviations:
a reference of 0 bytes (the macro wrote no row and no header) equals an eic2ai file holding
the header line alone; the columns in WAIVED_COLUMNS are compared with those columns dropped.

Usage:
  tests/parity.py --reco R.edm4eic.root [--sim S.edm4hep.root] --roles trk_hits,cal_hits,calo_clusters
                  --ref-dir DIR --out-dir DIR --eic2ai BIN [-n N] [--macro-bin DIR]
                  [--compress none,zip] [--extra-args "..."]

  --ref-dir    references <ref-dir>/<stem>.<role>.csv (made by tests/make_references.sh). With
               --macro-bin a missing reference is produced by running the macro executable.
  --out-dir    eic2ai outputs: <out-dir>/plain/ and <out-dir>/zip/, plus the run logs.
  --compress   which eic2ai modes to test (default: none,zip). Plain files are compared with
               cmp; the zip entry is streamed through zipfile and compared chunk by chunk.

Prints one line per role and mode: PASS, PASS (header-only vs 0-byte reference) or FAIL with the
first differing line and column; then wall time and MaxRSS of every run. Exit code 1 on any FAIL.
"""
import argparse
import importlib.util
import itertools
import os
import re
import subprocess
import sys
import zipfile

EIC2AI_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_spec = importlib.util.spec_from_file_location("column_renames", os.path.join(EIC2AI_DIR, "column_renames.py"))
column_renames = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(column_renames)

PAIRED_ROLES = {"cal_hits"}          # macros that take the sim file as a second input
SIM_ROLES = {"acceptance_ppim", "acceptance_npi0", "combinatorics_ppim"}   # edm4hep_<role> macros on the sim file
EXTRA_OUTPUTS = {"comb_candidates": ["events"], "acceptance_ppim": ["prot_hits", "pimin_hits"]}   # <stem>.<role>_<suffix>.csv
# Columns whose values differ from the macro by decision (QUESTIONS.md Q16): compared with these columns dropped.
WAIVED_COLUMNS = {"lambda_acceptance": {"lam_decay"}, "comb_candidates_events": {"lam_decay"}}   # new names
CHUNK = 1 << 20


def input_stem(path):
    name = os.path.basename(path)
    for suffix in (".eicrecon.edm4eic.root", ".edm4eic.root", ".edm4hep.root", ".root"):
        if name.endswith(suffix):
            return name[: -len(suffix)]
    return name


def run_timed(command, log_path):
    """Runs the command under /usr/bin/time -v; returns (exit code, wall seconds, max RSS MB)."""
    with open(log_path, "w") as log:
        result = subprocess.run(["/usr/bin/time", "-v"] + command, stdout=log, stderr=subprocess.STDOUT)
    text = open(log_path).read()
    wall = re.search(r"Elapsed \(wall clock\) time.*: (\S+)", text)
    rss = re.search(r"Maximum resident set size \(kbytes\): (\d+)", text)
    seconds = 0.0
    if wall:
        parts = [float(p) for p in wall.group(1).split(":")]
        seconds = sum(p * 60 ** i for i, p in enumerate(reversed(parts)))
    return result.returncode, seconds, (int(rss.group(1)) / 1024 if rss else 0.0)


def ensure_reference(args, stem, role):
    ref = os.path.join(args.ref_dir, f"{stem}.{role}.csv")
    if os.path.exists(ref + ".done") or (os.path.exists(ref) and not args.macro_bin):
        return ref, None
    if not args.macro_bin:
        sys.exit(f"reference missing: {ref} (run tests/make_references.sh or give --macro-bin)")
    os.makedirs(args.ref_dir, exist_ok=True)
    prefix = "edm4hep" if role in SIM_ROLES else "edm4eic"
    command = [os.path.join(args.macro_bin, f"{prefix}_{role}")]
    if args.events:
        command += ["-n", str(args.events)]
    command += ["-o", ref, args.sim if role in SIM_ROLES else args.reco]
    if role in PAIRED_ROLES:
        command.append(args.sim)
    code, seconds, rss = run_timed(command, ref + ".log")
    if code != 0:
        sys.exit(f"macro edm4eic_{role} failed (exit {code}); see {ref}.log")
    open(ref + ".done", "w").close()
    return ref, (seconds, rss)


def expected_header(output_name, ref_header_line):
    """The reference header line translated to the eic2ai vocabulary."""
    names = ref_header_line.rstrip("\r\n").split(",")
    return ",".join(column_renames.translate(output_name, names))


def first_difference(ref_lines, out_lines):
    """(line number, column number, reference line, output line) of the first mismatch after the header."""
    for line_number, (ref_line, out_line) in enumerate(itertools.zip_longest(ref_lines, out_lines), start=2):
        if ref_line != out_line:
            ref_cols = (ref_line or "").rstrip("\n").split(",")
            out_cols = (out_line or "").rstrip("\n").split(",")
            column = next((i + 1 for i, (a, b) in enumerate(itertools.zip_longest(ref_cols, out_cols)) if a != b), 0)
            return line_number, column, ref_line, out_line
    return None


def compare_with_waivers(ref_path, open_output, waived, output_name):
    """Column-wise comparison with the waived columns dropped; returns (verdict, detail)."""
    import csv, io
    with open(ref_path, "r", newline="") as ref_file, open_output() as out:
        out_text = out if isinstance(out, io.TextIOBase) else io.TextIOWrapper(out, encoding="utf-8", newline="")
        ref_rows = list(csv.reader(ref_file))
        out_rows = list(csv.reader(out_text))
    if not ref_rows:
        return ("PASS", "header-only vs 0-byte reference") if len(out_rows) == 1 else ("FAIL", "reference is empty, output has rows")
    ref_rows[0] = column_renames.translate(output_name, ref_rows[0])
    if ref_rows[0] != out_rows[0]:
        return "FAIL", f"header differs: ref={ref_rows[0]} out={out_rows[0]}"
    keep = [i for i, name in enumerate(ref_rows[0]) if name not in waived]
    waived_index = [i for i, name in enumerate(ref_rows[0]) if name in waived]
    if len(ref_rows) != len(out_rows):
        return "FAIL", f"{len(ref_rows)} reference lines, {len(out_rows)} output lines"
    changed = 0
    for line_number, (ref_row, out_row) in enumerate(zip(ref_rows[1:], out_rows[1:]), start=2):
        if [ref_row[i] for i in keep] != [out_row[i] for i in keep]:
            column = next(i + 1 for i in keep if ref_row[i] != out_row[i])
            return "FAIL", f"line {line_number} column {column}: ref={','.join(ref_row)!r} out={','.join(out_row)!r}"
        if any(ref_row[i] != out_row[i] for i in waived_index):
            changed += 1
    return "PASS", f"waived {sorted(waived)}: {changed} of {len(ref_rows) - 1} rows differ there"


def compare_streams(ref_path, open_output, output_name):
    """Byte comparison of the reference file with a stream, the header through the rename table; returns (verdict, detail)."""
    ref_size = os.path.getsize(ref_path)
    with open(ref_path, "rb") as ref, open_output() as out:
        if ref_size == 0:
            content = out.read()
            lines = content.split(b"\n")
            if len(lines) == 2 and lines[1] == b"" and lines[0]:
                return "PASS", "header-only vs 0-byte reference"
            return "FAIL", f"reference is empty, output has {len(content)} bytes"
        expected = expected_header(output_name, ref.readline().decode("utf-8"))
        actual = out.readline().decode("utf-8").rstrip("\r\n")
        if actual != expected:
            return "FAIL", f"header: expected {expected!r} got {actual!r}"
        while True:
            a = ref.read(CHUNK)
            b = out.read(CHUNK)
            if a != b:
                break
            if not a:
                return "PASS", ""
    # Locate the first differing line for the report (text pass over both, headers skipped).
    with open(ref_path, "r", errors="replace") as ref, open_output() as out:
        out_text = (line.decode("utf-8", "replace") for line in out) if "b" in getattr(out, "mode", "rb") else out
        next(ref, None)
        next(iter(out_text), None) if not hasattr(out_text, "readline") else out_text.readline()
        difference = first_difference(ref, out_text)
    if difference is None:
        return "FAIL", "byte difference not located line-wise (line endings?)"
    line_number, column, ref_line, out_line = difference
    return "FAIL", f"line {line_number} column {column}: ref={ref_line!r} out={out_line!r}"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--reco", required=True)
    parser.add_argument("--sim")
    parser.add_argument("--roles", required=True)
    parser.add_argument("--ref-dir", required=True)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--eic2ai", required=True)
    parser.add_argument("--macro-bin")
    parser.add_argument("-n", "--events", type=int, default=0)
    parser.add_argument("--compress", default="none,zip")
    parser.add_argument("--extra-args", default="", help="extra eic2ai arguments, e.g. '--progress 0'")
    parser.add_argument("--sim-only", action="store_true", help="run eic2ai with --sim alone (the reco file names the stem only)")
    args = parser.parse_args()

    roles = [r for r in args.roles.split(",") if r]
    modes = [m for m in args.compress.split(",") if m]
    stem = input_stem(args.reco)
    os.makedirs(args.out_dir, exist_ok=True)
    timings = []
    verdicts = []

    for role in roles:
        ref, timing = ensure_reference(args, stem, role)
        if timing:
            timings.append((f"macro {role}",) + timing)
        for suffix in EXTRA_OUTPUTS.get(role, []):
            extra_ref = os.path.join(args.ref_dir, f"{stem}.{role}_{suffix}.csv")
            if not os.path.exists(extra_ref):
                sys.exit(f"reference missing: {extra_ref}")

    for mode in modes:
        mode_dir = os.path.join(args.out_dir, "plain" if mode == "none" else mode)
        command = [args.eic2ai, "--out-dir", mode_dir, "--writers", ",".join(roles), "--compress", mode]
        if args.reco and not args.sim_only:
            command += ["--reco", args.reco]
        if args.sim:
            command += ["--sim", args.sim]
        if args.events:
            command += ["-n", str(args.events)]
        command += args.extra_args.split()
        code, seconds, rss = run_timed(command, os.path.join(args.out_dir, f"eic2ai_{mode}.log"))
        timings.append((f"eic2ai {mode} ({','.join(roles)})", seconds, rss))
        if code != 0:
            for role in roles:
                verdicts.append((role, mode, "FAIL", f"eic2ai exit {code}; see {args.out_dir}/eic2ai_{mode}.log"))
            continue

        outputs = []
        for role in roles:
            outputs.append((role, f"{stem}.{role}.csv"))
            for suffix in EXTRA_OUTPUTS.get(role, []):
                outputs.append((f"{role}_{suffix}", f"{stem}.{role}_{suffix}.csv"))
        for role, csv_name in outputs:
            ref = os.path.join(args.ref_dir, csv_name)
            if mode == "none":
                out_path = os.path.join(mode_dir, csv_name)
                if not os.path.exists(out_path):
                    verdicts.append((role, mode, "FAIL", f"missing {out_path}"))
                    continue
                if role in WAIVED_COLUMNS:
                    verdict, detail = compare_with_waivers(ref, lambda p=out_path: open(p, "r", newline=""), WAIVED_COLUMNS[role], role)
                else:
                    verdict, detail = compare_streams(ref, lambda p=out_path: open(p, "rb"), role)
            elif mode == "zip":
                out_path = os.path.join(mode_dir, csv_name + ".zip")
                if not os.path.exists(out_path):
                    verdicts.append((role, mode, "FAIL", f"missing {out_path}"))
                    continue
                with zipfile.ZipFile(out_path) as archive:
                    names = archive.namelist()
                    bad = archive.testzip()
                if bad is not None or names != [csv_name]:
                    verdicts.append((role, mode, "FAIL", f"zip entries {names}, testzip {bad!r}; expected one entry {csv_name}"))
                    continue
                if role in WAIVED_COLUMNS:
                    verdict, detail = compare_with_waivers(ref, lambda p=out_path, n=csv_name: zipfile.ZipFile(p).open(n), WAIVED_COLUMNS[role], role)
                else:
                    verdict, detail = compare_streams(ref, lambda p=out_path, n=csv_name: zipfile.ZipFile(p).open(n), role)
            else:
                verdicts.append((role, mode, "SKIP", f"mode {mode} is not compared"))
                continue
            verdicts.append((role, mode, verdict, detail))

    print(f"\nparity: stem={stem} events={args.events or 'all'}")
    for role, mode, verdict, detail in verdicts:
        print(f"  {verdict:<4} {role:<14} {mode:<5} {detail}")
    print("timings:")
    for name, seconds, rss in timings:
        print(f"  {name:<40} {seconds:8.1f} s  {rss:8.0f} MB MaxRSS")
    failed = any(v[2] == "FAIL" for v in verdicts)
    print("RESULT:", "FAIL" if failed else "PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
