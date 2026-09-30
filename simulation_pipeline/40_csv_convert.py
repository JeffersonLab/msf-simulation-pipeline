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

`converter: eic2ai` (stage or top level) switches a stage to the single-pass
program in ../eic2ai/: the same macro lists name the roles, or a `roles:` list
names them directly (for roles without a macro, such as event_index);
`eic2ai_bin` is the binary's path inside the container (its directory is
bound), `eic2ai_args` adds options such as `--selective`. Outputs are
`<stem>.<role>.csv.zip` only; a role is regenerated when its zip is missing or
empty.
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
SCRIPT_INTRO = """\
#!/bin/bash
set -uo pipefail

echo "= CSV CONVERSION ============================================================"
echo "  Input: {input_file}"
echo "  Macros dir: {csv_convert_dir}"
echo "==========================================================================="

cd "{csv_convert_dir}"

rc=0

"""

# Shared by the macro script and the eic2ai script (see build_eic2ai_script_template).
STAGING_BLOCK = """\
# ---- node-local staging --------------------------------------------------
# Farm admins require inputs staged to the compute node's local disk: many
# jobs streaming multi-GB files from the work file server saturate it and
# take nodes offline (campaign 2026-09 was cancelled for exactly this).
# Inputs are copied ONCE to $STAGE_DIR, macros read the local copies, and
# outputs are written locally and moved to the final directory at the end.
STAGE_BASE="/scratch"
[ -d "$STAGE_BASE" ] && [ -w "$STAGE_BASE" ] || STAGE_BASE="/tmp"
STAGE_DIR=$(mktemp -d "$STAGE_BASE/csvstage.XXXXXX") || {{ echo "[FATAL] cannot create staging dir under $STAGE_BASE"; exit 3; }}
trap 'rm -rf "$STAGE_DIR"' EXIT
echo "  Staging dir: $STAGE_DIR"

stage_in() {{
  # stage_in <src>: copies to $STAGE_DIR, echoes the local path.
  # Refuses to run without room — falling back to reading from the work
  # file server is the behavior the admins cancel jobs for.
  local src="$1" dst need avail
  dst="$STAGE_DIR/$(basename "$src")"
  need=$(( $(stat -c%s "$src") / 1000000000 + 25 ))
  avail=$(( $(df -Pk "$STAGE_DIR" | awk 'NR==2 {{print $4}}') / 1000000 ))
  if [ "$avail" -le "$need" ]; then
    echo "[FATAL] not enough staging space: need ~$need GB, avail $avail GB" >&2
    exit 3
  fi
  cp "$src" "$dst" || {{ echo "[FATAL] staging copy failed: $src" >&2; exit 3; }}
  echo "$dst"
}}

"""

MACRO_CONVERT_FUNCTION = """\
convert() {{
  local label="$1" macro="$2" out="$3"; shift 3
  local out_local="$STAGE_DIR/$(basename "$out")"
  # Remaining args are the macro's input file(s) -> ROOT call arguments.
  local in_args="" f
  for f in "$@"; do in_args="${{in_args}}\\"$f\\","; done
  # Regenerate when the FINAL CSV is missing OR empty (-s: exists, non-empty).
  if [ ! -s "$out" ]; then
    echo "[RUN] $label via $macro"
    if ! root -x -l -b -q "$macro(${{in_args}}\\"$out_local\\")"; then
      echo "[WARN] $label: macro returned non-zero"; rc=1
    fi
    # A crashed macro leaves a 0-byte file; drop it so it is retried next run.
    if [ -f "$out_local" ] && [ ! -s "$out_local" ]; then
      echo "[WARN] $label: produced empty output -- removing"; rm -f "$out_local"; rc=1
    fi
    if [ -s "$out_local" ]; then
      echo "[ZIP] $(basename "$out_local")"
      python3 -m zipfile -c "$out_local.zip" "$out_local" || {{ echo "[WARN] $label: zip failed"; rc=1; }}
      mv "$out_local" "$out" || rc=1
      [ -f "$out_local.zip" ] && {{ mv "$out_local.zip" "$out.zip" || rc=1; }}
    fi
  else
    echo "[SKIP] $label ($out exists, non-empty)"
    if [ ! -f "$out.zip" ]; then
      echo "[ZIP] existing $out"
      python3 -m zipfile -c "$out.zip" "$out" || {{ echo "[WARN] $label: zip failed"; rc=1; }}
    fi
  fi
}}

"""

SCRIPT_HEAD = SCRIPT_INTRO + STAGING_BLOCK + MACRO_CONVERT_FUNCTION

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
    all_outs = ' '.join(f'"{{out_{macro_role(m)}}}"'
                        for m in list(macros) + list(sim_macros) + list(paired_macros))
    stage = [
        '# Stage only when at least one output is missing — a fully converted',
        '# file must rerun as pure SKIPs without copying gigabytes first.',
        'need_stage=0',
        f'for f in {all_outs}; do [ -s "$f" ] || need_stage=1; done',
        'in_local=""; paired_local=""',
        'if [ "$need_stage" = 1 ]; then',
        '  in_local=$(stage_in "{input_file}") || exit 3',
    ]
    if paired:
        stage.append('  paired_local=$(stage_in "{paired_file}") || exit 3')
    stage += [
        'else',
        '  echo "All outputs exist -- staging skipped"',
        'fi',
        '',
    ]
    calls = []
    for m in macros:
        calls.append(f'convert "{macro_role(m)}" "{m}.cxx" "{{out_{macro_role(m)}}}" "$in_local"')
    for m in sim_macros:
        calls.append(f'convert "{macro_role(m)}" "{m}.cxx" "{{out_{macro_role(m)}}}" "$paired_local"')
    for m in paired_macros:
        calls.append(f'convert "{macro_role(m)}" "{m}.cxx" "{{out_{macro_role(m)}}}" '
                     f'"$in_local" "$paired_local"')
    return head + "\n".join(stage + calls) + "\n" + SCRIPT_FOOT


