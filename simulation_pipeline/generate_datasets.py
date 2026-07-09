"""generate_datasets — the universal dataset-card producer.

    generate_datasets <stage> -c config.yaml [--energy 9x130] [--rucio]
                       [--max-files N] [--clean]

For a stage, read that stage's config block (``<stage>.input`` +
``<stage>.pattern``), glob the input directory, and write one card per
``(stage, energy)`` into ``<datasets_dir>/<stage>/``. The stage generator then
consumes those cards.

``--rucio`` switches to the official-campaign producer: instead of globbing a
local directory it discovers datasets from rucio (see
:mod:`simulation_pipeline.rucio`) and writes one card per dataset DID. Same card
schema either way, so the stages never know which producer ran.

``--files a.root b.root ...`` writes a single card straight from an explicit file
list (no globbing, inputs need not exist on disk). Handy for dry-render testing
of a stage generator: hand a stage a synthetic ``files:`` list and inspect the
emitted job scripts. Same card schema, so the stage code path is identical.
"""

import argparse
import os
from glob import glob

from .datasets import cards_dir, load_config, load_config_for_energy, write_card


def _make_local_cards(stage, config_path, out_dir, energies, max_files):
    """Glob each energy's input dir and write one card per energy."""
    written = []
    for energy in energies:
        cfg = load_config_for_energy(config_path, energy)
        if stage not in cfg:
            raise SystemExit(f"Config has no '{stage}' block.")
        scfg = cfg[stage]
        input_dir = os.path.abspath(str(scfg.input))
        pattern = str(scfg.pattern)
        output = str(scfg.output) if "output" in scfg else None

        files = sorted(glob(os.path.join(input_dir, pattern)))
        if max_files:
            files = files[:max_files]
        if not files:
            print(f"  WARN: no '{pattern}' in {input_dir} -- skipping {energy}")
            continue

        slug = f"{stage}_{energy}"
        path = os.path.join(out_dir, f"{slug}.yaml")
        write_card(
            path,
            dataset=input_dir, slug=slug, stage=stage, energy=energy,
            pattern=pattern, input_dir=input_dir, output=output, files=files,
            metadata={"source": "local", "beam_energy": energy},
        )
        written.append(path)
        print(f"  {slug}  ({len(files)} files) -> {path}")
    return written


def _make_card_from_files(stage, config_path, out_dir, energy, slug, files):
    """Write one card from an explicit file list (no globbing; inputs may be fake)."""
    if energy:
        cfg = load_config_for_energy(config_path, energy)
    else:
        cfg = load_config(config_path)
    scfg = cfg.get(stage, {})

    # Resolve the stage's output template if it is present and resolvable
    # (a `${energy}` template needs --energy; otherwise leave output unset).
    output = None
    if "output" in scfg:
        try:
            output = str(scfg.output)
        except Exception:
            output = None
    pattern = str(scfg.pattern) if "pattern" in scfg else None

    slug = slug or (f"{stage}_{energy}" if energy else f"{stage}_manual")
    path = os.path.join(out_dir, f"{slug}.yaml")
    write_card(
        path,
        dataset="(manual file list)", slug=slug, stage=stage, energy=energy,
        pattern=pattern, output=output, files=list(files),
        metadata={"source": "manual", **({"beam_energy": energy} if energy else {})},
    )
    print(f"  {slug}  ({len(files)} files) -> {path}")
    return [path]


def _make_rucio_cards(stage, config, config_path, out_dir, max_files):
    """Discover datasets from rucio and write one card per dataset DID."""
    from .rucio import discover_datasets

    # For rucio cards the per-energy input dir is meaningless; the stage output
    # base gets the dataset slug appended so datasets never collide.
    scfg = config.get(stage, {})
    output_base = str(scfg.get("output")) if "output" in scfg else None

    written = []
    for ds in discover_datasets(config, max_files=max_files):
        slug = ds["slug"]
        output = os.path.join(output_base, slug) if output_base else None
        path = os.path.join(out_dir, f"{slug}.yaml")
        write_card(
            path,
            dataset=ds["did"], slug=slug, stage=stage, energy=ds.get("energy"),
            output=output, files=ds["files"],
            metadata={"source": "rucio",
                      "rucio_metadata": ds.get("rucio_metadata", {})},
        )
        written.append(path)
        print(f"  {slug}  ({len(ds['files'])} files) -> {path}")
    return written


def main():
    parser = argparse.ArgumentParser(
        description="Produce dataset cards for a pipeline stage.",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("stage", help="Stage name, e.g. afterburner / npsim / eicrecon / csv_eicrecon")
    parser.add_argument("-c", "--config", required=True, help="Campaign config YAML")
    parser.add_argument("--energy", default=None, help="Only this energy (default: config.energies)")
    parser.add_argument("--rucio", action="store_true",
                        help="Produce cards from rucio (official campaigns) instead of a local glob")
    parser.add_argument("--files", nargs="+", default=None,
                        help="Explicit file list -> one card (no globbing; inputs may be fake)")
    parser.add_argument("--slug", default=None,
                        help="Card slug/filename (only with --files; default <stage>_<energy>)")
    parser.add_argument("--max-files", type=int, default=None,
                        help="Cap files per card (override config.max_files_per_dataset; 0 = all)")
    parser.add_argument("--clean", action="store_true",
                        help="Remove existing *.yaml in the stage cards dir first")
    args = parser.parse_args()

    config = load_config(args.config)
    out_dir = cards_dir(config, args.stage)
    os.makedirs(out_dir, exist_ok=True)
    if args.clean:
        for f in glob(os.path.join(out_dir, "*.yaml")):
            os.remove(f)

    max_files = args.max_files if args.max_files is not None \
        else int(config.get("max_files_per_dataset", 0))

    source = "manual" if args.files else ("rucio" if args.rucio else "local")
    print("=" * 70)
    print(f"GENERATE DATASETS  stage={args.stage}  source={source}")
    print(f"  cards dir: {out_dir}")
    print("=" * 70)

    if args.files:
        written = _make_card_from_files(
            args.stage, args.config, out_dir, args.energy, args.slug, args.files)
    elif args.rucio:
        written = _make_rucio_cards(args.stage, config, args.config, out_dir, max_files)
    else:
        energies = [args.energy] if args.energy else list(config.get("energies", []))
        if not energies:
            raise SystemExit("No energies: set config.energies or pass --energy.")
        written = _make_local_cards(args.stage, args.config, out_dir, energies, max_files)

    print("\n" + "=" * 70)
    print(f"Wrote {len(written)} card(s) to {out_dir}")
    print(f"Next:  python simulation_pipeline/{_stage_script(args.stage)} -c {args.config}")
    print("=" * 70)


def _stage_script(stage):
    """Best-effort hint of which stage script consumes these cards."""
    if stage.startswith("csv"):
        return f"40_csv_convert.py {stage}"
    return {
        "afterburner": "10_create_afterburner_jobs.py",
        "bg_merger": "11_create_background_jobs.py",
        "npsim": "20_create_npsim_jobs.py",
        "npsim_saveall": "21_create_npsim_saveall_jobs.py",
        "npsim_background": "22_create_npsim_background_jobs.py",
        "eicrecon": "30_create_eicrecon_jobs.py",
    }.get(stage, f"<stage {stage}>.py")


if __name__ == "__main__":
    main()
