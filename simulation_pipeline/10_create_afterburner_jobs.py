#!/usr/bin/env python3
"""
10_create_afterburner_jobs.py

Generate afterburner (abconv) jobs. Reads the 'afterburner' dataset cards
(produced by `generate_datasets afterburner -c <config>`) and runs abconv on each
input *.hepmc, applying the per-energy IP6 preset flag.
"""

import os
import sys
import textwrap

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from simulation_pipeline.job_creator import JobCreator, exension_replacer
from simulation_pipeline.datasets import run_card_pipeline

STAGE = "afterburner"


def create_container_script_template():
    return textwrap.dedent("""\
    #!/bin/bash
    set -e

    mkdir -p $(dirname {output_file})

    echo ">"
    echo "=ABCONV===================================================================="
    echo "==========================================================================="
    echo "  Running afterburner on:"
    echo "    {input_file}"
    echo "  Resulting files:"
    echo "    {output_file}.*"
    /usr/bin/time -v abconv {afterburn_preset_flag} {input_file} --output {output_file} 2>&1

    echo ""
    echo "=========================================================================="
    echo "Job completed!"
    echo "Output: {output_file}"
    echo "=========================================================================="
    """)


def update_params(params):
    """Pick the abconv IP6 preset from the input filename's beam energy."""
    flag = ""
    if "10x130" in params['input_file']:
        flag = "--preset=ip6_ep_130x10"
    if "5x41" in params['input_file']:
        flag = "--preset=ip6_hidiv_41x5"
    if "9x100" in params['input_file']:
        flag = "--preset=ip6_hidiv_100x10"
    if "9x130" in params['input_file']:
        flag = "--preset=ip6_ep_130x10"
    if "9x275" in params['input_file']:
        flag = "--preset=ip6_hidiv_275x9"
    return {**params, 'afterburn_preset_flag': flag}


def build(config, card, config_path):
    output_dir = card.get("output") or str(config[STAGE].output)
    runner = JobCreator(
        input_files=list(card["files"]),
        output_file_name_func=exension_replacer('.hepmc', ''),  # abconv adds extension itself
        output_dir=output_dir,
        bind_dirs=config.bind_dirs,
        events=config.event_count,
        container=config['container'],
        slurm_mem_per_cpu=str(config.get("slurm_mem_per_cpu", "2G")),
        farm_out_dir=config.get("farm_out_dir"),
    )
    runner.container_script_params_updater = update_params
    runner.container_script_template = create_container_script_template()
    runner.run()
    return runner


if __name__ == "__main__":
    run_card_pipeline(STAGE, build, description="Generate afterburner jobs.")
