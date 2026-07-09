# csv_convert — ROOT converter macros

Each macro turns one podio ROOT file into a CSV. Run interpreted via ROOT:

```bash
root -x -l -b -q 'edm4eic_reco_particles.cxx("input.edm4eic.root","out.reco_particles.csv")'
```

## Naming convention

Every converter is named by the data model it reads:

- `edm4hep_*.cxx` — reads **edm4hep** (dd4hep / npsim output). Run by stage 40
  (`csv_dd4hep`).
- `edm4eic_*.cxx` — reads **edm4eic** (eicrecon output). Run by stage 41
  (`csv_eicrecon`).

The ROOT entry-point function inside each file has the **same name as the file**
(ROOT requires this for `root file.cxx(...)`), and each converter writes
`<stem>.<role>.csv` where `<role>` is the name minus the `edm4eic_`/`edm4hep_`
prefix (e.g. `edm4eic_reco_particles` → `reco_particles`).

A config's `<stage>.macros` list selects which converters run, so meson-structure
picks the Lambda + DIS set and official campaigns pick the generic set.

## edm4eic converters (in this repo)

| Macro | Origin | Notes |
|---|---|---|
| `edm4eic_reco_particles.cxx` | merged ms + ai-bg (ai-bg body) | reco-particle dump; robust across eic_xl versions |
| `edm4eic_mc_particles.cxx` | ai-bg | MCParticle dump |
| `edm4eic_trk_hits.cxx` | ai-bg | tracker/calo hit dump |
| `edm4eic_mc_dis.cxx` | ms | DIS invariants (per event, `dis_*` params) |
| `edm4eic_reco_dis.cxx` | ms | reco DIS kinematics |
| `edm4eic_mcpart_lambda.cxx` | ms | Lambda MC-truth |
| `edm4eic_reco_ff_lambda.cxx` | ms | Lambda far-forward reco |

## edm4hep converters

The edm4hep acceptance / combinatorics converters are meson-structure
analysis-specific and are **not** vendored here; a project that wants them keeps
them in its own `csv_convert/` and points `csv_dd4hep.macros` + `csv_convert_dir`
at that directory.
