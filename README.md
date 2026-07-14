# simulation-pipeline

Shared EIC full-sim simulation pipeline, consumed as a submodule by
`meson-structure` and `ai-epic-background`. It replaces the duplicated
`full-sim-pipeline/` that lived in each project.

## The card contract

Everything from the **afterburner input** onward is driven by *dataset cards* —
small YAML files with a `files:` array. A stage never globs a directory itself;
it reads cards. Cards are produced two ways, but stages don't care which:

- **local** (meson-structure): glob a directory on the farm;
- **rucio** (official campaigns): discover datasets already on the grid.

```
01/02 gen->hepmc (dir/file, not cards)
   │
   ▼   generate_datasets afterburner -c cfg   ->  datasets/afterburner/*.yaml
   ▼   python 10_create_afterburner_jobs.py -c cfg
   ▼   generate_datasets npsim -c cfg         ->  datasets/npsim/*.yaml
   ▼   python 20_create_npsim_jobs.py -c cfg
   ▼   ... eicrecon ... csv ...
```

Official campaigns skip the local chain — their RECO is already on rucio:

```
generate_datasets csv_eicrecon --rucio -c configs/config-off-26-06.yaml
python simulation_pipeline/40_csv_convert.py csv_eicrecon -c configs/config-off-26-06.yaml
```

## Dry-render / inspect (no farm, no real files)

A stage reads a card's `files:` list and renders job scripts from it — it never
stats the inputs. So you can verify job emission with a **synthetic** card and
just read the emitted bash. Mint a card from a fake file list:

```
generate_datasets csv_eicrecon -c cfg.yaml --energy 9x130 \
    --files /any/path/msf_9x130_0001.edm4eic.root /any/path/msf_9x130_0002.edm4eic.root
python simulation_pipeline/40_csv_convert.py csv_eicrecon -c cfg.yaml
# then inspect:
cat <output>/csv-reco/9x130/jobs/*.container.sh     # per-file container script
cat <output>/csv-reco/9x130/jobs/array.slurm.sh     # SLURM job-array wrapper
bash -n <output>/csv-reco/9x130/jobs/*.sh           # syntax check
```

Or hand-write a card YAML directly (see the schema in `datasets.py`) and drop it
in `<datasets_dir>/<stage>/` — the `files:` entries can be `root://` PFNs or any
paths; nothing needs to exist.

## Layout

Everything lives flat in one package directory — open a stage script and the
engine/card code is right next to it:

```
simulation_pipeline/
  NN_*.py                     stage job generators (run as scripts)
  40_csv_convert.py           ONE csv script for all csv stages (see below)
  generate_datasets.py        universal card producer (local glob / --rucio / --files)
  datasets.py                 card schema, config loader, run_card_pipeline
  job_creator.py              the job-generation engine (SLURM arrays + local)
  rucio.py                    rucio discovery (official campaigns)
csv_convert/                  ROOT converter macros, edm4hep_* / edm4eic_*
background_cocktails/         per-energy background cocktail JSONs (stages 11/22)
configs/                      one self-contained YAML per campaign
scripts/                      collect_job_stats.py, eg_*.sh helpers
```

## Farm etiquette (baked in, per JLab admin requirements)

- **SLURM stdout/stderr never goes to /work** — it overloads the work file
  server. Logs go under `farm_out_dir` (default `/farm_out/$USER`), mirroring
  the output path: `/farm_out/romanov/work/eic3/.../csv-reco/9x130/`. Job
  scripts and data outputs stay on /work as before.
- **Memory requests default to 2G/CPU** — the farm is provisioned for 2GB per
  CPU; requesting more makes SLURM bill extra CPUs (mem=5G → 2 CPUs) and idles
  the farm. Override per campaign with `slurm_mem_per_cpu` if a stage truly
  needs more.

## Configs — one campaign, one file

Each campaign config is **fully self-contained**: open it and you see the whole
picture — where inputs were, where every stage wrote, which macros ran. No
inheritance, no base files. Each stage is a top-level block:

```yaml
afterburner:
  input:   "${base_dir}/eg-hepmc/${energy}-priority"
  pattern: "*.hepmc"
  output:  "${base_dir}/afterburner/${energy}-priority"
npsim:
  input:   "${afterburner.output}"          # stages chain by interpolation
  pattern: "*.hepmc3.tree.root"
  output:  "${base_dir}/dd4hep/${energy}"
```

## Stages

| script | stage key (config block) | reads |
|---|---|---|
| `01/02_root_hepmc_*_convert.py` | — (pre-card) | generator ROOT |
| `10_create_afterburner_jobs.py` | `afterburner` | `*.hepmc` |
| `11_create_background_jobs.py` | `bg_merger` | `*.hepmc3.tree.root` |
| `20_create_npsim_jobs.py` | `npsim` | `*.hepmc3.tree.root` |
| `21_create_npsim_saveall_jobs.py` | `npsim_saveall` | `*.hepmc3.tree.root` |
| `22_create_npsim_background_jobs.py` | `npsim_background` | `*.bg.hepmc3.tree.root` |
| `30_create_eicrecon_jobs.py` | `eicrecon` | `*.edm4hep.root` |
| `40_csv_convert.py <stage>` | any `csv_*` block | `*.edm4hep.root` / `*.edm4eic.root` |
| `50/51_create_*_analysis_jobs.py` | — (directory-based) | csv |

There is **one** CSV script. Which converters run is entirely the config's
`macros:` list — `40_csv_convert.py csv_dd4hep` runs the `edm4hep_*` macros the
config names, `40_csv_convert.py csv_eicrecon` the `edm4eic_*` ones. Output CSVs
are named `<input-stem>.<role>.csv` (e.g. `msf_9x130_0001.reco_particles.csv`).

> Wiring `meson-structure` / `ai-epic-background` to consume this submodule is a
> separate, later step; this repo is the shared pipeline itself.
