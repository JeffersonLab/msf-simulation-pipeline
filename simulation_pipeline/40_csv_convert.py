#!/usr/bin/env python3
"""
40_csv_convert.py — CSV conversion jobs for ANY csv stage.

    python 40_csv_convert.py csv_eicrecon -c config.yaml
    python 40_csv_convert.py csv_dd4hep   -c config.yaml

Reads the stage's dataset cards (`generate_datasets <stage> -c <config>`) and,
for every input ROOT file, emits a job that runs the stage's converter macros:

    csv_eicrecon:
      macros: [edm4eic_mc_dis, edm4eic_reco_particles, ...]

A stage may declare a `paired` block: every input gets a same-stem
companion file (reco + its simulation file). Three macro lists say which
input(s) each macro receives:

    csv_background:
      input:   "${eicrecon.output}"
      pattern: "*.edm4eic.root"
      paired:  { dir: "${npsim.output}", suffix: ".edm4hep.root" }
      macros:        [edm4eic_trk_hits, ...]   # macro(input, out)
      sim_macros:    [edm4hep_acceptance_ppim] # macro(paired, out)
      paired_macros: [edm4eic_cal_hits]        # macro(input, paired, out)

Inputs whose paired file is missing are skipped at job-build time with a
warning.

Each macro `<name>` is `csv_convert/<name>.cxx` with a ROOT entry function of
the same name, called as `<name>("input.root", "output.csv")`. The output CSV
is `<output>/<input-basename>.<role>.csv`, where <role> is the macro name minus
its `edm4hep_`/`edm4eic_` data-model prefix:

    input:  msf_9x130_0001.edm4eic.root
    macro:  edm4eic_reco_particles
    output: msf_9x130_0001.reco_particles.csv  (+ .zip)

Which stage reads which data model is the config's business: csv_dd4hep lists
edm4hep_* macros, csv_eicrecon lists edm4eic_* macros. This script is the same
for both.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from simulation_pipeline.job_creator import JobCreator, write_top_master_scripts
from simulation_pipeline.datasets import load_config, load_config_for_energy, load_cards

# Converter macros shipped with this repo (a config may point csv_convert_dir
# at a project-local directory with extra macros instead).
CSV_CONVERT_DIR_DEFAULT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "csv_convert")


# The container script: one `convert <role> <macro>.cxx <out.csv>` line per
# configured macro. `set -uo pipefail` but NOT -e: converters are independent,
# one crashing must not abort the others; empty outputs are deleted so the
# next run retries them instead of skipping "already exists".
SCRIPT_HEAD = """\
#!/bin/bash
set -uo pipefail

echo "= CSV CONVERSION ============================================================"
echo "  Input: {input_file}"
echo "  Macros dir: {csv_convert_dir}"
echo "==========================================================================="

cd "{csv_convert_dir}"

rc=0

convert() {{
  local label="$1" macro="$2" out="$3"; shift 3
  # Remaining args are the macro's input file(s) -> ROOT call arguments.
  local in_args="" f
  for f in "$@"; do in_args="${{in_args}}\\"$f\\","; done
  # Regenerate when the CSV is missing OR empty (-s: exists and non-empty).
  if [ ! -s "$out" ]; then
    echo "[RUN] $label via $macro"
    if ! root -x -l -b -q "$macro(${{in_args}}\\"$out\\")"; then
      echo "[WARN] $label: macro returned non-zero"; rc=1
    fi
    # A crashed macro leaves a 0-byte file; drop it so it is retried next run.
    if [ -f "$out" ] && [ ! -s "$out" ]; then
      echo "[WARN] $label: produced empty $out -- removing"; rm -f "$out"; rc=1
    fi
  else
    echo "[SKIP] $label ($out exists, non-empty)"
  fi
  if [ -s "$out" ] && [ ! -f "$out.zip" ]; then
    echo "[ZIP] $out -> $out.zip"
    python3 -m zipfile -c "$out.zip" "$out" || {{ echo "[WARN] $label: zip failed"; rc=1; }}
  fi
}}

