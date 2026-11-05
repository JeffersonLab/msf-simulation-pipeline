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

Fan-out (optional): with `bg_merger.jobs_per_file: N` every signal file becomes
N jobs of `event_count` frames each (`--signalSkip` advances through the file,
output `<stem>.c<NNNN>.bg.hepmc3.tree.root`). Without it: one job per file.

Per-dataset signal options live in `datasets.<slug>` of the config:
  signal_status: 7000   # shift the signal's generatorStatus; stage 22 only
                        # simulates statuses it knows, so a shifted signal is a
                        # "ghost": kept in MCParticles, not tracked by Geant4 ->
                        # background-only frames from a merger that needs a
                        # signal file
  signal_freq:   0      # kHz; 0 = exactly one signal per frame (default)
  pool_offset_jobs: 0   # offset added to the job index used for background skips
  jobs_per_file: 80     # overrides bg_merger.jobs_per_file for this dataset

Background skips: every job starts each background source `skip` events further
in, skip = job_index * frames * rate * window, modulo the pool size from
`background_pool_events.<energy>.<status>` (the merger reopens a file at EOF,
so without this every job replays the same stretch of e-gas/Touschek/...).
"""

import hashlib
import json
import os
import re
import sys
import textwrap

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from simulation_pipeline.job_creator import (JobCreator, chunk_token, parse_chunk_token,
                                             chunk_basename_func, chunk_output_func)
from simulation_pipeline.datasets import run_card_pipeline

STAGE = "bg_merger"

# Signal inputs the merger reads (HepMC3 ASCII or ROOT); stripped for output names.
SIGNAL_EXTENSIONS = (".afterburner.hepmc", ".hepmc3.tree.root", ".hepmc3", ".hepmc")
INTEGRATION_WINDOW_NS = 2000

# Cocktail JSONs shipped with this repo (background_cocktails/). A config may
# set background_config_dir to use cocktails from somewhere else.
BACKGROUND_COCKTAILS_DIR_DEFAULT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "background_cocktails")


def cocktail_path_for(config, energy):
    """Path of the cocktail JSON configured for one energy, or None."""
    bg_configs = config.get("background_configs", {})
    if energy not in bg_configs:
        print(f"  WARN: no background_configs entry for '{energy}', skipping.")
        return None
    cocktail_dir = str(config.get("background_config_dir", BACKGROUND_COCKTAILS_DIR_DEFAULT))
    return os.path.join(cocktail_dir, str(bg_configs[energy]))


def load_bg_cocktail(config, energy):
    """Return the list of {file, freq, skip, status} dicts for one energy."""
    cocktail_path = cocktail_path_for(config, energy)
    if cocktail_path is None:
        return None
    if not os.path.isfile(cocktail_path):
        print(f"  WARN: cocktail JSON not found: {cocktail_path}")
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


def build_bg_args(entries, strip_xrootd=False, skips=None):
    """Build the `--bgFile <file> <freq> <skip> <status>` argument string.

    skip and status are cast to int: SignalBackgroundMerger only groups the
    3rd/4th values into one --bgFile when they are pure integers. `skips`
    (one int per entry) overrides the cocktail's skip column.
    """
    parts = []
    for i, e in enumerate(entries):
        file = to_local_path(str(e["file"]), strip_xrootd)
        skip = int(e["skip"]) if skips is None else int(skips[i])
        parts.append(f"--bgFile {file} {e['freq']} {skip} {int(e['status'])}")
    return " \\\n        ".join(parts)


def background_skips(entries, job_index, frames, pool_events):
    """Per-source skip so job `job_index` starts where job `job_index-1` ended.

    rate [kHz] * window [ns] * 1e-6 = mean events per frame. Without a pool
    size for a source (None) the skip stays at the cocktail's value.
    """
    skips = []
    for e in entries:
        pool = (pool_events or {}).get(int(e["status"]))
        if not pool:
            skips.append(int(e["skip"]))
            continue
        per_frame = float(e["freq"]) * INTEGRATION_WINDOW_NS * 1e-6
        skips.append(int(job_index * frames * per_frame) % int(pool))
    return skips


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
    echo "  Slices:    {events}   (2us frames; signal skip {signal_skip}, freq {signal_freq} kHz, status shift {signal_status})"
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
        --signalSkip {signal_skip} \\
        --signalFreq {signal_freq} \\
        --signalStatus {signal_status} \\
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
    bg_cocktail_path = cocktail_path_for(config, energy)
    stage_cfg = config[STAGE]
    frames = int(config.event_count)

    # Per-dataset signal options (see module docstring).
    ds = (config.get("datasets") or {}).get(card["slug"], {}) or {}
    signal_status = int(ds.get("signal_status", 0))
    signal_freq = ds.get("signal_freq", 0)
    pool_offset_jobs = int(ds.get("pool_offset_jobs", 0))
    pool_events = (config.get("background_pool_events") or {}).get(energy)
    pool_events = {int(k): int(v) for k, v in dict(pool_events).items()} if pool_events else None
    if pool_events is None:
        print("  WARN: no background_pool_events for this energy; background skips stay at the cocktail values")

    # Fan-out: jobs_per_file chunks of `frames` slices per signal file.
    jobs_per_file = int(ds.get("jobs_per_file", stage_cfg.get("jobs_per_file", 1)))
    files = list(card["files"])
    if jobs_per_file > 1:
        inputs = [chunk_token(f, j) for f in files for j in range(jobs_per_file)]
    else:
        inputs = files
    print(f"  {len(files)} signal file(s) x {jobs_per_file} job(s) = {len(inputs)} jobs of {frames} frames")

    def job_index_of(token):
        path, chunk = parse_chunk_token(token)
        return files.index(path) * jobs_per_file + (chunk or 0)

    def update_params(params):
        token = params["input_file"]
        path, chunk = parse_chunk_token(token)
        job_index = job_index_of(token)
        skips = background_skips(bg_entries, pool_offset_jobs + job_index, frames, pool_events)
        return {
            **params,
            "input_file": to_local_path(path, strip_xrootd),
            "signal_skip": (chunk or 0) * frames,
            "signal_freq": signal_freq,
            "signal_status": signal_status,
            "bg_args": build_bg_args(bg_entries, strip_xrootd=strip_xrootd, skips=skips),
            "bg_cocktail_path": bg_cocktail_path,
            "rng_seed": derive_seed(params["basename"]),
        }

    output_dir = card.get("output") or str(stage_cfg.output)
    runner = JobCreator(
        input_files=inputs,
        output_file_name_func=chunk_output_func(SIGNAL_EXTENSIONS, '.bg.hepmc3.tree.root'),
        output_dir=output_dir,
        bind_dirs=config.bind_dirs,
        events=frames,
        container=config["container"],
        beam_config=energy or card["slug"],
        slurm_mem_per_cpu=str(config.get("slurm_mem_per_cpu", "2G")),
        slurm_array_throttle=int(config.get("slurm_array_throttle", 0)),
        farm_out_dir=config.get("farm_out_dir"),
    )
    runner.basename_func = chunk_basename_func(SIGNAL_EXTENSIONS)
    runner.container_script_template = create_container_script_template()
    runner.container_script_params_updater = update_params
    runner.run()
    return runner


if __name__ == "__main__":
    run_card_pipeline(STAGE, build,
                      description="Generate SignalBackgroundMerger jobs (signal+bg mixing).")
