#!/usr/bin/env python3
"""
23_create_npsim_bgsource_jobs.py

Simulate each background source of a cocktail on its own through npsim: the
per-source Geant4 "pools" that TimeframeBuilder (stage 13) later mixes into
2 us frames at the cocktail rates. No cards: the cocktail JSON for the energy
is the input list (like stages 01/02, this is a pre-card producer).

    python simulation_pipeline/23_create_npsim_bgsource_jobs.py -c cfg [--energy 10x100]

Config block:

    npsim_bgsource:
      output: "${base_dir}/pools-tfb/${energy}"     # + /<source name>/
      sources:                                     # keyed by cocktail status
        2000: {name: synrad,   pool_events: all,    events_per_job: 50000, replicas: 1, slurm_mem_per_cpu: 4G}
        3000: {name: egas,     pool_events: 300000, events_per_job: 20000}
        4000: {name: coulomb,  pool_events: 3000,   events_per_job: 1000}
        5000: {name: touschek, pool_events: 25000,  events_per_job: 1000}
        6000: {name: pgas,     pool_events: 3500,   events_per_job: 1000}

`pool_events` is how many generator events of that source go through Geant4
(`all` = the whole file, size taken from `background_pool_events.<energy>`).
`replicas` re-simulates the same events with different Geant4 seeds; only
SynRad needs it (its pool is ~31 frame-equivalents at 10x gold), everything
else has millions of unused events.

Jobs: one per (chunk, replica); chunk c simulates events
[c*events_per_job, (c+1)*events_per_job) via `--skipNEvents`, with
`--random.seed` derived from (source, chunk, replica). Output
`<pools>/<name>/<input stem>.c<NNNN>.r<R>.edm4hep.root`. Particles keep their
plain generator statuses; stage 13 applies the cocktail status offsets when it
mixes (TimeframeBuilder `generator_status_offset`).
"""

import argparse
import hashlib
import json
import math
import os
import re
import sys
import textwrap

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from simulation_pipeline.job_creator import JobCreator, write_top_master_scripts
from simulation_pipeline.datasets import load_config, load_config_for_energy

STAGE = "npsim_bgsource"

BACKGROUND_COCKTAILS_DIR_DEFAULT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "background_cocktails")

INPUT_EXTENSIONS = (".hepmc3.tree.root", ".hepmc3", ".hepmc")

# Token for one job: "<path>::chunk<c>::rep<r>" (never reaches the shell).
_TOKEN_RE = re.compile(r"^(?P<path>.*)::chunk(?P<chunk>\d+)::rep(?P<rep>\d+)$")


def job_token(path, chunk, replica):
    return f"{path}::chunk{chunk:04d}::rep{replica}"


def parse_job_token(token):
    m = _TOKEN_RE.match(str(token))
    if not m:
        raise ValueError(f"not a job token: {token}")
    return m.group("path"), int(m.group("chunk")), int(m.group("rep"))


def strip_input_extension(name):
    for ext in sorted(INPUT_EXTENSIONS, key=len, reverse=True):
        if name.endswith(ext):
            return name[: -len(ext)]
    return os.path.splitext(name)[0]


def job_basename(token):
    path, chunk, rep = parse_job_token(token)
    return f"{strip_input_extension(os.path.basename(path))}.c{chunk:04d}.r{rep}"


def job_output(token, output_dir):
    return os.path.join(output_dir, job_basename(token) + ".edm4hep.root")


def derive_seed(text):
    """Deterministic, distinct Geant4 seed per (source, chunk, replica)."""
    h = hashlib.md5(text.encode()).hexdigest()[:8]
    return (int(h, 16) % (2**31 - 1)) + 1


def to_local_path(url, strip_xrootd):
    if strip_xrootd:
        return re.sub(r"^root://[^/]+/+", "/", url)
    return url


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
        entries = json.load(fh)
    print(f"  Cocktail: {path}")
    return entries


def create_container_script_template():
    return textwrap.dedent("""\
    #!/bin/bash
    set -e

    echo ">"
    echo "= NPSIM (background source pool) =========================================="
    echo "==========================================================================="
    echo "  Source:   {source_name} (cocktail status {status}, {freq_khz} kHz)"
    echo "  Input:    {input_file}"
    echo "  Events:   skip {skip_events}, simulate {events}"
    echo "  Replica:  {replica}   Geant4 seed: {seed}"
    echo "  Output:   {output_file}"
    echo

    mkdir -p $(dirname {output_part})
    cd $(dirname {output_file})

    if [ -f "/opt/detector/epic-main/bin/thisepic.sh" ]; then
        source /opt/detector/epic-main/bin/thisepic.sh
    fi

    # Empty particle handler: background particles are tracked wherever they
    # start (the default handler drops particles created far from the IP).
    /usr/bin/time -v npsim \\
        --part.userParticleHandler="" \\
        --compactFile=$DETECTOR_PATH/epic_craterlake_{beam_config}.xml \\
        --runType batch \\
        --hepmc3.useHepMC3 true \\
        --random.seed {seed} \\
        --inputFiles {input_file} \\
        --skipNEvents {skip_events} \\
        --numberOfEvents {events} \\
        --outputFile {output_part} 2>&1

    # Written into partial/ (same filesystem, outside every *.edm4hep.root glob),
    # then renamed: a killed job never leaves a half file where TimeframeBuilder looks.
    mv {output_part} {output_file}

    echo
    echo "=========================================================================="
    echo "Job completed!"
    echo "Output: {output_file}"
    echo "=========================================================================="
    """)


