#!/usr/bin/env python3
"""check_columns.py — enforces the column vocabulary (acceptance rule 3) on every header eic2ai prints.

Usage:
  tests/check_columns.py --eic2ai BIN          reads `eic2ai --list-columns`
  tests/check_columns.py --headers FILE        reads a saved `--list-columns` output
  add --markdown to print the headers as a Markdown table for eic2ai-space/notes/COLUMN_NAMES.md

Every column name must be a whole name from WHOLE_NAMES, or a prefix from PREFIXES followed by a
quantity from QUANTITIES (the vocabulary documented in column_renames.py), or a detector flag of the
acceptance roles (<particle>_<Collection>Hits). Duplicate names within one output are an error.
Exit code 1 on any violation.
"""
import argparse
import re
import subprocess
import sys

PREFIXES = ("prt", "trk_hit", "cal_hit", "clu", "rec", "cand", "hit",
            "lam", "prot", "pimin", "neut", "pizero", "gamone", "gamtwo", "true_prot", "true_pimin",
            "elec", "mc_elec", "mc_lam", "ff_lam", "mc_beam_prot", "mc_beam_elec", "mc",
            "da", "esigma", "electron", "jb", "ml", "sigma")

QUANTITIES = {
    "index", "pdg", "gen_status", "sim_status", "origin", "mass", "energy", "charge",
    "mom_x", "mom_y", "mom_z", "mom", "theta", "eta", "phi",
    "vtx_time", "vtx_pos_x", "vtx_pos_y", "vtx_pos_z", "end_time", "end_pos_x", "end_pos_y", "end_pos_z",
    "n_parents", "n_daughters", "parent_index",
    "ref_pos_x", "ref_pos_y", "ref_pos_z", "pid_goodness", "type",
    "n_clusters", "n_tracks", "n_particles", "n_particle_ids", "n_cluster_hits", "n_track_measurements", "n_tracker_hits",
    "cov_xx", "cov_xy", "cov_xz", "cov_yy", "cov_yz", "cov_zz", "cov_xt", "cov_yt", "cov_zt", "cov_tt",
    "collection", "cell_id", "system_id", "system_name", "pos_x", "pos_y", "pos_z", "time",
    "pos_err_xx", "pos_err_yy", "pos_err_zz", "time_err", "edep", "edep_err", "energy_err", "path_length",
    "first_cell_id", "nhits", "assoc_weight",
    "is_first", "decay", "decay_pos_x", "decay_pos_y", "decay_pos_z",
    "source", "quality", "ndf", "n_measurements", "weight",
    "n_hits_b0", "n_hits_rp", "n_hits_offm", "cent", "b0trk", "rp_dp", "offm_dp",
    "first_b0_pos_x", "first_b0_pos_y", "first_b0_pos_z", "ecal_contrib",
    "first_ecal_pos_x", "first_ecal_pos_y", "first_ecal_pos_z", "first_rp_pos_x", "first_rp_pos_y", "first_rp_pos_z",
    "x", "q2", "y", "nu", "w", "true_t", "tb_t", "exp_t",
}

WHOLE_NAMES = {
    "evt", "event_number", "reco_collections", "sim_collections",
    "label_purity", "n_contrib", "sim_energy", "sim_time", "n_lambdas", "n_rp", "is_true_lam",
    "is_sul_prot", "is_sul_pimin", "dp_prot", "dth_prot", "dp_pimin", "dth_pimin",
    # mc_dis: the generator's own variable names
    "alphas", "mx2", "nu", "p_rt", "pdrest", "pperps", "pperpz", "q2", "s_e", "s_q", "tempvar", "tprime",
    "tspectator", "twopdotk", "twopdotq", "w", "x_d", "xbj", "y_d", "yplus",
}

DETECTOR_FLAG = re.compile(r"^(prot|pimin|neut|gamone|gamtwo)_[A-Z][A-Za-z0-9]*Hits$")


def violation(name):
    """None when the name fits the vocabulary; otherwise the reason."""
    if name in WHOLE_NAMES or DETECTOR_FLAG.match(name):
        return None
    for prefix in sorted(PREFIXES, key=len, reverse=True):
        if name.startswith(prefix + "_") and name[len(prefix) + 1:] in QUANTITIES:
            return None
    return "no known prefix + quantity"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--eic2ai")
    parser.add_argument("--headers")
    parser.add_argument("--markdown", action="store_true")
    args = parser.parse_args()
    if args.eic2ai:
        text = subprocess.run([args.eic2ai, "--list-columns"], capture_output=True, text=True, check=True).stdout
    elif args.headers:
        text = open(args.headers).read()
    else:
        sys.exit("give --eic2ai BIN or --headers FILE")

    problems = 0
    for line in text.splitlines():
        if ": " not in line:
            continue
        output, header = line.split(": ", 1)
        names = header.split(",")
        duplicates = sorted({n for n in names if names.count(n) > 1})
        if duplicates:
            print(f"{output}: duplicate columns {duplicates}")
            problems += 1
        for name in names:
            reason = violation(name)
            if reason:
                print(f"{output}: column {name!r}: {reason}")
                problems += 1
        if args.markdown:
            print(f"| `{output}` | {len(names)} | {', '.join(f'`{n}`' for n in names)} |")
    print("columns:", "FAIL" if problems else "PASS", f"({problems} violations)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
