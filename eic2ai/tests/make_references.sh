#!/usr/bin/env bash
# make_references.sh — runs one csv_convert macro executable on the reference inputs and
# stores its CSVs as parity references for eic2ai (tests/parity.py compares against them).
#
# Run inside the eic-claude container (the same podio and fmt libraries eic2ai links, so
# float formatting matches). Each run is wrapped in /usr/bin/time -v; the log next to the
# CSV carries wall time and "Maximum resident set size".
#
# Usage: tests/make_references.sh <macro-bin-dir> <ref-root> <role> [dataset ...]
#   role:     trk_hits | cal_hits | calo_clusters | ...  (executable edm4eic_<role>)
#   datasets: signal_n100 bkg_n100 signal_full bkg_n500 bkg_full (default: the two 100-event sets)
# Output:     <ref-root>/<dataset>/<stem>.<role>.csv  (+ .log, + .done marker on success)
# A dataset with a .done marker is skipped, so the script can be re-run after an interruption.
set -uo pipefail

macro_bin_dir=$1
ref_root=$2
role=$3
shift 3
datasets=("$@")
if [ ${#datasets[@]} -eq 0 ]; then
    datasets=(signal_n100 bkg_n100)
fi

signal_reco=/data/meson-structure-2026-07/reco/9x130/msf_9x130_1000evt_0001.edm4eic.root
signal_sim=/data/meson-structure-2026-07/dd4hep/9x130/msf_9x130_1000evt_0001.edm4hep.root
bkg_reco=/data/msf-2026-06/reco-background/k_lambda_18x275_5000evt_0022.edm4eic.root
bkg_sim=/data/msf-2026-06/dd4hep-background/18x275/k_lambda_18x275_5000evt_0022.edm4hep.root

# Roles whose macro takes the paired sim file as a second input.
paired_roles=" cal_hits "
# Roles whose macro (edm4hep_<role>) reads the sim file alone.
sim_roles=" acceptance_ppim acceptance_npi0 combinatorics_ppim "

run_one() {
    local dataset=$1 events=$2 reco=$3 sim=$4
    local stem; stem=$(basename "$reco"); stem=${stem%.edm4eic.root}
    local dir=$ref_root/$dataset
    mkdir -p "$dir"
    local out=$dir/$stem.$role.csv
    if [ -f "$out.done" ]; then
        echo "skip $dataset/$role: reference exists"
        return
    fi
    local args=()
    if [ "$events" -gt 0 ]; then args+=(-n "$events"); fi
    local executable=edm4eic_$role
    if [[ "$sim_roles" == *" $role "* ]]; then
        executable=edm4hep_$role
        args+=(-o "$out" "$sim")
    else
        args+=(-o "$out" "$reco")
        if [[ "$paired_roles" == *" $role "* ]]; then args+=("$sim"); fi
    fi
    echo "run  $dataset/$role: $executable ${args[*]}"
    if /usr/bin/time -v "$macro_bin_dir/$executable" "${args[@]}" > "$dir/$stem.$role.log" 2>&1; then
        touch "$out.done"
        grep -E "Elapsed|Maximum resident" "$dir/$stem.$role.log"
    else
        echo "FAILED $dataset/$role: see $dir/$stem.$role.log"
    fi
}

for dataset in "${datasets[@]}"; do
    case $dataset in
        signal_n100) run_one "$dataset" 100 "$signal_reco" "$signal_sim" ;;
        signal_full) run_one "$dataset" 0   "$signal_reco" "$signal_sim" ;;
        bkg_n100)    run_one "$dataset" 100 "$bkg_reco"    "$bkg_sim" ;;
        bkg_n500)    run_one "$dataset" 500 "$bkg_reco"    "$bkg_sim" ;;
        bkg_full)    run_one "$dataset" 0   "$bkg_reco"    "$bkg_sim" ;;
        *) echo "unknown dataset: $dataset"; exit 1 ;;
    esac
done
