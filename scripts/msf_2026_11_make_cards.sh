#!/bin/bash
# Write the dataset cards for configs/config-msf-26-11.yaml: one card per
# (stage, energy). Run on ifarm from the simulation-pipeline checkout, then
# render each stage's jobs with its NN_*.py script (see the bottom).
#
#   bash scripts/msf_2026_11_make_cards.sh [stage ...]
#
# Stages: npsim tfb_200ns tfb_2us bkg_2us eicrecon csv (default: all). Stage 23
# (pools) is cocktail-driven and needs no cards. Re-running overwrites the
# cards it touches. Cards glob the upstream stage's directory, so make them
# after that stage has produced its files (frames after stage 20, eicrecon
# after the frames, csv after eicrecon).
set -euo pipefail

HERE=$(cd "$(dirname "$0")/.." && pwd)
CFG="$HERE/configs/config-msf-26-11.yaml"
GEN="python -m simulation_pipeline.generate_datasets"
BASE=/work/eic3/users/romanov/meson-structure-2026-11

ENERGIES="5x41 9x100 9x130 9x275"          # signal (stage 20)
FRAME_ENERGIES="9x100 9x130 9x275"         # pools and frames (5x41 has no cocktail inputs)
BKG=/work/eic3/users/romanov/background-only-2026-11
STAGES=${*:-"npsim tfb_200ns tfb_2us bkg_2us eicrecon csv"}

cd "$HERE"
for stage in $STAGES; do
  echo "################ $stage"
  energies=$FRAME_ENERGIES; [[ $stage == npsim || $stage == eicrecon || $stage == csv ]] && energies=$ENERGIES
  for e in $energies; do
    case $stage in
      npsim)
        # first 80 afterburner files of 2026-07 (config max_files_per_dataset)
        $GEN npsim -c "$CFG" --energy "$e" ;;
      tfb_200ns)
        $GEN tfb_200ns -c "$CFG" --energy "$e" --slug msf_tfb200ns --input-dir "$BASE/dd4hep/$e" ;;
      tfb_2us)
        $GEN tfb_2us -c "$CFG" --energy "$e" --slug msf_tfb2us --input-dir "$BASE/dd4hep/$e" ;;
      bkg_2us)
        $GEN bkg_2us -c "$CFG" --energy "$e" --slug bkg_2us --empty ;;
      eicrecon)
        # one card per input dataset; 5x41 has the signal only
        $GEN eicrecon -c "$CFG" --energy "$e" --slug msf_signal \
             --input-dir "$BASE/dd4hep/$e" --output-dir "$BASE/reco/$e"
        [[ $e == 5x41 ]] && continue
        $GEN eicrecon -c "$CFG" --energy "$e" --slug msf_tfb200ns \
             --input-dir "$BASE/dd4hep-tfb200ns/$e" --output-dir "$BASE/reco-tfb200ns/$e"
        $GEN eicrecon -c "$CFG" --energy "$e" --slug msf_tfb2us \
             --input-dir "$BASE/dd4hep-tfb2us/$e" --output-dir "$BASE/reco-tfb2us/$e"
        $GEN eicrecon -c "$CFG" --energy "$e" --slug bkg_2us \
             --input-dir "$BKG/frames-2us/$e" --output-dir "$BKG/reco-2us/$e" ;;
      csv)
        $GEN csv_signal -c "$CFG" --energy "$e"
        [[ $e == 5x41 ]] && continue
        $GEN csv_tfb200ns -c "$CFG" --energy "$e"
        $GEN csv_tfb2us   -c "$CFG" --energy "$e"
        $GEN csv_bkg2us   -c "$CFG" --energy "$e" ;;
      *) echo "unknown stage $stage"; exit 1 ;;
    esac
  done
done

cat <<EOF

Cards written under $BASE/datasets/<stage>/. Render jobs with:
  python simulation_pipeline/23_create_npsim_bgsource_jobs.py    -c $CFG            # no cards: cocktail-driven
  python simulation_pipeline/20_create_npsim_jobs.py             -c $CFG
  python simulation_pipeline/13_create_timeframe_builder_jobs.py bkg_2us  -c $CFG   # after stage 23 ran
  python simulation_pipeline/13_create_timeframe_builder_jobs.py tfb_200ns -c $CFG   # after stages 20 and 23 ran
  python simulation_pipeline/13_create_timeframe_builder_jobs.py tfb_2us  -c $CFG
  python simulation_pipeline/30_create_eicrecon_jobs.py          -c $CFG   # after the frames
  python simulation_pipeline/40_csv_convert.py csv_signal        -c $CFG   # after eicrecon
  python simulation_pipeline/40_csv_convert.py csv_tfb200ns      -c $CFG
  python simulation_pipeline/40_csv_convert.py csv_tfb2us        -c $CFG
  python simulation_pipeline/40_csv_convert.py csv_bkg2us        -c $CFG
EOF
