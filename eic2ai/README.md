# eic2ai — single-pass podio → CSV converter (JANA2)

Status: planned, no code yet. The plan lives in the parent repository:
`eic2ai-plan.md` (design, phases, parity gate); the rationale in
`eic2ai-intention.md`.

## What it does

`eic2ai` reads an eicrecon file (`.edm4eic.root`) and, when given, its paired
simulation file (`.edm4hep.root`) once, and hands every event to a set of
writers. Each writer produces one CSV role with the same bytes the macro in
`../csv_convert/` produces today, streamed into `<stem>.<role>.csv.zip`.

```
eic2ai --reco in.edm4eic.root --sim in.edm4hep.root --out-dir DIR \
       --writers trk_hits,cal_hits,calo_clusters
```

Every option is a JANA2 parameter underneath (`-Peic2ai:reco=`, `-Peic2ai:writers=`, ...).

## Design in one paragraph

One `JEventSource` (`PodioPairSource`) owns both podio readers, verifies the
pairing (entry counts, `EventHeader.eventNumber` per entry) and emits one
`JEvent` per entry carrying both frames (`EventInputs`). Writers are
`JEventProcessor`s, one header per CSV role under `src/writers/`, registered
through one table; shared physics logic (origin classification, system-id
table) is defined once under `src/truth/`. Each writer declares which inputs it
needs, so reco-only, sim-only and paired runs use the same binary.

## Planned layout

```
CMakeLists.txt
src/main.cpp                  CLI → parameters, writer registry, run
src/PodioPairSource.hpp/.cpp  two readers, pairing guards
src/EventInputs.hpp           per-event frames + entry index
src/truth/                    origin.hpp, system_ids.hpp, podio_helpers.hpp
src/output/CsvSink.hpp/.cpp   plain / zip (minizip, ZIP64) / gz, tmp + rename
src/writers/                  CsvWriter.hpp base, one <Role>Writer.hpp per role, registry
third_party/minizip/          zlib contrib zip writer (zlib license)
tests/parity.py               byte-for-byte comparison against the csv_convert macros
```

## Build

Inside a container that ships JANA2 with podio support (eic_xl, eic-claude):

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Dependencies: JANA2 (built with podio), podio, EDM4HEP, EDM4EIC, ROOT, fmt, zlib.

## Migration rule

A macro in `../csv_convert/` retires only after its writer produces
byte-identical CSVs on the reference files (`tests/parity.py`). Until then both
converters stay and the pipeline config selects one.
