"""Dataset cards — the universal input contract for every stage.

A *card* is a small YAML file describing one group of input files for one stage:

    dataset:  <identity: an absolute dir, an xrootd URL, or a rucio DID>
    slug:     afterburner_9x130          # unique name for this group
    stage:    afterburner
    energy:   9x130                       # None for rucio cards without one
    pattern:  "*.hepmc"                   # how the files were discovered (local)
    input_dir: /work/.../eg-hepmc/9x130-priority
    output:   /work/.../afterburner/9x130-priority
    n_files:  998
    files:
      - /work/.../file_0001.hepmc
      - ...
    metadata: {source: local, beam_energy: 9x130, ...}

Cards are produced by ``generate_datasets <stage> -c config`` (local glob),
``generate_datasets <stage> --rucio`` (official campaigns) or
``generate_datasets <stage> --files ...`` (explicit list, e.g. for dry-render
testing). Every stage generator reads cards and never globs a directory itself,
so local farm files and rucio ``root://`` PFNs look identical downstream.
"""

import argparse
import os
from glob import glob

import yaml
from omegaconf import OmegaConf

from .job_creator import write_top_master_scripts


# ---------------------------------------------------------------------------
# Config loading — one campaign = one self-contained YAML file
# ---------------------------------------------------------------------------

def load_config(config_path):
    """Load a campaign config. ``${...}`` interpolations resolve lazily."""
    return OmegaConf.load(config_path)


def load_config_for_energy(config_path, energy):
    """Load config with ``${energy}`` bound, so per-energy paths interpolate."""
    return OmegaConf.merge(load_config(config_path), {"energy": energy})


# ---------------------------------------------------------------------------
# Card I/O
# ---------------------------------------------------------------------------

# Field order for readable cards.
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
    # Drop None values (keep the card tidy), preserving order.
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
    return os.path.abspath(os.path.join(str(config.datasets_dir), stage))


def load_cards(config, stage, override=None):
    """Load every ``*.yaml`` card for *stage*, sorted by filename."""
    d = cards_dir(config, stage, override)
    paths = sorted(glob(os.path.join(d, "*.yaml")))
    if not paths:
        raise SystemExit(
            f"No cards for stage '{stage}' in {d}.\n"
            f"Run:  generate_datasets {stage} -c <config>")
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
    config first so per-energy path templates resolve. Writes the top-level
    aggregated submit/run scripts across all cards at the end.
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
