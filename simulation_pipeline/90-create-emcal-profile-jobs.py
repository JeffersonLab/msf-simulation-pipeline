#!/usr/bin/env python3
"""
90-create-emcal-profile-jobs.py

Generate emcal LL-profile production jobs (ilreco PHASE II). Unlike the other
stages there are NO input files: every job is derived from the config alone —
one job = (energy, impact offset (fx, fy), chunk) -> calsim point-gun run +
in-container histogram fill (python -m g4cal.llprof fill) -> one sparse
partial npz under <profiles.output>/partials/. Requires the eic-full image
with ilreco + g4cal baked in. Merge after download with g4cal.llprof merge.

Usage:
    python 90-create-emcal-profile-jobs.py -c configs/config-ilreco-2026-07.yaml
"""

import os
import sys
import textwrap

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from simulation_pipeline.job_creator import JobCreator, run_pipeline

CONTAINER_TEMPLATE = textwrap.dedent("""\
    #!/bin/bash
    set -e

    echo "= EMCAL LL-PROFILE ======================================================="
    echo "  energy {energy} GeV  gamma at impact ({fx}, {fy})/100 cell  seed {seed}"
    echo "  output: {output_file}"

    source $GEANT4_INS_PATH/bin/geant4.sh
    TMP="${{SLURM_TMPDIR:-/tmp}}/{basename}"
    mkdir -p "$TMP" "$(dirname {output_file})"
    trap 'rm -rf "$TMP"' EXIT

    calsim --nx {nx} --ny {nx} \\
        --crystal-xy {crystal_xy_mm} --crystal-z {crystal_z_mm} --wrap {wrap_mm} \\
        --particle {particle} --e-min {energy} --e-max {energy} \\
        --gun-mode point --gun-x {gun_x} --gun-y {gun_y} \\
        --events {events} --seed {seed} --out-dir "$TMP" --run-id {basename} \\
        --att {att} --lambda {lambda_mm} --veff {veff_mm_ns} \\
        --smear {smear} --en-scale {en_scale} \\
        --meas-a {meas_a} --meas-b {meas_b} --meas-c {meas_c} \\
        --intr-a {intr_a} --intr-b {intr_b} \\
        --thresh-att {thresh_att_gev} --store-min {store_min_gev} \\
        --time off --t-sigma 0.4

    python3 -m g4cal.llprof fill \\
        --hits "$TMP/hits.csv" --run-json "$TMP/run.json" --out {output_file} \\
        --energy {energy} --fx {fx} --fy {fy} --nx {nx} --i-max {node_i_max} \\
        --f-min {f_min} --f-max {f_max} --bins-per-decade {bins_per_decade}

    echo "Job completed! {output_file}"
    """)


def impact_points(prof):
    """One octant of the central cell: fx in [0, impact_max], fy <= fx."""
    step = int(prof.impact_step)
    return [(fx, fy)
            for fx in range(0, int(prof.impact_max) + 1, step)
            for fy in range(0, fx + 1, step)]


def process_energy(config, energy, config_path):
    prof, cs = config.profiles, config.calsim
    pitch = float(cs.pitch_mm)
    # gun at central-cell center + offset; cell (nx//2) center = pitch/2 for even nx
    e_tag = str(energy).replace(".", "p")

    jobs = {}
    for n, (fx, fy) in enumerate(impact_points(prof)):
        for chunk in range(int(prof.jobs_per_impact)):
            tag = f"llprof-e{e_tag}-x{fx:03d}-y{fy:03d}-c{chunk}"
            jobs[tag] = {
                "energy": energy, "fx": fx, "fy": fy,
                "gun_x": round(pitch * (0.5 + fx / 100.0), 6),
                "gun_y": round(pitch * (0.5 + fy / 100.0), 6),
                "seed": int(prof.seed_base) + int(float(energy) * 10) * 100000
                        + n * int(prof.jobs_per_impact) + chunk,
                "nx": int(cs.nx), "particle": prof.particle,
                "node_i_max": int(prof.node_i_max),
                "f_min": prof.f_min, "f_max": prof.f_max,
                "bins_per_decade": prof.bins_per_decade,
                "crystal_xy_mm": cs.crystal_xy_mm, "crystal_z_mm": cs.crystal_z_mm,
                "wrap_mm": cs.wrap_mm, "att": cs.att, "lambda_mm": cs.lambda_mm,
                "veff_mm_ns": cs.veff_mm_ns, "smear": cs.smear,
                "en_scale": cs.en_scale,
                "meas_a": cs.meas[0], "meas_b": cs.meas[1], "meas_c": cs.meas[2],
                "intr_a": cs.intrinsic[0], "intr_b": cs.intrinsic[1],
                "thresh_att_gev": cs.thresh_att_gev,
                "store_min_gev": cs.store_min_gev,
            }

    def output_name(input_file, output_dir):
        return os.path.join(output_dir, "partials",
                            os.path.basename(input_file) + ".npz")

    def params_updater(params):
        params.update(jobs[os.path.basename(params["input_file"])])
        return params

    runner = JobCreator(
        input_files=list(jobs),          # synthetic tags, never read as files
        output_file_name_func=output_name,
        output_dir=str(prof.output),
        bind_dirs=list(config.bind_dirs),
        events=int(prof.events_per_job),
        container=str(config.container),
        beam_config=f"llprof-e{e_tag}",
        slurm_time=str(config.slurm_time if isinstance(config.slurm_time, str)
                       else config.slurm_time[energy]),
        slurm_mem_per_cpu=str(config.get("slurm_mem_per_cpu", "2G")),
        farm_out_dir=config.get("farm_out_dir"),
    )
    runner.container_script_template = CONTAINER_TEMPLATE
    runner.container_script_params_updater = params_updater
    runner.run()
    return runner


if __name__ == "__main__":
    run_pipeline(process_energy, description="Generate emcal LL-profile jobs.")
