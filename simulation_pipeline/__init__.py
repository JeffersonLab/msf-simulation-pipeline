"""Shared EIC full-sim simulation pipeline.

Every stage from the afterburner input onward reads *dataset cards* (small YAML
files with a ``files:`` array) produced by ``generate_datasets``. The numbered
``NN_*.py`` scripts in this directory are the stage job generators; run them as
scripts, e.g. ``python 40_csv_convert.py csv_eicrecon -c config.yaml``.
"""

from .job_creator import JobCreator, exension_replacer, write_top_master_scripts
from .datasets import (
    load_config,
    load_config_for_energy,
    load_cards,
    write_card,
    run_card_pipeline,
)

__all__ = [
    "JobCreator",
    "exension_replacer",
    "write_top_master_scripts",
    "load_config",
    "load_config_for_energy",
    "load_cards",
    "write_card",
    "run_card_pipeline",
]
