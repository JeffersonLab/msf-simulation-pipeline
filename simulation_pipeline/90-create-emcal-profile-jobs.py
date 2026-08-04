#!/usr/bin/env python3
"""
90-create-emcal-profile-jobs.py

Generate emcal LL-profile production jobs (ilreco PHASE II). Unlike the other
stages there are NO input files: every job is derived from the config alone -
one job = (energy, impact point, chunk) -> calsim point-gun run + in-container
histogram fill (python -m g4cal.llprof fill) -> one sparse partial npz under
<profiles.output>/partials/. Raw calsim output stays in job scratch; the npz is
the deliverable, written atomically by llprof (an existing npz is complete, so
missing jobs are visible by comparing partials/ against the job count and can
simply be resubmitted). Requires the eic-full image with ilreco + g4cal baked in.

After download, merge per energy:
    python -m g4cal.llprof merge --out profile_e5.npz profiles/5/partials/*.npz

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
    echo "  energy {energy} GeV  gamma at impact ({impact_x}, {impact_y})/100 cell  seed {seed}"
    echo "  output: {output_file}"

    source $GEANT4_INS_PATH/bin/geant4.sh

    # Per-job scratch for the raw calsim CSVs (~1 GB, deleted at job end).
    # NOT /tmp (farm admin requirement) and NOT $PWD: slurm jobs inherit the
    # submit directory as cwd, which littered the repo the jobs were submitted
    # from. Leftovers from killed jobs are safe to delete wholesale.
    SCRATCH="{scratch_dir}/{basename}"
    mkdir -p "$SCRATCH" "$(dirname {output_file})"
    trap 'rm -rf "$SCRATCH"' EXIT
    # bash skips the EXIT trap on untrapped fatal signals; slurm sends TERM on
    # walltime/scancel - route it (and INT/HUP) into a normal exit so cleanup runs.
    # Only SIGKILL (OOM, kill -9) can still leave a scratch dir behind.
    trap 'exit 143' TERM INT HUP

    calsim --crystals-nx {crystals_nx} --crystals-ny {crystals_nx} \\
        --crystal-side-mm {crystal_side_mm} --crystal-length-mm {crystal_length_mm} \\
        --wrap-thickness-mm {wrap_thickness_mm} \\
        --particle {particle} --energy-gev {energy} \\
        --gun-mode point --gun-x-mm {gun_x_mm} --gun-y-mm {gun_y_mm} \\
        --events {events} --seed {seed} --out-dir "$SCRATCH" --run-id {basename} \\
        --hadronic {hadronic} \\
        --attenuation {attenuation} --atten-length-mm {atten_length_mm} \\
        --light-speed-mm-ns {light_speed_mm_ns} \\
        --smearing {smearing} --energy-scale {energy_scale} \\
        --measured-res-stochastic {measured_res_stochastic} \\
        --measured-res-noise {measured_res_noise} \\
        --measured-res-constant {measured_res_constant} \\
        --intrinsic-res-stochastic {intrinsic_res_stochastic} \\
        --intrinsic-res-noise {intrinsic_res_noise} \\
        --hit-threshold-gev {hit_threshold_gev} --store-min-gev {store_min_gev} \\
        --timing off --time-sigma-ns 0.4

    python3 -m g4cal.llprof fill \\
        --hits "$SCRATCH/hits.csv" --run-json "$SCRATCH/run.json" --out {output_file} \\
        --energy {energy} --impact-x {impact_x} --impact-y {impact_y} \\
        --crystals-nx {crystals_nx} --max-cell-offset {max_cell_offset} \\
        --f-min {f_min} --f-max {f_max} --bins-per-decade {bins_per_decade}

    echo "Job completed! {output_file}"
    """)


def impact_points(prof):
    """Gun positions inside the central cell, in 0.01-cell units from its center.

    One octant is enough: impact_x in [0, impact_max], impact_y <= impact_x - every
    other position in the cell is a mirror/axis-swap image of one of these.
    """
    step = int(prof.impact_step)
    return [(impact_x, impact_y)
            for impact_x in range(0, int(prof.impact_max) + 1, step)
            for impact_y in range(0, impact_x + 1, step)]


def process_energy(config, energy, config_path):
    prof, cs = config.profiles, config.calsim
    pitch = float(cs.pitch_mm)
    e_tag = str(energy).replace(".", "p")

    jobs = {}
    for point_i, (impact_x, impact_y) in enumerate(impact_points(prof)):
        for chunk in range(int(prof.jobs_per_impact)):
            tag = f"llprof-e{e_tag}-x{impact_x:03d}-y{impact_y:03d}-c{chunk}"
            jobs[tag] = {
                "energy": energy, "impact_x": impact_x, "impact_y": impact_y,
                # cell (nx//2) center sits at pitch/2 for even nx; add the impact offset
                "gun_x_mm": round(pitch * (0.5 + impact_x / 100.0), 6),
                "gun_y_mm": round(pitch * (0.5 + impact_y / 100.0), 6),
                "seed": int(prof.seed_base) + int(float(energy) * 10) * 100000
                        + point_i * int(prof.jobs_per_impact) + chunk,
                "crystals_nx": int(cs.crystals_nx), "particle": prof.particle,
                "max_cell_offset": int(prof.max_cell_offset),
                "f_min": prof.f_min, "f_max": prof.f_max,
                "bins_per_decade": prof.bins_per_decade,
                "crystal_side_mm": cs.crystal_side_mm,
                "crystal_length_mm": cs.crystal_length_mm,
                "wrap_thickness_mm": cs.wrap_thickness_mm,
                "hadronic": cs.hadronic,
                "attenuation": cs.attenuation, "atten_length_mm": cs.atten_length_mm,
                "light_speed_mm_ns": cs.light_speed_mm_ns,
                "smearing": cs.smearing, "energy_scale": cs.energy_scale,
                "measured_res_stochastic": cs.measured_res.stochastic,
                "measured_res_noise": cs.measured_res.noise,
                "measured_res_constant": cs.measured_res.constant,
                "intrinsic_res_stochastic": cs.intrinsic_res.stochastic,
                "intrinsic_res_noise": cs.intrinsic_res.noise,
                "hit_threshold_gev": cs.hit_threshold_gev,
                "store_min_gev": cs.store_min_gev,
            }

    def output_name(input_file, output_dir):
        return os.path.join(output_dir, "partials", os.path.basename(input_file) + ".npz")

    def params_updater(params):
        params.update(jobs[os.path.basename(params["input_file"])])
        params["scratch_dir"] = str(prof.scratch)
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
    print(f"Completeness check:  ls {prof.output}/partials/*.npz | wc -l  "
          f"must equal {len(jobs)}")
    return runner


if __name__ == "__main__":
    run_pipeline(process_energy, description="Generate emcal LL-profile jobs.")
