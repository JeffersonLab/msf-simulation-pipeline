"""Shared EIC full-sim simulation pipeline.

Everything from the afterburner stage onward reads a *dataset card* (a small YAML
with a ``files:`` array), produced by the universal card producer
(:mod:`simulation_pipeline.generate_datasets`). The stage job generators live in
:mod:`simulation_pipeline.stages` and consume those cards; they never glob a
directory themselves.
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
