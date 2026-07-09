#!/usr/bin/env python3
"""
40_create_csv_dd4hep_jobs.py

CSV conversion of dd4hep (edm4hep) output. Reads the 'csv_dd4hep' dataset cards
(`generate_datasets csv_dd4hep -c <config>`) and runs the configured `edm4hep_*`
converter macros over each *.edm4hep.root.

The macro set is config-driven (`csv_dd4hep.macros`); it has no default because
the edm4hep converters (acceptance / combinatorics) are analysis-specific and
live in the consuming project's csv_convert dir, not in the shared repo.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from simulation_pipeline.csv_stage import build_csv_creator
from simulation_pipeline.datasets import run_card_pipeline

STAGE = "csv_dd4hep"
DEFAULT_MACROS = []          # edm4hep converters are project-specific (Q8)
DEFAULT_STEM = "basename"


def build(config, card, config_path):
    return build_csv_creator(STAGE, config, card, config_path, DEFAULT_MACROS, DEFAULT_STEM)


if __name__ == "__main__":
    run_card_pipeline(STAGE, build, description="Generate CSV conversion jobs (dd4hep/edm4hep).")
