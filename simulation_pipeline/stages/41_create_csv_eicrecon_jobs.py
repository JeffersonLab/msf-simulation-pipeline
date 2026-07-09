#!/usr/bin/env python3
"""
41_create_csv_eicrecon_jobs.py

CSV conversion of eicrecon (edm4eic) output. Reads the 'csv_eicrecon' dataset
cards (`generate_datasets csv_eicrecon -c <config>`, or `generate_datasets csv_eicrecon
--rucio -c <official-config>`) and runs the configured `edm4eic_*` converter
macros over each *.edm4eic.root (local files or streamed root:// PFNs).

The macro set and output stem are config-driven (`csv_eicrecon.macros`,
`csv_eicrecon.stem`): meson-structure selects the Lambda + DIS set with
`stem: basename`; official campaigns select the generic set with
`stem: file_index`.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from simulation_pipeline.csv_stage import build_csv_creator
from simulation_pipeline.datasets import run_card_pipeline

STAGE = "csv_eicrecon"
DEFAULT_MACROS = ["edm4eic_reco_particles"]
DEFAULT_STEM = "basename"


def build(config, card, config_path):
    return build_csv_creator(STAGE, config, card, config_path, DEFAULT_MACROS, DEFAULT_STEM)


if __name__ == "__main__":
    run_card_pipeline(STAGE, build, description="Generate CSV conversion jobs (eicrecon/edm4eic).")
