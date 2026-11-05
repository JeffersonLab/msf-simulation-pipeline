#!/usr/bin/env python3
"""
13_create_timeframe_builder_jobs.py

Mix Geant4-simulated EDM4hep events into timeframes with TimeframeBuilder
(`timeframe_builder`, eic/TimeframeBuilder; 0.9.2 ships in eic_xl:26.10+).
Background comes from the per-source pools of stage 23; the signal (if any)
from background-free npsim output of stage 20.

The config block name is the optional first argument (default `tfb_frames`),
so one config can carry several frame flavours (200 ns, 2 us, background-only):

    python simulation_pipeline/13_create_timeframe_builder_jobs.py tfb_200ns -c cfg

Cards (same name as the block):
  * signal dataset:  `generate_datasets tfb_200ns --files <signal edm4hep> --energy E --slug S`
                     (or the local glob of `tfb_200ns.input`); each signal file
                     becomes `jobs_per_file` jobs of `frames_per_job` frames,
                     exactly one signal event per frame, `skip` walking through
                     the file.
  * background-only: `generate_datasets bkg_2us --empty --energy E --slug S`;
                     `background_only_jobs` jobs of `frames_per_job` frames.

Config block:

    tfb_frames:
      input:   "${npsim.output}"               # background-free signal (stage 20)
      pattern: "*.edm4hep.root"
      output:  "${base_dir}/dd4hep-tfb/${energy}/${slug}"
      pools:   "${npsim_bgsource.output}"      # <pools>/<source name>/*.edm4hep.root
      frame_ns: 2000                           # timeframe length
      signal_time_ns: null                     # pin the signal at this time; needs the
                                               # fixed_time_offset patch (tfb_binary). null = random
      frames_per_job: 500
      jobs_per_file: 1                         # per stage-20 output file; datasets.<slug>.tfb_jobs_per_file overrides
      background_only_jobs: 80                 # datasets.<slug>.background_only_jobs overrides
      bunch_crossing: true
      bunch_crossing_period_ns: 10.15          # 98.5 MHz
      attach_to_beam: true                     # z/c time correction, as in the ePIC configs
      tfb_binary: timeframe_builder            # path of a patched build, if not the container's

Each job gets its own YAML (`jobs/<name>.tfb.yaml`): one source per cocktail
entry with `mean_event_frequency` = cocktail kHz x 1e-6 (TimeframeBuilder takes
events/ns), `generator_status_offset` = cocktail status (2000 SynRad, 3000
e-gas, 4000 Coulomb, 5000 Touschek, 6000 p-gas), `repeat_on_eof: true`, and a
per-job `skip` so consecutive jobs read consecutive stretches of each pool
(0.9.2 wraps `skip` modulo the pool size). The per-source pool file list is
expanded by TimeframeBuilder from the glob.

Output: `<slug>_<energy>_<NNNN>.edm4hep.root` with a podio `events` tree
(MCParticles, SubEventHeaders, Sim*Hit collections), consumed by stage 30.
"""

import hashlib
import json
import os
import sys
import textwrap

import yaml

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from simulation_pipeline.job_creator import JobCreator, chunk_token, parse_chunk_token
from simulation_pipeline.datasets import run_card_pipeline

STAGE = "tfb_frames"      # config block; replaced by the optional first CLI argument

BACKGROUND_COCKTAILS_DIR_DEFAULT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "background_cocktails")

# Beam attachment per cocktail status (mirrors eic/TimeframeBuilder
# configs/config_epic_10x275.yml): electron-side sources travel along -z,
# proton beam-gas along the +z crossing angle. SynRad photons are emitted
# upstream of the IP and arrive with the electron bunch.
BEAM_ATTACH = {
    2000: {"beam_angle": -3.141592653589793, "beam_spread": 0.003},
    3000: {"beam_angle": -3.141592653589793, "beam_spread": 0.003},
    4000: {"beam_angle": -3.141592653589793, "beam_spread": 0.003},
    5000: {"beam_angle": -3.141592653589793, "beam_spread": 0.003},
    6000: {"beam_angle": 0.025, "beam_spread": 0.030},
}
BEAM_SPEED_MM_NS = 299.792458

EMPTY_TOKEN = "background-only"


def is_empty_input(path):
    """JobCreator may absolutize input tokens; compare the last component."""
    return os.path.basename(str(path)) == EMPTY_TOKEN


def load_cocktail(config, energy):
    bg_configs = config.get("background_configs", {})
    if energy not in bg_configs:
        print(f"  WARN: no background_configs entry for '{energy}', skipping.")
        return None
    cocktail_dir = str(config.get("background_config_dir", BACKGROUND_COCKTAILS_DIR_DEFAULT))
    path = os.path.join(cocktail_dir, str(bg_configs[energy]))
    if not os.path.isfile(path):
        print(f"  WARN: cocktail JSON not found: {path}")
        return None
    with open(path) as fh:
        return json.load(fh)


