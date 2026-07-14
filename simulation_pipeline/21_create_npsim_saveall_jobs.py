#!/usr/bin/env python3
"""
21_create_npsim_saveall_jobs.py

npsim variant with an empty user particle handler (save-all). Reads the
'npsim_saveall' dataset cards (`generate_datasets npsim_saveall -c <config>`).
"""

import os
import sys
import textwrap

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from simulation_pipeline.job_creator import JobCreator, exension_replacer
from simulation_pipeline.datasets import run_card_pipeline

STAGE = "npsim_saveall"


def create_container_script_template():
    return textwrap.dedent("""\
    #!/bin/bash
    set -e

    echo ">"
    echo "= NPSIM ==================================================================="
    echo "==========================================================================="
    echo "  Running npsim on:"
    echo "    {input_file}"
    echo "  Resulting files:"
    echo "    {output_file}.*"

    mkdir -p $(dirname {output_file})
    cd $(dirname {output_file})/..

    if [ -f "/opt/detector/epic-main/bin/thisepic.sh" ]; then
        source /opt/detector/epic-main/bin/thisepic.sh
    fi
    /usr/bin/time -v npsim --part.userParticleHandler="" --compactFile=$DETECTOR_PATH/epic_craterlake_{beam_config}.xml --runType run --inputFiles {input_file} --outputFile {output_file} --numberOfEvents {events} 2>&1

    echo ""
    echo "=========================================================================="
    echo "Job completed!"
    echo "Output: {output_file}"
    echo "=========================================================================="
    """)


def build(config, card, config_path):
    output_dir = card.get("output") or str(config[STAGE].output)
    runner = JobCreator(
        input_files=list(card["files"]),
        output_file_name_func=exension_replacer('.hepmc3.tree.root', '.edm4hep.root'),
        output_dir=output_dir,
        bind_dirs=config.bind_dirs,
        events=config.event_count,
        container=config['container'],
        beam_config=card.get("energy") or card["slug"],
        slurm_mem_per_cpu=str(config.get("slurm_mem_per_cpu", "2G")),
        farm_out_dir=config.get("farm_out_dir"),
    )
    runner.container_script_template = create_container_script_template()
    runner.run()
    return runner


if __name__ == "__main__":
    run_card_pipeline(STAGE, build, description="Generate npsim (save-all) jobs.")
