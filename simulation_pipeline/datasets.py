"""Dataset cards — the universal input contract for every stage.

A *card* is a small YAML file describing one group of input files for one stage:

    dataset:  <identity: an absolute dir, an xrootd URL, or a rucio DID>
    slug:     afterburner_9x130          # unique name for this group
    stage:    afterburner
    energy:   9x130                       # None for rucio cards without an energy
    pattern:  "*.hepmc"                   # how the files were discovered (local)
    input_dir: /work/.../eg-hepmc/9x130-priority
    output:   /work/.../afterburner/9x130-priority
    n_files:  998
    files:
      - /work/.../file_0001.hepmc
      - ...
    metadata: {source: local, beam_energy: 9x130, ...}

Cards are produced by :mod:`simulation_pipeline.generate_datasets` (local glob) or
``generate_datasets --rucio`` (official campaigns). Every stage generator reads cards
and never globs a directory itself — so the two worlds (local farm files vs
rucio ``root://`` PFNs) look identical downstream.

This module also holds the config loader (with a small ``extends:`` overlay for
the ``config-base`` / ``config-msf`` / ``config-off`` layering) and
``run_card_pipeline`` — the standard CLI entry point stage scripts call.
"""

import argparse
import os
from glob import glob

import yaml
from omegaconf import OmegaConf

from .job_creator import write_top_master_scripts


# ---------------------------------------------------------------------------
# Config loading  (base -> family -> campaign overlay via `extends:`)
# ---------------------------------------------------------------------------

def load_config(config_path):
    """Load a config YAML, resolving an optional ``extends:`` list.

    ``extends`` is a list of config files (relative to *this* file's directory)
    merged in order before the file's own keys, so a campaign config can layer
    onto ``config-base`` + ``config-msf`` / ``config-off``:

        extends: [config-base.yaml, config-msf.yaml]
        base_dir: /work/.../meson-structure-2026-07

    Later files win (OmegaConf.merge semantics). Interpolations (``${base_dir}``,
    ``${afterburner.output}``, ...) are preserved and resolved lazily on access.
    """
    config_path = os.path.abspath(config_path)
    here = os.path.dirname(config_path)
    cfg = OmegaConf.load(config_path)

    parents = cfg.pop("extends", None)
    if not parents:
        return cfg

    merged = OmegaConf.create({})
    for parent in parents:
        merged = OmegaConf.merge(merged, load_config(os.path.join(here, str(parent))))
    return OmegaConf.merge(merged, cfg)


def load_config_for_energy(config_path, energy):
    """Load config with ``${energy}`` bound, so per-energy paths interpolate."""
    return OmegaConf.merge(load_config(config_path), {"energy": energy})


# ---------------------------------------------------------------------------
# Card I/O
# ---------------------------------------------------------------------------

# Field order for readable cards (matches ms 60 / ai-bg 42 card layout).
_CARD_ORDER = ["dataset", "xrootd", "slug", "stage", "energy",
               "pattern", "input_dir", "output", "metadata", "n_files", "files"]


def write_card(path, *, dataset, slug, stage, files,
               energy=None, pattern=None, input_dir=None, output=None,
               xrootd=None, metadata=None):
    """Write one dataset card to *path* (creating parent dirs)."""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    card = {
        "dataset": dataset,
        "xrootd": xrootd,
        "slug": slug,
        "stage": stage,
        "energy": energy,
        "pattern": pattern,
        "input_dir": input_dir,
        "output": output,
        "metadata": metadata or {},
        "n_files": len(files),
        "files": list(files),
    }
    # Drop keys that are None (keep the card tidy), preserving order.
    card = {k: card[k] for k in _CARD_ORDER if card.get(k) is not None}
    with open(path, "w") as f:
        yaml.safe_dump(card, f, sort_keys=False, default_flow_style=False)
    return path


def load_card(path):
    """Load a single card as a plain dict."""
    with open(path) as f:
        return yaml.safe_load(f)


def cards_dir(config, stage, override=None):
    """Directory that holds a stage's cards: ``<datasets_dir>/<stage>``."""
    if override:
        return os.path.abspath(str(override))
    base = str(config.get("datasets_dir"))
    return os.path.abspath(os.path.join(base, stage))


def load_cards(config, stage, override=None):
    """Load every ``*.yaml`` card for *stage*, sorted by filename."""
    d = cards_dir(config, stage, override)
    paths = sorted(glob(os.path.join(d, "*.yaml")))
    if not paths:
        raise SystemExit(
            f"No cards for stage '{stage}' in {d}.\n"
            f"Run:  generate_datasets {stage} -c <config>   (or `--rucio` for official campaigns)")
    cards = [load_card(p) for p in paths]
    print(f"Loaded {len(cards)} card(s) for stage '{stage}' from {d}")
    return cards


# ---------------------------------------------------------------------------
# Stage entry point
# ---------------------------------------------------------------------------

def run_card_pipeline(stage, build_creator_fn, description="Generate jobs."):
    """Standard CLI for a stage: load cards, build one JobCreator per card.

    ``build_creator_fn(config_for_energy, card, config_path) -> JobCreator|None``
    is called once per card; the card's ``energy`` (if any) is bound into the
    config first so per-energy path templates resolve. Returns nothing; writes
    the top-level aggregated submit/run scripts across all cards.
    """
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("-c", "--config", required=True, help="Path to config YAML file")
    parser.add_argument("--datasets", default=None,
                        help="Override the cards directory (default: <datasets_dir>/<stage>)")
    args = parser.parse_args()

    config = load_config(args.config)
    cards = load_cards(config, stage, override=args.datasets)

    creators = []
    for card in cards:
        energy = card.get("energy")
        cfg = load_config_for_energy(args.config, energy) if energy else config
        print("\n" + "=" * 60)
        print(f"CARD: {card['slug']}  ({card.get('n_files', len(card.get('files', [])))} files)")
        creators.append(build_creator_fn(cfg, card, args.config))

    write_top_master_scripts([c for c in creators if c is not None])
    print(f"ALL CARDS PROCESSED FOR STAGE '{stage}'")


# ---------------------------------------------------------------------------
# CSV helpers (shared by the 40 / 41 CSV stages)
# ---------------------------------------------------------------------------

def macro_role(macro):
    """Role name of a converter, i.e. the macro minus its data-model prefix.

    ``edm4eic_reco_particles`` -> ``reco_particles`` (used for the CSV suffix and
    the log label). Names without a known prefix are returned unchanged.
    """
    for prefix in ("edm4eic_", "edm4hep_"):
        if macro.startswith(prefix):
            return macro[len(prefix):]
    return macro


def csv_stem(input_file, mode="basename"):
    """Derive the CSV filename stem from an input ROOT file.

    ``basename``   -> the filename with its ``.edm4*.root`` suffix stripped
                      (meson-structure convention).
    ``file_index`` -> the trailing numeric index only, e.g. ``...hiDiv_1.0000``
                      -> ``0000`` (ai-bg convention; unique within a dataset).
    """
    name = os.path.basename(input_file)
    for suffix in (".eicrecon.edm4eic.root", ".edm4eic.root", ".edm4hep.root", ".root"):
        if name.endswith(suffix):
            name = name[: -len(suffix)]
            break
    if mode == "file_index":
        import re
        m = re.search(r"(\d+)$", name)
        return m.group(1) if m else name
    return name