# The eic2ai container script: one program call for every role still missing.
# eic2ai (../eic2ai/) reads the input once and writes every role as
# <stem>.<role>.csv.zip through a .tmp + rename, so a present zip is a
# complete one and the skip rule needs no "empty output" special case.
EIC2AI_SCRIPT_INTRO = """\
#!/bin/bash
set -uo pipefail

echo "= CSV CONVERSION (eic2ai) ==================================================="
echo "  Input: {input_file}"
echo "  eic2ai: {eic2ai_bin}"
echo "==========================================================================="

rc=0

"""


def build_eic2ai_script_template(roles, paired=False):
    """The full container script for converter: eic2ai.

    Every input file (reco, or sim, or both) is staged once; eic2ai gets the
    local copies as positional arguments (it sorts them by suffix) and writes
    the missing roles into the staging directory; finished zips move to the
    output directory at the end. A writer that fails leaves no file and the
    exit code is non-zero, so the next run regenerates that role alone.
    """
    head = EIC2AI_SCRIPT_INTRO + STAGING_BLOCK
    if paired:
        head = head.replace(
            'echo "  eic2ai: {eic2ai_bin}"',
            'echo "  Paired: {paired_file}"\n'
            'echo "  eic2ai: {eic2ai_bin}"')
    body = [
        '# Roles whose final <stem>.<role>.csv.zip is missing or empty are regenerated.',
        'missing=""',
    ]
    for role in roles:
        body.append(f'if [ -s "{{out_{role}}}.zip" ]; then echo "[SKIP] {role} (zip exists)"; '
                    f'else missing="$missing,{role}"; fi')
    body += [
        'missing=${{missing#,}}',
        'if [ -z "$missing" ]; then echo "All outputs exist -- nothing to do"; exit 0; fi',
        'in_local=$(stage_in "{input_file}") || exit 3',
    ]
    if paired:
        body.append('paired_local=$(stage_in "{paired_file}") || exit 3')
    inputs = '"$in_local" "$paired_local"' if paired else '"$in_local"'
    body += [
        'echo "[RUN] eic2ai --writers $missing"',
        f'if ! "{{eic2ai_bin}}" {inputs} --out-dir "$STAGE_DIR" --writers "$missing" --compress zip {{eic2ai_args}}; then',
        '  echo "[WARN] eic2ai returned non-zero (a failed writer leaves no file; the other roles are complete)"; rc=1',
        'fi',
        'for f in "$STAGE_DIR"/*.csv.zip; do',
        '  [ -f "$f" ] || continue',
        '  mv "$f" "{output_dir}/" || {{ echo "[WARN] cannot move $f"; rc=1; }}',
        'done',
    ]
    return head + "\n".join(body) + "\n" + SCRIPT_FOOT


def build_creator(stage, config, card):
    """One JobCreator per card: run the stage's macros over the card's files."""
    scfg = config[stage]
    macros = list(scfg.get("macros", []))
    sim_macros = list(scfg.get("sim_macros", []))
    paired_macros = list(scfg.get("paired_macros", []))
    if not (macros or sim_macros or paired_macros or scfg.get("roles")):
        raise SystemExit(f"Config '{stage}.macros' is empty -- nothing to convert.")
    csv_convert_dir = str(config.get("csv_convert_dir", CSV_CONVERT_DIR_DEFAULT))

    # converter: macros (default; one ROOT macro per role) or eic2ai (one program
    # call for every role; the same macro lists name the roles).
    converter = str(scfg.get("converter", config.get("converter", "macros")))
    if converter not in ("macros", "eic2ai"):
        raise SystemExit(f"'{stage}.converter' must be macros or eic2ai, got {converter!r}")
    eic2ai_bin = str(scfg.get("eic2ai_bin", config.get("eic2ai_bin", "")))
    eic2ai_args = str(scfg.get("eic2ai_args", config.get("eic2ai_args", "")))
    if converter == "eic2ai" and not eic2ai_bin:
        raise SystemExit(f"'{stage}': converter eic2ai needs 'eic2ai_bin' (the binary's path as seen inside the container)")
    # The roles a job produces: the macro lists name them; an eic2ai stage may list them
    # directly (`roles:`), which also covers roles without a macro such as event_index.
    roles = [macro_role(m) for m in macros + sim_macros + paired_macros]
    if converter == "eic2ai" and scfg.get("roles"):
        roles = [str(r) for r in scfg.get("roles")]

    bind_dirs = list(config.bind_dirs) if "bind_dirs" in config else []
    if csv_convert_dir not in bind_dirs:
        bind_dirs.append(csv_convert_dir)
    if converter == "eic2ai" and os.path.dirname(eic2ai_bin) not in bind_dirs:
        bind_dirs.append(os.path.dirname(eic2ai_bin))

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
        for role in roles:
            params[f"out_{role}"] = os.path.join(params["output_dir"], f"{stem}.{role}.csv")
        params["eic2ai_bin"] = eic2ai_bin
        params["eic2ai_args"] = eic2ai_args
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
        slurm_array_throttle=int(config.get("slurm_array_throttle", 0)),
        slurm_mem_per_cpu=str(config.get("slurm_mem_per_cpu", "2G")),
        farm_out_dir=config.get("farm_out_dir"),
    )
    if converter == "eic2ai":
        runner.container_script_template = build_eic2ai_script_template(
            roles, paired=paired_cfg is not None)
    else:
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
