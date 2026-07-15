#!/usr/bin/env python3
"""
30_create_eicrecon_jobs.py

Generate eicrecon jobs. Reads the 'eicrecon' dataset cards
(`generate_datasets eicrecon -c <config>`) and runs eicrecon on each dd4hep
*.edm4hep.root, producing *.edm4eic.root.
"""

import os
import sys
import textwrap

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from simulation_pipeline.job_creator import JobCreator, exension_replacer
from simulation_pipeline.datasets import run_card_pipeline

STAGE = "eicrecon"


def create_container_script_template():
    return textwrap.dedent("""\
    #!/bin/bash
    set -e

    mkdir -p $(dirname {output_file})

    echo ">"
    echo "= EICRECON ==================================================================="
    echo "==========================================================================="
    echo "  Running eicrecon on:"
    echo "    {input_file}"
    echo "  Resulting files:"
    echo "    {output_file}.*"

    if [ -f "/opt/detector/epic-main/bin/thisepic.sh" ]; then
        source /opt/detector/epic-main/bin/thisepic.sh
    fi
    cd $(dirname {output_file})/..
    /usr/bin/time -v /usr/bin/time -v eicrecon -Pdd4hep:xml_files=$DETECTOR_PATH/epic_craterlake_{beam_config}.xml  -Ppodio:output_file={output_file}  {eicrecon_flags}  {input_file} 2>&1

    echo ""
    echo "=========================================================================="
    echo "Job completed!"
    echo "Output: {output_file}"
    echo "=========================================================================="
    """)


def build(config, card, config_path):
    output_dir = card.get("output") or str(config[STAGE].output)
    # extra -P arguments from the config ('eicrecon.flags' list), e.g. the B0
    # truth-seeding recovery parameters for the Lambda -> p pi- analysis.
    # Substituted into the template here because JobCreator formats it with
    # per-file params only.
    flags = " ".join(str(f) for f in config[STAGE].get("flags", []))
    runner = JobCreator(
        input_files=list(card["files"]),
        output_file_name_func=exension_replacer('.edm4hep.root', '.edm4eic.root'),
        output_dir=output_dir,
        bind_dirs=config.bind_dirs,
        events=config.event_count,
        container=config['container'],
        beam_config=card.get("energy") or card["slug"],
        slurm_mem_per_cpu=str(config.get("slurm_mem_per_cpu", "2G")),
        farm_out_dir=config.get("farm_out_dir"),
    )
    runner.container_script_template = (
        create_container_script_template().replace("{eicrecon_flags}", flags))
    runner.run()
    return runner


if __name__ == "__main__":
    run_card_pipeline(STAGE, build, description="Generate eicrecon jobs.")
