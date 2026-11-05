#!/usr/bin/env python3
"""Sanity check of a TimeframeBuilder (EDM4hep mode) output file.

    python3 check_tfb_frames.py FILE.edm4hep.root --frame-ns 200 [--signal-at 0] [--expect-frames 500]

Prints, and checks where an expectation is given:
  * number of frames (= TTree entries)
  * SubEventHeaders: sub-events per source per frame and time offsets (stored
    in `weight`) per source: range, whether they sit on the 10.15 ns bunch grid
    and inside [0, frame_ns). Sources with `attach_to_beam` carry the vertex
    z / c shift on top of the bunch time, so for them the grid and the frame
    edges are informational (beam-gas at z = +-14 m shifts by +-47 ns).
  * for --signal-at T: source 0 has exactly one sub-event per frame and its
    offset is exactly T
  * MCParticles.generatorStatus split by cocktail status block (0 signal,
    2000 SynRad, 3000 e-gas, 4000 Coulomb, 5000 Touschek, 6000 p-gas)
  * hit counts of a few collections, per frame

Exit code 1 if any check fails. Runs inside eic_xl (uproot, numpy, awkward).
"""
import argparse
import sys

import numpy as np
import uproot

BUNCH_NS = 10.15
STATUS_NAMES = {0: "signal", 2: "synrad", 3: "egas", 4: "coulomb", 5: "touschek", 6: "pgas"}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file")
    ap.add_argument("--frame-ns", type=float, required=True, help="timeframe length in ns")
    ap.add_argument("--signal-at", type=float, default=None,
                    help="expected fixed signal offset in ns (source 0); omit for random/no signal")
    ap.add_argument("--expect-frames", type=int, default=None)
    ap.add_argument("--source-names", default="",
                    help="comma list naming the sources in order (default: index numbers)")
    ap.add_argument("--strict-grid", default="",
                    help="comma list of source names whose offsets must sit on the bunch grid "
                         "and inside the frame (sources without attach_to_beam)")
    args = ap.parse_args()

    failures = []

    def check(ok, text):
        print(("  ok   " if ok else "  FAIL ") + text)
        if not ok:
            failures.append(text)

    tree = uproot.open(args.file)["events"]
    n_frames = tree.num_entries
    print(f"{args.file}\n  frames: {n_frames}")
    if args.expect_frames is not None:
        check(n_frames == args.expect_frames, f"frames == {args.expect_frames}")

    keys = tree.keys()
    seh = [k for k in keys if k.startswith("SubEventHeaders/")]
    check(bool(seh), "SubEventHeaders collection present")
    if not seh:
        return finish(failures)

    src = tree["SubEventHeaders/SubEventHeaders.runNumber"].array(library="np")
    off = tree["SubEventHeaders/SubEventHeaders.weight"].array(library="np")
    names = [s.strip() for s in args.source_names.split(",") if s.strip()]
    src_all = np.concatenate(src)
    off_all = np.concatenate(off)
    strict = {s.strip() for s in args.strict_grid.split(",") if s.strip()}
    print("  sub-events per source (total over all frames; mean per frame; offset min/max ns; on bunch grid; inside frame):")
    for s in np.unique(src_all):
        sel = src_all == s
        o = off_all[sel]
        on_grid = bool(np.allclose(o / BUNCH_NS, np.round(o / BUNCH_NS), atol=1e-4))
        inside = bool((o >= 0).all() and (o < args.frame_ns).all())
        label = names[s] if s < len(names) else f"source{s}"
        print(f"    {s} {label:9s} n={sel.sum():9d}  per frame {sel.sum() / n_frames:10.3f}  "
              f"offset [{o.min():9.3f}, {o.max():9.3f}]  grid={on_grid}  inside={inside}")
        check(sel.sum() > 0, f"{label}: present")
        if label in strict:
            check(inside, f"{label}: offsets inside [0, {args.frame_ns:g})")
            check(on_grid, f"{label}: offsets on the {BUNCH_NS} ns grid")

    if args.signal_at is not None:
        per_frame = np.array([(f == 0).sum() for f in src])
        check(bool((per_frame == 1).all()), "exactly one signal sub-event (source 0) per frame")
        sig_off = off_all[src_all == 0]
        check(bool((sig_off == args.signal_at).all()),
              f"signal offset exactly {args.signal_at:g} ns in every frame "
              f"(seen {np.unique(sig_off)[:5]})")

    if "MCParticles/MCParticles.generatorStatus" in keys:
        gs = tree["MCParticles/MCParticles.generatorStatus"].array(library="np")
        gs_all = np.concatenate(gs).astype(np.int64)
        print(f"  MCParticles: {len(gs_all)} total, {len(gs_all) / n_frames:.1f} per frame; by status block:")
        plain = (gs_all >= 0) & (gs_all < 10000)
        blocks, counts = np.unique(gs_all[plain] // 1000, return_counts=True)
        for b, c in zip(blocks, counts):
            print(f"    {int(b) * 1000:5d}+ {STATUS_NAMES.get(int(b), '?'):9s} {int(c):10d}")
        if (~plain).any():
            odd = np.unique(gs_all[~plain])
            print(f"    outside [0, 10000): {int((~plain).sum())} particles, {len(odd)} distinct values, "
                  f"e.g. {odd[:4].tolist()}")

    for coll in ("B0TrackerHits", "VertexBarrelHits", "SiBarrelHits", "ForwardRomanPotHits",
                 "ForwardOffMTrackerHits", "HcalFarForwardZDCHits", "EcalFarForwardZDCHits", "HcalEndcapPHits", "EcalEndcapPHits"):
        k = f"{coll}/{coll}.time"
        if k not in keys:   # SimCalorimeterHit has no time; the contributions carry it
            k = f"{coll}Contributions/{coll}Contributions.time"
        if k in keys:
            t = tree[k].array(library="np")
            n = np.array([len(x) for x in t])
            t_all = np.concatenate(t) if n.sum() else np.zeros(0)
            rng = f"time [{t_all.min():.1f}, {t_all.max():.1f}] ns" if len(t_all) else "no hits"
            print(f"  {coll:22s} hits/frame mean {n.mean():9.1f}  min {n.min():7d}  max {n.max():7d}  {rng}")

    return finish(failures)


def finish(failures):
    if failures:
        print(f"\n{len(failures)} check(s) FAILED:")
        for f in failures:
            print("  - " + f)
        return 1
    print("\nall checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