def build_source(config, energy, entry, source_cfg, pool_sizes, strip_xrootd):
    """One JobCreator for one (energy, source): chunks x replicas jobs."""
    status = int(entry["status"])
    name = str(source_cfg["name"])
    events_per_job = int(source_cfg["events_per_job"])
    replicas = int(source_cfg.get("replicas", 1))
    pool_events = source_cfg.get("pool_events", "all")
    if str(pool_events) == "all":
        pool_events = pool_sizes.get(status)
        if not pool_events:
            raise SystemExit(f"{name}: pool_events 'all' needs background_pool_events.{energy}.{status}")
    pool_events = int(pool_events)
    if pool_sizes.get(status) and pool_events > int(pool_sizes[status]):
        print(f"  WARN: {name}: pool_events {pool_events} > file size {pool_sizes[status]}; clipping")
        pool_events = int(pool_sizes[status])

    n_chunks = math.ceil(pool_events / events_per_job)
    path = to_local_path(str(entry["file"]), strip_xrootd)
    tokens = [job_token(path, c, r) for r in range(replicas) for c in range(n_chunks)]

    per_frame = float(entry["freq"]) * 2000 * 1e-6
    frame_equiv = pool_events / per_frame if per_frame else float("inf")
    print(f"  {name:9s} status {status}: {pool_events:,} events in {n_chunks} chunk(s) x {replicas} replica(s)"
          f" = {len(tokens)} jobs; {per_frame:.4g} events/frame -> {frame_equiv:,.0f} frame-equivalents per replica")

    def update_params(params):
        p, chunk, rep = parse_job_token(params["input_file"])
        n = min(events_per_job, pool_events - chunk * events_per_job)
        return {
            **params,
            "input_file": p,
            "output_part": os.path.join(os.path.dirname(params["output_file"]), "partial",
                                        os.path.basename(params["output_file"])),
            "skip_events": chunk * events_per_job,
            "events": n,
            "replica": rep,
            "seed": derive_seed(f"{energy}/{name}/c{chunk}/r{rep}"),
            "source_name": name,
            "status": status,
            "freq_khz": entry["freq"],
        }

    output_dir = os.path.join(str(config[STAGE].output), name)
    runner = JobCreator(
        input_files=tokens,
        output_file_name_func=job_output,
        output_dir=output_dir,
        bind_dirs=config.bind_dirs,
        events=events_per_job,
        container=config["container"],
        beam_config=energy,
        # SynRad chunks (50k photons) peak near 3 GB; per-source `slurm_mem_per_cpu`
        # overrides the stage and campaign values.
        slurm_mem_per_cpu=str(source_cfg.get("slurm_mem_per_cpu",
                                             config[STAGE].get("slurm_mem_per_cpu",
                                                               config.get("slurm_mem_per_cpu", "2G")))),
        slurm_array_throttle=int(config.get("slurm_array_throttle", 0)),
        farm_out_dir=config.get("farm_out_dir"),
    )
    runner.basename_func = job_basename
    runner.container_script_template = create_container_script_template()
    runner.container_script_params_updater = update_params
    runner.run()
    return runner


def main():
    parser = argparse.ArgumentParser(description="Generate npsim jobs for per-source background pools.")
    parser.add_argument("-c", "--config", required=True, help="Campaign config YAML")
    parser.add_argument("--energy", default=None, help="Only this energy (default: config.energies)")
    args = parser.parse_args()

    config = load_config(args.config)
    energies = [args.energy] if args.energy else list(config.get("energies", []))
    strip_xrootd = bool(config.get("strip_xrootd_prefix", False))
    creators = []
    for energy in energies:
        cfg = load_config_for_energy(args.config, energy)
        print("\n" + "=" * 60)
        print(f"ENERGY: {energy}")
        entries = load_cocktail(cfg, energy)
        if entries is None:
            continue
        sources_cfg = cfg[STAGE].get("sources", {})
        pool_sizes = (cfg.get("background_pool_events") or {}).get(energy) or {}
        pool_sizes = {int(k): int(v) for k, v in dict(pool_sizes).items()}
        for entry in entries:
            status = int(entry["status"])
            source_cfg = sources_cfg.get(status) or sources_cfg.get(str(status))
            if source_cfg is None:
                print(f"  WARN: no npsim_bgsource.sources entry for status {status}; skipping "
                      f"{os.path.basename(str(entry['file']))}")
                continue
            creators.append(build_source(cfg, energy, entry, source_cfg, pool_sizes, strip_xrootd))
    write_top_master_scripts(creators)
    print(f"ALL SOURCES PROCESSED FOR STAGE '{STAGE}'")


if __name__ == "__main__":
    main()
