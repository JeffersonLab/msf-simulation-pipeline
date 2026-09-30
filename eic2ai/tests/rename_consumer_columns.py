#!/usr/bin/env python3
"""rename_consumer_columns.py — rewrites macro-era column names in a consumer script, notebook or document.

Usage:
  tests/rename_consumer_columns.py FILE --roles ROLE[,ROLE...] [--dry-run] [--generic]

Applies OLD_TO_NEW of column_renames.py for the given roles (their union; a conflict between roles
is an error) to:
  - quoted names: "lam_px", 'lam_px'; in Markdown also `lam_px`;
  - attribute access: df.lam_px (names with an underscore only, so df.x or df.time stay);
  - prefix-built names: f"{p}_px", "{}_px", "{0}_px" (the suffix after the brace);
  - notebooks (.ipynb): the source of every cell, JSON rewritten with the standard indent.
Generic names (event, x, y, z, time, id, px, py, pz, charge, energy, mass, pdg, p, theta, eta, phi,
detector, ...) are replaced in quoted form only, and only with --generic, because they are also
ordinary variable names. Prints every replacement as "name → name (count)"; --dry-run prints alone.
"""
import argparse
import importlib.util
import json
import os
import re
import sys

EIC2AI_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_spec = importlib.util.spec_from_file_location("column_renames", os.path.join(EIC2AI_DIR, "column_renames.py"))
column_renames = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(column_renames)

GENERIC = {"event", "x", "y", "z", "time", "id", "idx", "coll", "px", "py", "pz", "p", "theta", "eta", "phi",
           "charge", "energy", "mass", "pdg", "type", "detector", "quality", "ndf", "nmeas", "decay",
           "ref_x", "ref_y", "ref_z", "hit_id", "eDep", "pathLength", "pid_goodness",
           "n_clusters", "n_tracks", "n_particles", "n_particle_ids", "n_cluster_hits", "n_track_measurements", "n_tracker_hits"}


def merged_table(roles):
    table = {}
    for role in roles:
        for old, new in column_renames.OLD_TO_NEW[role].items():
            if old in table and table[old] != new:
                sys.exit(f"conflict for {old!r}: {table[old]!r} (earlier role) against {new!r} ({role})")
            table[old] = new
    return table


def suffix_table(table):
    """old suffix → new suffix for names built as <prefix>_<suffix> in the roles' blocks."""
    suffixes = {}
    for old, new in table.items():
        if "_" in old and "_" in new:
            old_prefix, old_suffix = old.split("_", 1)
            if new.startswith(old_prefix + "_"):
                suffixes.setdefault(old_suffix, new[len(old_prefix) + 1:])
    return suffixes


def rewrite(text, table, generic, markdown):
    counts = {}

    def count(old, new, n):
        if n:
            counts[(old, new)] = counts.get((old, new), 0) + n

    for old, new in sorted(table.items(), key=lambda kv: -len(kv[0])):
        if old in GENERIC and not generic:
            continue
        quotes = ['"', "'"] + (["`"] if markdown else [])
        for quote in quotes:
            text, n = re.subn(re.escape(quote + old + quote), quote + new + quote, text)
            count(old, new, n)
        if "_" in old and old not in GENERIC:
            text, n = re.subn(r"(?<=\.)" + re.escape(old) + r"\b", new, text)
            count(old, new, n)
            if markdown:
                text, n = re.subn(r"(?<![\w`])" + re.escape(old) + r"(?![\w`])", new, text)
                count(old, new, n)
    for old_suffix, new_suffix in suffix_table(table).items():
        pattern = r"(\{[^{}]*\})_" + re.escape(old_suffix) + r"(?=[\"'`\]\s,)])"
        text, n = re.subn(pattern, lambda m: m.group(1) + "_" + new_suffix, text)
        count("{}_" + old_suffix, "{}_" + new_suffix, n)
    return text, counts


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("file")
    parser.add_argument("--roles", required=True)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--generic", action="store_true")
    args = parser.parse_args()
    table = merged_table([r for r in args.roles.split(",") if r])
    markdown = args.file.endswith(".md")

    if args.file.endswith(".ipynb"):
        notebook = json.load(open(args.file))
        total = {}
        for cell in notebook.get("cells", []):
            source = "".join(cell.get("source", []))
            new_source, counts = rewrite(source, table, args.generic, False)
            for key, n in counts.items():
                total[key] = total.get(key, 0) + n
            if new_source != source:
                cell["source"] = new_source.splitlines(keepends=True)
        if not args.dry_run and total:
            with open(args.file, "w") as out:
                json.dump(notebook, out, indent=1, ensure_ascii=False)
                out.write("\n")
        counts = total
    else:
        text = open(args.file).read()
        new_text, counts = rewrite(text, table, args.generic, markdown)
        if not args.dry_run and new_text != text:
            open(args.file, "w").write(new_text)

    for (old, new), n in sorted(counts.items()):
        print(f"{args.file}: {old} → {new} ({n})")
    if not counts:
        print(f"{args.file}: no macro-era names of {args.roles}")


if __name__ == "__main__":
    main()
