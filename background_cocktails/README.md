# background_cocktails

Per-energy background "cocktail" JSONs for SignalBackgroundMerger (stage 11)
and the npsim status-offset flags (stage 22). Each file lists the background
sources mixed into a 2us timeframe:

    [{"file": <xrootd URL>, "freq": <events/ns>, "skip": <fraction>,
      "status": <generator-status offset>}, ...]

Copied from the official `eic_official_campaign_info` repo (`config_data/`),
which used to be a meson-structure submodule; they live here now so the
pipeline is self-contained. A config selects one per energy:

    background_configs:
      "9x130": "synrad_..._10GeVx130GeV_GoldCoating_10um_..._50s.json"

`background_config_dir` defaults to this directory; set it in the config to
use cocktails from somewhere else.
