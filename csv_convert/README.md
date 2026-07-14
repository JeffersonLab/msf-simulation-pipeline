# csv_convert — ROOT converter macros

Each macro turns one podio ROOT file into a CSV. Run interpreted via ROOT:

```bash
root -x -l -b -q 'edm4eic_reco_particles.cxx("input.edm4eic.root","out.reco_particles.csv")'
```

## Naming convention

Every converter is named by the data model it reads:

- `edm4hep_*.cxx` — reads **edm4hep** (dd4hep / npsim output).
- `edm4eic_*.cxx` — reads **edm4eic** (eicrecon output).

The ROOT entry-point function inside each file has the **same name as the file**
(ROOT requires this for `root file.cxx(...)`), and each converter writes
`<input-stem>.<role>.csv` where `<role>` is the name minus the
`edm4eic_`/`edm4hep_` prefix (e.g. `edm4eic_reco_particles` → `reco_particles`).

A config's `<csv-stage>.macros` list selects which converters run; all CSV job
generation goes through the single `40_csv_convert.py <stage>` script.

## Converters

| Macro | Reads | Output |
|---|---|---|
| `edm4eic_reco_particles.cxx` | edm4eic | reco-particle dump (one row per particle) |
| `edm4eic_mc_particles.cxx` | edm4eic | MCParticle dump |
| `edm4eic_trk_hits.cxx` | edm4eic | tracker/calo hit dump |
| `edm4eic_calo_clusters.cxx` | edm4eic | calo cluster dump (one row per cluster↔MCParticle association, with `prt_origin`) |
| `edm4eic_mc_dis.cxx` | edm4eic | DIS invariants (one row per event, from `dis_*` params) |
| `edm4eic_reco_dis.cxx` | edm4eic | reconstructed DIS kinematics |
| `edm4eic_mcpart_lambda.cxx` | edm4eic | Lambda MC-truth |
| `edm4eic_reco_ff_lambda.cxx` | edm4eic | Lambda far-forward reco |
| `edm4hep_acceptance_ppim.cxx` | edm4hep | p pi- acceptance (writes 3 CSVs: main + pimin_hits + prot_hits) |
| `edm4hep_acceptance_npi0.cxx` | edm4hep | n pi0 acceptance |
| `edm4hep_combinatorics_ppim.cxx` | edm4hep | p pi- combinatorics |

Note: `edm4hep_acceptance_ppim` derives two extra CSV paths internally
(`*_pimin_hits.csv`, `*_prot_hits.csv`); the job script zips only the main
output, the extra CSVs stay unzipped next to it.