"""

SCRIPT_FOOT = """
echo "==========================================================================="
echo "Done (rc=$rc)"
exit $rc
"""


def macro_role(macro):
    """'edm4eic_reco_particles' -> 'reco_particles' (csv suffix + log label)."""
    for prefix in ("edm4eic_", "edm4hep_"):
        if macro.startswith(prefix):
            return macro[len(prefix):]
    return macro


def input_stem(input_file):
    """Input basename without its ROOT suffix: 'x.edm4eic.root' -> 'x'."""
    name = os.path.basename(input_file)
    for suffix in (".eicrecon.edm4eic.root", ".edm4eic.root", ".edm4hep.root", ".root"):
        if name.endswith(suffix):
            return name[: -len(suffix)]
    return name


def build_script_template(macros, sim_macros=(), paired_macros=(), paired=False):
    """The full container script with one convert() call per macro.

    Macro classes differ only in which input file(s) each call receives:
      macros        — the stage's input file (the reco file for csv stages)
      sim_macros    — the paired file alone (converters that read simulation)
      paired_macros — both files, e.g. edm4eic_cal_hits(reco, sim, out)
    """
    head = SCRIPT_HEAD
    if paired:
        head = head.replace(
            'echo "  Macros dir: {csv_convert_dir}"',
            'echo "  Paired: {paired_file}"\n'
            'echo "  Macros dir: {csv_convert_dir}"')
    calls = []
    for m in macros:
        calls.append(f'convert "{macro_role(m)}" "{m}.cxx" "{{out_{macro_role(m)}}}" "{{input_file}}"')
    for m in sim_macros:
        calls.append(f'convert "{macro_role(m)}" "{m}.cxx" "{{out_{macro_role(m)}}}" "{{paired_file}}"')
    for m in paired_macros:
        calls.append(f'convert "{macro_role(m)}" "{m}.cxx" "{{out_{macro_role(m)}}}" '
                     f'"{{input_file}}" "{{paired_file}}"')
    return head + "\n".join(calls) + "\n" + SCRIPT_FOOT


def build_creator(stage, config, card):
    """One JobCreator per card: run the stage's macros over the card's files."""
    scfg = config[stage]
    macros = list(scfg.get("macros", []))
    sim_macros = list(scfg.get("sim_macros", []))
    paired_macros = list(scfg.get("paired_macros", []))
    if not (macros or sim_macros or paired_macros):
        raise SystemExit(f"Config '{stage}.macros' is empty -- nothing to convert.")
    csv_convert_dir = str(config.get("csv_convert_dir", CSV_CONVERT_DIR_DEFAULT))

    bind_dirs = list(config.bind_dirs) if "bind_dirs" in config else []
    if csv_convert_dir not in bind_dirs:
        bind_dirs.append(csv_convert_dir)

    output_dir = card.get("output") or str(scfg.output)

    # Paired stage: every input gets a same-stem companion file from
    # paired.dir + stem + paired.suffix, passed to the macros as a second
    # argument. Inputs whose companion is missing are dropped HERE, loudly —
    # a missing pair must surface at job-build time, not as a farm crash.
    paired_cfg = scfg.get("paired", None)
    if (sim_macros or paired_macros) and paired_cfg is None:
        raise SystemExit(f"'{stage}' lists sim_macros/paired_macros but has no "
                         f"'paired: {{dir, suffix}}' block")
    input_files = list(card["files"])
    if paired_cfg is not None:
        paired_dir = str(paired_cfg["dir"])
        paired_suffix = str(paired_cfg["suffix"])
        if paired_dir not in bind_dirs:
            bind_dirs.append(paired_dir)
        kept, missing = [], []
        for f in input_files:
            pair = os.path.join(paired_dir, input_stem(f) + paired_suffix)
            (kept if os.path.exists(pair) else missing).append(f)
        for f in missing:
            print(f"[WARN] no paired file for {os.path.basename(f)} "
                  f"(expected {input_stem(f)}{paired_suffix} in {paired_dir}) -- skipped")
        if not kept:
            raise SystemExit(f"'{stage}': no input has a paired file in {paired_dir}")
        input_files = kept

    def add_csv_paths(params):
        stem = input_stem(params["input_file"])
        params["csv_convert_dir"] = csv_convert_dir
        if paired_cfg is not None:
            params["paired_file"] = os.path.join(str(paired_cfg["dir"]),
                                                 stem + str(paired_cfg["suffix"]))
        for m in macros + sim_macros + paired_macros:
            role = macro_role(m)
            params[f"out_{role}"] = os.path.join(params["output_dir"], f"{stem}.{role}.csv")
        return params

    runner = JobCreator(
        input_files=input_files,
        output_file_name_func=lambda input_file, output_dir: output_dir,
        output_dir=output_dir,
        bind_dirs=bind_dirs,
        events=config.event_count,
        container=config.container,
        beam_config=card.get("energy") or card["slug"],
        slurm_files_per_job=int(config.get("slurm_files_per_job", 20)),
        slurm_mem_per_cpu=str(config.get("slurm_mem_per_cpu", "2G")),
        farm_out_dir=config.get("farm_out_dir"),
    )
    runner.container_script_template = build_script_template(
        macros, sim_macros, paired_macros, paired=paired_cfg is not None)
    runner.container_script_params_updater = add_csv_paths
    runner.run()
    return runner


def main():
    parser = argparse.ArgumentParser(description="Generate CSV conversion jobs for a csv stage.")
    parser.add_argument("stage", help="csv stage name from the config, e.g. csv_eicrecon or csv_dd4hep")
    parser.add_argument("-c", "--config", required=True, help="Path to config YAML file")
    parser.add_argument("--datasets", default=None,
                        help="Override the cards directory (default: <datasets_dir>/<stage>)")
    args = parser.parse_args()

    config = load_config(args.config)
    cards = load_cards(config, args.stage, override=args.datasets)

    creators = []
    for card in cards:
        energy = card.get("energy")
        cfg = load_config_for_energy(args.config, energy) if energy else config
        print("\n" + "=" * 60)
        print(f"CARD: {card['slug']}  ({card.get('n_files', len(card['files']))} files)")
        creators.append(build_creator(args.stage, cfg, card))

    write_top_master_scripts([c for c in creators if c is not None])
    print(f"ALL CARDS PROCESSED FOR STAGE '{args.stage}'")


if __name__ == "__main__":
    main()