def derive_seed(text):
    h = hashlib.md5(text.encode()).hexdigest()[:8]
    return (int(h, 16) % (2**31 - 1)) + 1


def source_names(config):
    """status -> pool directory name, from npsim_bgsource.sources."""
    names = {}
    for status, scfg in dict(config.get("npsim_bgsource", {}).get("sources", {})).items():
        names[int(status)] = str(scfg["name"])
    return names


def background_sources(entries, names, pools_dir, job_index, frames, frame_ns, stage_cfg):
    """TimeframeBuilder source blocks for the cocktail, with per-job skips."""
    attach = bool(stage_cfg.get("attach_to_beam", True))
    bunch = bool(stage_cfg.get("bunch_crossing", True))
    sources = []
    for e in entries:
        status = int(e["status"])
        name = names.get(status)
        if name is None:
            raise SystemExit(f"no npsim_bgsource.sources name for status {status}")
        freq_ghz = float(e["freq"]) * 1e-6
        per_frame = freq_ghz * frame_ns
        src = {
            "name": name,
            "input_files": [os.path.join(pools_dir, name, "*.edm4hep.root")],
            "static_number_of_events": False,
            "mean_event_frequency": freq_ghz,
            "use_bunch_crossing": bunch,
            "generator_status_offset": status,
            "repeat_on_eof": True,
            # consecutive jobs read consecutive stretches of the pool
            "skip": int(job_index * frames * per_frame),
        }
        if attach and status in BEAM_ATTACH:
            src.update({"attach_to_beam": True, "beam_speed": BEAM_SPEED_MM_NS, **BEAM_ATTACH[status]})
        sources.append(src)
    return sources


def create_container_script_template():
    return textwrap.dedent("""\
    #!/bin/bash
    set -e

    echo ">"
    echo "= TIMEFRAME BUILDER (EDM4hep) ============================================="
    echo "==========================================================================="
    echo "  Config:   {tfb_yaml}"
    echo "  Signal:   {signal_desc}"
    echo "  Pools:    {pools_dir}"
    echo "  Frames:   {events} x {frame_ns} ns   seed {seed}"
    echo "  Output:   {output_file}"
    echo

    mkdir -p $(dirname {output_part})
    cd $(dirname {output_file})

    # Library paths for a timeframe_builder built outside the image (RPATH stripped).
    if [ -f "/opt/detector/epic-main/bin/thisepic.sh" ]; then
        source /opt/detector/epic-main/bin/thisepic.sh
    fi

    /usr/bin/time -v {tfb_binary} \\
        --config {tfb_yaml} \\
        -o {output_part} \\
        -n {events} \\
        -d {frame_ns} \\
        -p {bunch_period_ns} \\
        --random-seed {seed} 2>&1

    # Written into partial/ (same filesystem, outside every *.edm4hep.root glob),
    # then renamed: a killed job never leaves a half file where cards look.
    mv {output_part} {output_file}

    echo
    echo "=========================================================================="
    echo "Job completed!"
    echo "Output: {output_file}"
    echo "=========================================================================="
    """)


