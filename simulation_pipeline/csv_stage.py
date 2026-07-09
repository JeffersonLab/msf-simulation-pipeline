"""Shared CSV-conversion stage builder (used by stages 40 and 41).

Both CSV stages do the same thing — run a configurable list of ROOT converter
macros over each input ROOT file and zip the CSV outputs — differing only in
the config block they read (``csv_dd4hep`` reads edm4hep, ``csv_eicrecon`` reads
edm4eic), the default macro set, and the default output-name stem.

The macro list is config-driven (``<stage>.macros``); each entry is a converter
basename under ``csv_convert/`` following the ``edm4hep_*`` / ``edm4eic_*``
naming convention (§3.5 of the plan). A macro ``edm4eic_reco_particles`` runs
``edm4eic_reco_particles.cxx`` (whose ROOT entry function has the same name) and
writes ``<stem>.reco_particles.csv``.
"""

import os
import textwrap

from .job_creator import JobCreator
from .datasets import macro_role, csv_stem

# Default converter directory: the merged csv_convert/ shipped with the repo.
CSV_CONVERT_DIR_DEFAULT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "csv_convert")


_TEMPLATE_HEAD = """\
#!/bin/bash
set -uo pipefail

echo "= CSV CONVERSION ============================================================"
echo "  Input: {input_file}"
echo "  Macros dir: {csv_convert_dir}"
echo "==========================================================================="

cd "{csv_convert_dir}"

rc=0

convert() {{
  local label="$1" macro="$2" out="$3"
  # Regenerate when the CSV is missing OR empty (-s: exists and non-empty).
  if [ ! -s "$out" ]; then
    echo "[RUN] $label via $macro"
    if ! root -x -l -b -q "$macro(\\"{input_file}\\",\\"$out\\")"; then
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

_TEMPLATE_FOOT = """
echo "==========================================================================="
echo "Done. Outputs in: {input_dir} (rc=$rc)"
exit $rc
"""


def build_template(macros):
    """Compose the container script for the given ordered macro list."""
    lines = []
    for m in macros:
        role = macro_role(m)
        lines.append(f'convert "{role}" "{m}.cxx" "{{out_{role}}}"')
    return _TEMPLATE_HEAD + "\n".join(lines) + "\n" + _TEMPLATE_FOOT


def make_params_updater(macros, stem_mode, csv_convert_dir):
    """Params updater that computes each converter's CSV output path."""
    def updater(params):
        input_file = params["input_file"]
        output_dir = params["output_dir"]
        stem = csv_stem(input_file, stem_mode)
        params["csv_convert_dir"] = csv_convert_dir
        params["input_dir"] = os.path.dirname(input_file)
        for m in macros:
            role = macro_role(m)
            params[f"out_{role}"] = os.path.join(output_dir, f"{stem}.{role}.csv")
        return params
    return updater


def build_csv_creator(stage, config, card, config_path, default_macros, default_stem):
    """Build a JobCreator for one CSV card. Shared by stages 40 and 41."""
    scfg = config.get(stage, {})
    macros = list(scfg.get("macros", default_macros))
    if not macros:
        raise SystemExit(
            f"Stage '{stage}' has no macros. Set '{stage}.macros' in the config.")
    stem_mode = str(scfg.get("stem", default_stem))
    csv_convert_dir = str(config.get("csv_convert_dir", CSV_CONVERT_DIR_DEFAULT))

    bind_dirs = list(config.bind_dirs) if "bind_dirs" in config else []
    if csv_convert_dir not in bind_dirs:
        bind_dirs.append(csv_convert_dir)

    output_dir = card.get("output") or str(scfg.get("output"))
    runner = JobCreator(
        input_files=list(card["files"]),
        output_file_name_func=lambda input_file, output_dir: output_dir,
        output_dir=output_dir,
        bind_dirs=bind_dirs,
        events=config.event_count,
        container=config.container,
        beam_config=card.get("energy") or card["slug"],
        slurm_files_per_job=int(config.get("slurm_files_per_job", 20)),
    )
    runner.container_script_template = build_template(macros)
    runner.container_script_params_updater = make_params_updater(macros, stem_mode, csv_convert_dir)
    runner.run()
    return runner
