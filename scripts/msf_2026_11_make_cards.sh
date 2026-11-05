#!/bin/bash
# Write the dataset cards for configs/config-msf-26-11.yaml: one card per
# (stage, energy). Run on ifarm from the simulation-pipeline checkout, then
# render each stage's jobs with its NN_*.py script (see the bottom).
#
#   bash scripts/msf_2026_11_make_cards.sh [stage ...]
#
# Stages: npsim tfb_200ns tfb_2us bkg_2us (default: all). Stage 23 (pools) is
# cocktail-driven and needs no cards. Re-running overwrites the cards it
# touches. Cards for a frame stage glob the dd4hep/ directory, so make them
# after stage 20 has produced its files.
set -euo pipefail

HERE=$(cd "$(dirname "$0")/.." && pwd)
CFG="$HERE/configs/config-msf-26-11.yaml"
GEN="python -m simulation_pipeline.generate_datasets"
BASE=/work/eic3/users/romanov/meson-structure-2026-11

ENERGIES="5x41 9x100 9x130 9x275"          # signal (stage 20)
FRAME_ENERGIES="9x100 9x130 9x275"         # pools and frames (5x41 has no cocktail inputs)
STAGES=${*:-"npsim tfb_200ns tfb_2us bkg_2us"}

cd "$HERE"
for stage in $STAGES; do
  echo "################ $stage"
  energies=$ENERGIES; [[ $stage != npsim ]] && energies=$FRAME_ENERGIES
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
EOF
