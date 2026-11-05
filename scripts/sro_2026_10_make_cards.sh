#!/bin/bash
# Write the dataset cards for configs/config-sro-26-10.yaml: one card per
# (stage, energy, slug). Run on ifarm from the simulation-pipeline checkout,
# then render each stage's jobs with its NN_*.py script (see the bottom).
#
#   bash scripts/sro_2026_10_make_cards.sh [stage ...]
#
# Stages: bg_merger npsim tfb_frames npsim_background eicrecon csv
# (default: all). Re-running overwrites the cards it touches.
set -euo pipefail

HERE=$(cd "$(dirname "$0")/.." && pwd)
CFG="$HERE/configs/config-sro-26-10.yaml"
GEN="python -m simulation_pipeline.generate_datasets"
BASE=/work/eic/users/romanov/sro-2026-10
MSF=/work/eic3/users/romanov/meson-structure-2026-06/afterburner
CC=/volatile/eic/EPIC/EVGEN/DIS/CC

ENERGIES="10x100 10x130 10x275"
STAGES=${*:-"bg_merger npsim tfb_frames npsim_background eicrecon csv"}

# Which signal datasets exist at which energy (Q1, Q2 in the config).
signal_dir() {                 # $1 slug, $2 energy -> input dir or "" if none
  case "$1" in
    dis_cc_py8_q2100)  [[ $2 != 10x130 ]] && echo "$CC/$2/minQ2=100" ;;
    dis_cc_py8_q21000) [[ $2 != 10x130 ]] && echo "$CC/$2/minQ2=1000" ;;
    msf_klam)          [[ $2 != 10x275 ]] && echo "$MSF/$2-priority" ;;
  esac
  return 0
}
signal_pattern() {
  case "$1" in msf_klam) echo "*.afterburner.hepmc" ;; *) echo "*.hepmc3.tree.root" ;; esac
}
signal_max_files() {
  case "$1" in msf_klam) echo 8 ;; *) echo 1 ;; esac
}
SIGNALS="dis_cc_py8_q2100 dis_cc_py8_q21000 msf_klam"
GHOST="$CC/10x100/minQ2=100"   # bkg_empty ghost signal on the merger path (Q3)

cd "$HERE"
for stage in $STAGES; do
  echo "################ $stage"
  for e in $ENERGIES; do
    case $stage in
      bg_merger)
        # background-only frames: ghost signal, status shifted (config datasets.bkg_empty)
        $GEN bg_merger -c "$CFG" --energy "$e" --slug bkg_empty \
             --input-dir "$GHOST" --max-files 1
        for s in $SIGNALS; do
          d=$(signal_dir "$s" "$e"); [[ -z $d ]] && continue
          $GEN bg_merger -c "$CFG" --energy "$e" --slug "$s" --input-dir "$d" \
               --pattern "$(signal_pattern "$s")" --max-files "$(signal_max_files "$s")"
        done ;;
      npsim)
        # background-free signal for the -tfb path (same chunking as the merger)
        for s in $SIGNALS; do
          d=$(signal_dir "$s" "$e"); [[ -z $d ]] && continue
          $GEN npsim -c "$CFG" --energy "$e" --slug "$s" --input-dir "$d" \
               --pattern "$(signal_pattern "$s")" --max-files "$(signal_max_files "$s")"
        done ;;
      tfb_frames)
        $GEN tfb_frames -c "$CFG" --energy "$e" --slug bkg_empty --empty
        for s in $SIGNALS; do
          d=$(signal_dir "$s" "$e"); [[ -z $d ]] && continue
          $GEN tfb_frames -c "$CFG" --energy "$e" --slug "$s" \
               --input-dir "$BASE/dd4hep-signal/$e/$s"
        done ;;
      npsim_background)
        for s in bkg_empty $SIGNALS; do
          [[ $s != bkg_empty ]] && { d=$(signal_dir "$s" "$e"); [[ -z $d ]] && continue; }
          $GEN npsim_background -c "$CFG" --energy "$e" --slug "$s" \
               --input-dir "$BASE/bg_merged-merger/$e/$s"
        done ;;
      eicrecon)
        for s in bkg_empty $SIGNALS; do
          [[ $s != bkg_empty ]] && { d=$(signal_dir "$s" "$e"); [[ -z $d ]] && continue; }
          $GEN eicrecon -c "$CFG" --energy "$e" --slug "$s-merger" \
               --input-dir "$BASE/dd4hep-merger/$e/$s" --output-dir "$BASE/reco-merger/$e/$s"
          $GEN eicrecon -c "$CFG" --energy "$e" --slug "$s-tfb" \
               --input-dir "$BASE/dd4hep-tfb/$e/$s" --output-dir "$BASE/reco-tfb/$e/$s"
        done ;;
      csv)
        for s in bkg_empty $SIGNALS; do
          [[ $s != bkg_empty ]] && { d=$(signal_dir "$s" "$e"); [[ -z $d ]] && continue; }
          $GEN csv_merger -c "$CFG" --energy "$e" --slug "$s" --input-dir "$BASE/reco-merger/$e/$s"
          $GEN csv_tfb    -c "$CFG" --energy "$e" --slug "$s" --input-dir "$BASE/reco-tfb/$e/$s"
        done ;;
      *) echo "unknown stage $stage"; exit 1 ;;
    esac
  done
done

cat <<EOF

Cards written under $BASE/datasets/<stage>/. Render jobs with:
  python simulation_pipeline/11_create_background_jobs.py        -c $CFG
  python simulation_pipeline/23_create_npsim_bgsource_jobs.py    -c $CFG   # no cards: cocktail-driven
  python simulation_pipeline/20_create_npsim_jobs.py             -c $CFG
  python simulation_pipeline/13_create_timeframe_builder_jobs.py -c $CFG   # after stages 20 and 23 ran
  python simulation_pipeline/22_create_npsim_background_jobs.py  -c $CFG   # after stage 11 ran
  python simulation_pipeline/30_create_eicrecon_jobs.py          -c $CFG
  python simulation_pipeline/40_csv_convert.py csv_merger        -c $CFG
  python simulation_pipeline/40_csv_convert.py csv_tfb           -c $CFG
Cards for a downstream stage must be (re)made after the upstream stage has
produced its files (they glob the directory).
EOF
