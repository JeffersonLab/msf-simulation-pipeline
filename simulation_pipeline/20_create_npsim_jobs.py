#!/usr/bin/env python3
"""
20_create_npsim_jobs.py

Generate npsim (dd4hep) jobs. Reads the 'npsim' dataset cards
(`generate_datasets npsim -c <config>`) and runs npsim on each afterburned
*.hepmc3.tree.root with the standard EIC tracking-volume particle handler.
"""

import os
import sys
import textwrap

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from simulation_pipeline.job_creator import (JobCreator, exension_replacer, chunk_token,
                                             parse_chunk_token, chunk_basename_func,
                                             chunk_output_func)
from simulation_pipeline.datasets import run_card_pipeline

STAGE = "npsim"


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
    /usr/bin/time -v npsim --part.userParticleHandler="Geant4TVUserParticleHandler" --compactFile=$DETECTOR_PATH/epic_craterlake_{beam_config}.xml --runType run --hepmc3.useHepMC3 true --inputFiles {input_file} --skipNEvents {skip_events} --outputFile {output_file} --numberOfEvents {events} 2>&1

    echo ""
    echo "=========================================================================="
    echo "Job completed!"
    echo "Output: {output_file}"
    echo "=========================================================================="
    """)


INPUT_EXTENSIONS = (".afterburner.hepmc", ".hepmc3.tree.root", ".hepmc3", ".hepmc")


def build(config, card, config_path):
    """One job per input file, or `jobs_per_file` chunks of `event_count`
    events each (`--skipNEvents` advances through the file; output
    `<stem>.c<NNNN>.edm4hep.root`). `datasets.<slug>.jobs_per_file` overrides
    `npsim.jobs_per_file`."""
    stage_cfg = config[STAGE]
    events = int(config.event_count)
    ds = (config.get("datasets") or {}).get(card["slug"], {}) or {}
    jobs_per_file = int(ds.get("jobs_per_file", stage_cfg.get("jobs_per_file", 1)))
    files = list(card["files"])
    inputs = [chunk_token(f, j) for f in files for j in range(jobs_per_file)] \
        if jobs_per_file > 1 else files
    print(f"  {len(files)} input file(s) x {jobs_per_file} job(s) = {len(inputs)} jobs of {events} events")

    def update_params(params):
        path, chunk = parse_chunk_token(params["input_file"])
        return {**params, "input_file": path, "skip_events": (chunk or 0) * events}

    output_dir = card.get("output") or str(stage_cfg.output)
    runner = JobCreator(
        input_files=inputs,
        output_file_name_func=chunk_output_func(INPUT_EXTENSIONS, '.edm4hep.root'),
        output_dir=output_dir,
        bind_dirs=config.bind_dirs,
        events=events,
        container=config['container'],
        beam_config=card.get("energy") or card["slug"],
        slurm_mem_per_cpu=str(stage_cfg.get("slurm_mem_per_cpu", config.get("slurm_mem_per_cpu", "2G"))),
        slurm_array_throttle=int(config.get("slurm_array_throttle", 0)),
        farm_out_dir=config.get("farm_out_dir"),
    )
    runner.basename_func = chunk_basename_func(INPUT_EXTENSIONS)
    runner.container_script_template = create_container_script_template()
    runner.container_script_params_updater = update_params
    runner.run()
    return runner


if __name__ == "__main__":
    run_card_pipeline(STAGE, build, description="Generate npsim jobs.")