def build(config, card, config_path):
    energy = card.get("energy")
    slug = card["slug"]
    entries = load_cocktail(config, energy)
    if entries is None:
        return None
    stage_cfg = config[STAGE]
    frames = int(stage_cfg.get("frames_per_job", config.event_count))
    frame_ns = float(stage_cfg.get("frame_ns", 2000.0))
    # Pinning the signal needs TimeframeBuilder with the per-source
    # `fixed_time_offset` option (not in 0.9.2 as shipped; see tfb_binary).
    signal_time_ns = stage_cfg.get("signal_time_ns")
    tfb_binary = str(stage_cfg.get("tfb_binary", "timeframe_builder"))
    pools_dir = str(stage_cfg.pools)
    names = source_names(config)
    bunch_period = float(stage_cfg.get("bunch_crossing_period_ns", 10.15))
    output_dir = card.get("output") or str(stage_cfg.output)
    ds = (config.get("datasets") or {}).get(slug, {}) or {}
    # Signal files here are stage-20 output, already chunked to `event_count`
    # events, so the default is one TFB job per file. `datasets.<slug>.jobs_per_file`
    # is the *generator-file* fan-out of stages 11/20 and is deliberately NOT
    # inherited; `datasets.<slug>.tfb_jobs_per_file` overrides if ever needed.
    jobs_per_file = int(ds.get("tfb_jobs_per_file", stage_cfg.get("jobs_per_file", 1)))
    pool_offset_jobs = int(ds.get("pool_offset_jobs", 0))

    files = list(card.get("files") or [])
    if files:
        tokens = [chunk_token(f, j) for f in files for j in range(jobs_per_file)]
        print(f"  {len(files)} signal file(s) x {jobs_per_file} job(s) = {len(tokens)} jobs of {frames} frames")
    else:
        n_jobs = int(ds.get("background_only_jobs", stage_cfg.get("background_only_jobs", 1)))
        tokens = [chunk_token(EMPTY_TOKEN, j) for j in range(n_jobs)]
        print(f"  background-only: {n_jobs} jobs of {frames} frames")

    def job_index_of(token):
        path, chunk = parse_chunk_token(token)
        if is_empty_input(path):
            return chunk or 0
        return files.index(path) * jobs_per_file + (chunk or 0)

    def basename(token):
        return f"{slug}_{energy}_{job_index_of(token):04d}"

    def output_name(token, out_dir):
        return os.path.join(out_dir, basename(token) + ".edm4hep.root")

    jobs_dir = os.path.join(output_dir, "jobs")
    os.makedirs(jobs_dir, exist_ok=True)

    def update_params(params):
        token = params["input_file"]
        path, chunk = parse_chunk_token(token)
        job_index = job_index_of(token)
        name = basename(token)
        seed = derive_seed(f"{energy}/{slug}/{job_index}")
        sources = []
        if not is_empty_input(path):
            signal = {
                "name": "signal",
                "input_files": [path],
                "skip": (chunk or 0) * frames,
                "static_number_of_events": True,
                "static_events_per_timeframe": 1,
                "use_bunch_crossing": bool(stage_cfg.get("bunch_crossing", True)),
                "generator_status_offset": 0,
                "keep_weight": True,
                "repeat_on_eof": False,
            }
            if signal_time_ns is not None:
                signal["fixed_time_offset"] = float(signal_time_ns)
            sources.append(signal)
        sources += background_sources(entries, names, pools_dir, pool_offset_jobs + job_index,
                                      frames, frame_ns, stage_cfg)
        # TimeframeBuilder takes the YAML `output_file` over the `-o` option, so the
        # YAML must name the partial path too (the job renames it at the end).
        output_part = os.path.join(os.path.dirname(params["output_file"]), "partial",
                                   os.path.basename(params["output_file"]))
        tfb_cfg = {
            "output_file": output_part,
            "max_events": frames,
            "timeframe_duration": frame_ns,
            "bunch_crossing_period": bunch_period,
            "random_seed": seed,
            "introduce_offsets": True,
            "sources": sources,
        }
        tfb_yaml = os.path.join(jobs_dir, name + ".tfb.yaml")
        with open(tfb_yaml, "w") as fh:
            fh.write(f"# {slug} {energy} job {job_index}: {frames} frames of {frame_ns:g} ns\n")
            yaml.safe_dump(tfb_cfg, fh, sort_keys=False)
        signal_desc = "none (background-only)" if is_empty_input(path) else (
            f"{path} skip {(chunk or 0) * frames}, 1 per frame"
            + (f" at t = {float(signal_time_ns):g} ns" if signal_time_ns is not None else ""))
        return {
            **params,
            "input_file": path,
            "output_part": output_part,
            "tfb_yaml": tfb_yaml,
            "tfb_binary": tfb_binary,
            "signal_desc": signal_desc,
            "pools_dir": pools_dir,
            "frame_ns": f"{frame_ns:g}",
            "bunch_period_ns": bunch_period,
            "seed": seed,
        }

    runner = JobCreator(
        input_files=tokens,
        output_file_name_func=output_name,
        output_dir=output_dir,
        bind_dirs=config.bind_dirs,
        events=frames,
        container=config["container"],
        beam_config=energy or slug,
        slurm_mem_per_cpu=str(stage_cfg.get("slurm_mem_per_cpu", config.get("slurm_mem_per_cpu", "2G"))),
        slurm_array_throttle=int(config.get("slurm_array_throttle", 0)),
        farm_out_dir=config.get("farm_out_dir"),
    )
    runner.basename_func = basename
    runner.container_script_template = create_container_script_template()
    runner.container_script_params_updater = update_params
    runner.run()
    return runner


if __name__ == "__main__":
    # Optional first argument = config block name (tfb_200ns, tfb_2us, ...);
    # the remaining arguments are the standard -c/--datasets of every stage.
    if len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
        STAGE = sys.argv.pop(1)
    run_card_pipeline(STAGE, build,
                      description="Generate TimeframeBuilder (EDM4hep) timeframe jobs "
                                  "for the config block named by the first argument "
                                  "(default tfb_frames).")
