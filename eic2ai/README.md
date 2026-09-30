# eic2ai — single-pass podio → CSV converter

`eic2ai` reads an eicrecon file (`.edm4eic.root`) and, when given, its paired simulation file
(`.edm4hep.root`) once, and hands every event to a set of writers. Each writer produces one CSV
role with the rows the macro in `../csv_convert/` produces today, streamed into
`<stem>.<role>.csv.zip`. One process, one thread, one loop over entries. Dependencies: podio,
EDM4HEP, EDM4EIC, zlib, fmt, minizip-ng (downloaded at configure time). No framework.

```
eic2ai --reco in.edm4eic.root --sim in.edm4hep.root --out-dir DIR \
       --writers trk_hits,cal_hits,calo_clusters [--compress zip|none|gz] [-n N]
eic2ai --list-writers      # every role, the inputs it needs, the collections it reads
eic2ai --help
```

`--writers auto` runs every writer whose inputs are given. `--selective` reads only the collections the
enabled writers list (five times faster bare reads; off by default, see the plan). `--zip-level N` sets the
deflate level. Exit code 0: every writer succeeded;
1: a writer failed and its output was discarded, the others are complete; 2: the run did not
start or stopped (bad options, unreadable input, pairing guard).

Design, readability rules, phases and the parity gate: `eic2ai-plan.md` in the parent
repository; the rationale in `eic2ai-intention.md`.

## Reading order

Open these five files in sequence to understand the whole program:

1. `src/main.cpp` — the story: options, writers, needs check, inputs, event loop, summary.
2. `src/PairReader.hpp` — one or two podio readers, the two pairing guards, what `read()` returns.
3. `src/EventInputs.hpp` — what every writer receives per event.
4. `src/Writer.hpp` — what a writer is: role, needs, collections, header, `write_event`; the failure policy.
5. `src/writer_TrkHitsWriter.hpp` — one complete writer; every other writer has the same layout.

Then `src/origin.hpp` and `src/system_ids.hpp` for the shared physics logic, `src/CsvSink.hpp`
for how bytes reach the zip, and `src/WritersRegistry.cpp` for the role table.

## Layout

```
CMakeLists.txt                 dependencies, minizip-ng FetchContent, one executable
src/main.cpp                   options → writers → reader → loop → summary
src/Options.hpp/.cpp           command line → Options struct; --help, input_stem()
src/PairReader.hpp/.cpp        readers, entry-count and eventNumber guards
src/EventInputs.hpp            reco and sim frames of one entry, entry_index, event_number
src/origin.hpp                 classify_gen_status, get_origin_status (prt_origin)
src/system_ids.hpp             system-id table, "Unknown" for ids outside it
src/podio_helpers.hpp          get_optional_collection, event_number_of, SkipNotes
src/lambda_decay.hpp           Λ decay classification shared by the Λ analysis roles
src/mc_particle_block.hpp      the 16-column MC particle block of the Λ analysis roles
src/legacy_format.hpp          stream_text: ostream-style number formatting some macros used
src/CsvSink.hpp/.cpp           plain / zip (minizip-ng, ZIP64) / gz backends, tmp + rename
src/Writer.hpp/.cpp            base class: output file, eager header, failure marking, summary line
src/writer_<Role>Writer.hpp    one header per role
src/WritersRegistry.hpp/.cpp   role → writer table; --writers auto; --list-writers
tests/parity.py                byte comparison against the csv_convert macro outputs (header through the rename table)
tests/check_columns.py         the column vocabulary check
tests/rename_consumer_columns.py  rewrites macro-era column names in a consumer file
column_renames.py              macro-era → unified column names, per output
tests/make_references.sh       runs a macro executable on the reference inputs
tests/build_macros.sh          builds the macro executables
tests/build_in_image.sh        builds eic2ai inside a local EIC docker image
```

## Build

Inside a container that ships podio, EDM4HEP and EDM4EIC (eic_xl, eic-full, eic-claude):

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The configure step downloads minizip-ng 4.2.2. Offline, pass
`-DFETCHCONTENT_SOURCE_DIR_MINIZIP=<unpacked source tree>` or `-DEIC2AI_FETCH_MINIZIP=OFF`
(system minizip-ng).

## Column vocabulary

Every column is `<prefix>_<quantity>` from one vocabulary (`column_renames.py` documents it; `eic2ai
--list-columns` prints every header; `tests/check_columns.py` rejects a header outside it). The
macros wrote two vocabularies; `column_renames.py` maps their names to the unified ones, so a CSV,
parquet or feather made before 2026-09-30 loads into the new names with
`column_renames.rename_dataframe(df, "trk_hits")`, and `tests/rename_consumer_columns.py`
rewrites a script, notebook or document that still uses the old names. The parity gate compares
rows byte for byte and the header through the same table.

## Adding a writer

Copy `src/writer_TrkHitsWriter.hpp`, keep its layout (header comment, record struct with one
comment per column, `write_event`), add one line to the table in `src/WritersRegistry.cpp`.
A writer with several output files declares them in `extra_outputs()` and writes them with
`write_extra_row()` (see `writer_CombCandidatesWriter.hpp`). List every collection the writer
reads, relation targets included, in `collections()`: `--selective` reads nothing else.
Column names follow the existing vocabulary (`prt_*`, `trk_hit_*`, `cal_hit_*`, `clu_*`): one
name per quantity across roles.

## Roles

`eic2ai --list-writers` prints the table. Reco roles (need `--reco`): trk_hits, calo_clusters, mc_dis,
mc_particles, mcpart_lambda, reco_particles, reco_dis, reco_ff_lambda, lambda_acceptance, comb_candidates
(+ `_events`). Paired (need both): cal_hits. Sim roles (need `--sim`): acceptance_ppim (+ `_prot_hits`,
`_pimin_hits`), acceptance_npi0, combinatorics_ppim. Any input: event_index (one row per event).

## Output contract

- `<out_dir>/<stem>.<role>.csv.zip`: one deflate ZIP64 entry named `<stem>.<role>.csv`
  (pandas `read_csv(..., compression='zip')` and `zipfile.ZipFile` read it).
- `<stem>` is the input file name without `.eicrecon.edm4eic.root`, `.edm4eic.root`,
  `.edm4hep.root` or `.root` (the reco file's name, or the sim file's in sim-only runs).
- The header line is always present; a role with no rows yields a header-only file.
- `evt` is the 0-based entry counter of the run, the same in every role of one run.
- Outputs are written as `.tmp` and renamed when complete; a crashed run leaves no final file.

## Migration rule

A macro in `../csv_convert/` retires only after its writer passes `tests/parity.py` on the
reference files. Until then both converters stay and the pipeline config selects one.
