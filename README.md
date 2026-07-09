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
   ▼   generate_datasets afterburner -c cfg      ->  datasets/afterburner/*.yaml
   ▼   python .../stages/10_create_afterburner_jobs.py -c cfg
   ▼   generate_datasets npsim -c cfg            ->  datasets/npsim/*.yaml
   ▼   python .../stages/20_create_npsim_jobs.py -c cfg
   ▼   ... eicrecon ... csv ...
```

Official campaigns skip the local chain — their RECO is already on rucio:

```
generate_datasets csv_eicrecon --rucio -c config-off-26-06.yaml
python .../stages/41_create_csv_eicrecon_jobs.py -c config-off-26-06.yaml
```

## Dry-render / inspect (no farm, no real files)

A stage reads a card's `files:` list and renders job scripts from it — it never
stats the inputs. So you can verify job emission with a **synthetic** card and
just read the emitted bash. Mint a card from a fake file list:

```
generate_datasets csv_eicrecon -c cfg.yaml --energy 9x130 \
    --files /any/path/msf_9x130_0001.edm4eic.root /any/path/msf_9x130_0002.edm4eic.root
python .../stages/41_create_csv_eicrecon_jobs.py -c cfg.yaml
# then inspect:
cat <output>/csv-reco/9x130/jobs/*.container.sh     # per-file container script
cat <output>/csv-reco/9x130/jobs/array.slurm.sh     # SLURM job-array wrapper
bash -n <output>/csv-reco/9x130/jobs/*.sh           # syntax check
```

Or hand-write a card YAML directly (see the schema in `datasets.py`) and drop it
in `<datasets_dir>/<stage>/` — the `files:` entries can be `root://` PFNs or any
paths; nothing needs to exist.

## Layout

```
simulation_pipeline/          importable package (`pip install -e .`)
  job_creator.py              the job-generation engine (SLURM arrays + local)
  generate_datasets.py        universal card producer (local glob + --rucio)
  datasets.py                 card schema, config loader, run_card_pipeline
  rucio.py                    rucio discovery (official campaigns)
  csv_stage.py                shared CSV-stage builder (config-driven macros)
  stages/                     NN_create_*_jobs.py  (run as scripts)
csv_convert/                  ROOT converter macros, edm4hep_* / edm4eic_*
configs/                      config-base + config-msf/off + per-campaign
scripts/                      collect_job_stats.py, eg_*.sh helpers
```

## Configs

Campaign configs layer via an `extends:` list (base -> family -> campaign):

```yaml
extends: [config-base.yaml, config-msf.yaml]
base_dir: "/work/eic3/users/romanov/meson-structure-2026-07"
energies: ["5x41", "9x100", "9x130", "9x275"]
```

Each stage is a flat top-level block (`afterburner`, `npsim`, `eicrecon`,
`csv_eicrecon`, ...) with `input` / `pattern` / `output`; stages chain via
`${<stage>.output}`. The CSV stages also carry a config-driven `macros:` list
and a `stem:` (`basename` or `file_index`).

## Stages

| # | script | stage key | reads |
|---|---|---|---|
| 01/02 | gen->hepmc converters | — (pre-card) | generator ROOT |
| 10 | afterburner | `afterburner` | `*.hepmc` |
| 11 | background merge | `bg_merger` | `*.hepmc3.tree.root` |
| 20 | npsim | `npsim` | `*.hepmc3.tree.root` |
| 21 | npsim save-all | `npsim_saveall` | `*.hepmc3.tree.root` |
| 22 | npsim (bg-merged) | `npsim_background` | `*.bg.hepmc3.tree.root` |
| 30 | eicrecon | `eicrecon` | `*.edm4hep.root` |
| 40 | csv (edm4hep) | `csv_dd4hep` | `*.edm4hep.root` |
| 41 | csv (edm4eic) | `csv_eicrecon` | `*.edm4eic.root` |
| 50/51 | analysis | — (directory-based) | csv |

> Wiring `meson-structure` / `ai-epic-background` to consume this submodule is a
> separate, later step; this repo is the shared pipeline itself.
