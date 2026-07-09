#!/usr/bin/env python3
"""
11_create_background_jobs.py

Generate SignalBackgroundMerger jobs that mix afterburned signal
*.hepmc3.tree.root with the official ePIC background cocktails into 2us
timeframes. Reads the 'bg_merger' dataset cards (`generate_datasets bg_merger -c
<config>`); the per-energy cocktail JSON is looked up in the config's
`background_configs[energy]`.

Output: `*.bg.hepmc3.tree.root`, consumed by 22_create_npsim_background_jobs.py.
The cocktail JSON is the source of truth for stage 22's status-offset flags too.
"""

import hashlib
import json
import os
import re
import sys
import textwrap

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from simulation_pipeline.job_creator import JobCreator, exension_replacer
from simulation_pipeline.datasets import run_card_pipeline

STAGE = "bg_merger"


def load_bg_cocktail(config, energy):
    """Return the list of {file, freq, skip, status} dicts for one energy."""
    bg_configs = config.get("background_configs", {})
    if energy not in bg_configs:
        print(f"  WARN: no background_configs entry for '{energy}', skipping.")
        return None

    cocktail_path = os.path.join(str(config.background_config_dir), str(bg_configs[energy]))
    if not os.path.isfile(cocktail_path):
        print(f"  WARN: cocktail JSON not found: {cocktail_path}")
        print(f"        (it must be visible inside the container too)")
        return None

    with open(cocktail_path, "r") as fh:
        entries = json.load(fh)

    print(f"  Cocktail: {cocktail_path}")
    print(f"    {len(entries)} background sources:")
    for e in entries:
        print(f"      status={e['status']:>5}  freq={e['freq']:<12g}  "
              f"skip={e['skip']:<5}  {os.path.basename(e['file'])}")
    return entries


def to_local_path(url, strip_xrootd):
    """Optionally turn `root://host//abs/path` into a local `/abs/path`."""
    if strip_xrootd:
        return re.sub(r"^root://[^/]+/+", "/", url)
    return url


def build_bg_args(entries, strip_xrootd=False):
    """Build the `--bgFile <file> <freq> <skip> <status>` argument string.

    skip and status are cast to int: SignalBackgroundMerger only groups the
    3rd/4th values into one --bgFile when they are pure integers.
    """
    parts = []
    for e in entries:
        file = to_local_path(str(e["file"]), strip_xrootd)
        parts.append(f"--bgFile {file} {e['freq']} {int(e['skip'])} {int(e['status'])}")
    return " \\\n        ".join(parts)


def derive_seed(basename):
    """Per-file deterministic RNG seed (distinct Poisson realisation per output)."""
    h = hashlib.md5(basename.encode()).hexdigest()[:8]
    return (int(h, 16) % (2**31 - 1)) + 1


def create_container_script_template():
    return textwrap.dedent("""\
    #!/bin/bash
    set -e

    echo ">"
    echo "= SIGNAL+BG MERGE ========================================================="
    echo "==========================================================================="
    echo "  Signal:    {input_file}"
    echo "  Cocktail:  {bg_cocktail_path}"
    echo "  Slices:    {events}   (each = 1 signal event + Poisson bg, 2us frame)"
    echo "  RNG seed:  {rng_seed}"
    echo "  Output:    {output_file}"
    echo

    mkdir -p $(dirname {output_file})
    cd $(dirname {output_file})

    if [ -f "/opt/detector/epic-main/bin/thisepic.sh" ]; then
        source /opt/detector/epic-main/bin/thisepic.sh
    fi

    /usr/bin/time -v SignalBackgroundMerger \\
        --rngSeed {rng_seed} \\
        --nSlices {events} \\
        --signalFile {input_file} \\
        --signalFreq 0 \\
        --signalStatus 0 \\
        {bg_args} \\
        --outputFile {output_file} 2>&1

    echo
    echo "=========================================================================="
    echo "Job completed!"
    echo "Output: {output_file}"
    echo "=========================================================================="
    """)


def build(config, card, config_path):
    energy = card.get("energy")
    bg_entries = load_bg_cocktail(config, energy)
    if bg_entries is None:
        return None

    strip_xrootd = bool(config.get("strip_xrootd_prefix", False))
    bg_args_str = build_bg_args(bg_entries, strip_xrootd=strip_xrootd)
    bg_cocktail_path = os.path.join(
        str(config.background_config_dir), str(config.background_configs[energy]))

    output_dir = card.get("output") or str(config[STAGE].output)
    runner = JobCreator(
        input_files=list(card["files"]),
        output_file_name_func=exension_replacer('.hepmc3.tree.root', '.bg.hepmc3.tree.root'),
        output_dir=output_dir,
        bind_dirs=config.bind_dirs,
        events=config.event_count,
        container=config["container"],
        beam_config=energy or card["slug"],
    )
    runner.container_script_template = create_container_script_template()
    runner.container_script_params_updater = lambda params: {
        **params,
        "bg_args": bg_args_str,
        "bg_cocktail_path": bg_cocktail_path,
        "rng_seed": derive_seed(params["basename"]),
    }
    runner.run()
    return runner


if __name__ == "__main__":
    run_card_pipeline(STAGE, build,
                      description="Generate SignalBackgroundMerger jobs (signal+bg mixing).")
