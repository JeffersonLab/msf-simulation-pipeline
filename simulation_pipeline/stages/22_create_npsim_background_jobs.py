#!/usr/bin/env python3
"""
22_create_npsim_background_jobs.py

Run npsim on the merged signal+background *.bg.hepmc3.tree.root files produced
by stage 11. Reads the 'npsim_background' dataset cards
(`generate_datasets npsim_background -c <config>`).

The merger offsets each background source's generator-status (synrad=2000,
ebrems=3000, ...). npsim would silently drop those unknown codes, so the
alternative stable/decay status lists are derived from the same per-energy
cocktail JSON stage 11 used and passed to npsim.

Output: `*.edm4hep.root`, consumed by stage 30 (eicrecon) as usual.
"""

import json
import os
import sys
import textwrap

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from simulation_pipeline.job_creator import JobCreator, exension_replacer
from simulation_pipeline.datasets import run_card_pipeline

STAGE = "npsim_background"


def load_status_offsets(config, energy):
    """Return the list of integer status offsets from the cocktail JSON."""
    bg_configs = config.get("background_configs", {})
    if energy not in bg_configs:
        print(f"  WARN: no background_configs entry for '{energy}', skipping.")
        return None

    cocktail_path = os.path.join(str(config.background_config_dir), str(bg_configs[energy]))
    if not os.path.isfile(cocktail_path):
        print(f"  WARN: cocktail JSON not found: {cocktail_path}")
        return None

    with open(cocktail_path, "r") as fh:
        entries = json.load(fh)

    offsets = sorted(int(e["status"]) for e in entries)
    print(f"  Cocktail status offsets: {offsets}")
    return offsets


def build_status_lists(bg_offsets, signal_offset=0):
    """Return ('1 2001 3001 ...', '2 2002 3002 ...') for npsim's status flags."""
    all_offsets = [signal_offset] + list(bg_offsets)
    stable = " ".join(str(off + 1) for off in all_offsets)
    decay = " ".join(str(off + 2) for off in all_offsets)
    return stable, decay


def create_container_script_template():
    return textwrap.dedent("""\
    #!/bin/bash
    set -e

    echo ">"
    echo "= NPSIM (bg-merged) ======================================================="
    echo "==========================================================================="
    echo "  Input:    {input_file}"
    echo "  Output:   {output_file}"
    echo "  Stable:   {alt_stable_statuses}"
    echo "  Decay:    {alt_decay_statuses}"
    echo

    mkdir -p $(dirname {output_file})
    cd $(dirname {output_file})/..

    if [ -f "/opt/detector/epic-main/bin/thisepic.sh" ]; then
        source /opt/detector/epic-main/bin/thisepic.sh
    fi

    /usr/bin/time -v npsim \\
        --part.userParticleHandler="" \\
        --compactFile=$DETECTOR_PATH/epic_craterlake_{beam_config}.xml \\
        --runType batch \\
        --hepmc3.useHepMC3 true \\
        --physics.alternativeStableStatuses "{alt_stable_statuses}" \\
        --physics.alternativeDecayStatuses  "{alt_decay_statuses}" \\
        --inputFiles {input_file} \\
        --outputFile {output_file} \\
        --numberOfEvents {events} 2>&1

    echo
    echo "=========================================================================="
    echo "Job completed!"
    echo "Output: {output_file}"
    echo "=========================================================================="
    """)


def build(config, card, config_path):
    energy = card.get("energy")
    bg_offsets = load_status_offsets(config, energy)
    if bg_offsets is None:
        return None

    stable_str, decay_str = build_status_lists(bg_offsets, signal_offset=0)
    print(f"  --physics.alternativeStableStatuses \"{stable_str}\"")
    print(f"  --physics.alternativeDecayStatuses  \"{decay_str}\"")

    output_dir = card.get("output") or str(config[STAGE].output)
    runner = JobCreator(
        input_files=list(card["files"]),
        output_file_name_func=exension_replacer('.bg.hepmc3.tree.root', '.edm4hep.root'),
        output_dir=output_dir,
        bind_dirs=config.bind_dirs,
        events=config.event_count,
        container=config["container"],
        beam_config=energy or card["slug"],
    )
    runner.container_script_template = create_container_script_template()
    runner.container_script_params_updater = lambda params: {
        **params,
        "alt_stable_statuses": stable_str,
        "alt_decay_statuses": decay_str,
    }
    runner.run()
    return runner


if __name__ == "__main__":
    run_card_pipeline(STAGE, build,
                      description="Generate npsim jobs for background-merged input.")
