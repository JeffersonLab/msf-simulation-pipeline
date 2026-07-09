"""Rucio dataset discovery for official campaigns.

Used only by ``generate_datasets <stage> --rucio`` to produce cards for
official-campaign files that already live on the grid. Auth is assumed handled
by the environment; ``rucio`` runs inside the campaign container via
``singularity exec`` so the host needs no rucio on PATH.

``discover_datasets(config)`` returns one dict per dataset DID matched by the
campaign's ``rucio_did_pattern`` / ``rucio_did_filters``:

    {did, slug, energy, rucio_metadata, files: [root://...]}

Design note: we deliberately parse almost nothing out of DIDs or filenames.
The one exception is the beam energy — a ``<int>x<int>`` path token (10x100) —
because cards group per energy. Everything else (generator, process, beam
effects, ...) is taken verbatim from ``rucio did metadata list``, which is the
authoritative source; guessing it from filename fragments proved fragile.
"""

import os
import re
import shlex
import subprocess
import sys

# A beam-energy token in a DID path, e.g. "10x100". This is the only DID
# content we interpret.
ENERGY_RE = re.compile(r"^\d+x\d+$")


def make_rucio_runner(container, bind_dirs):
    """Return ``run(args)`` that executes ``rucio <args>`` inside the container."""
    bindings = []
    for d in bind_dirs:
        d = os.path.abspath(d)
        bindings += ["-B", f"{d}:{d}"]
    prefix = ["singularity", "exec", *bindings, container, "rucio"]

    def run(args):
        cmd = [*prefix, *args]
        print("  $ " + " ".join(shlex.quote(c) for c in cmd))
        try:
            out = subprocess.run(cmd, check=True, text=True, capture_output=True).stdout
        except FileNotFoundError:
            sys.exit("ERROR: 'singularity' not found on PATH.")
        except subprocess.CalledProcessError as e:
            sys.stderr.write(e.stdout or "")
            sys.stderr.write(e.stderr or "")
            sys.exit(f"ERROR: rucio command failed (exit {e.returncode}).")
        return out.splitlines()

    return run


def parse_did_lines(lines):
    """Pull DID strings out of rucio output (plain lines or ASCII table)."""
    dids = []
    for line in lines:
        line = line.strip()
        # New CLI prints plain DIDs; old CLI prints a '| epic:/... |' table.
        if line.startswith("|"):
            line = line.split("|", 2)[1].strip()
        if line.startswith("epic:/"):
            dids.append(line)
    return dids


def parse_pfn_lines(lines):
    """Keep only the ``root://`` PFN lines from ``rucio replica list ... --pfns``."""
    return [ln.strip() for ln in lines if ln.strip().startswith("root://")]


_META_LINE_RE = re.compile(r"^(\w+):\s*(.*)$")


def _convert_meta_value(raw):
    """Coerce a rucio metadata string value to None / bool / int / float / str."""
    v = raw.strip()
    if v in ("", "None"):
        return None
    if v == "True":
        return True
    if v == "False":
        return False
    for cast in (int, float):
        try:
            return cast(v)
        except ValueError:
            pass
    return v


def parse_rucio_metadata(lines, drop_none=True):
    """Parse ``rucio did metadata list --plugin ALL`` aligned ``key: value`` output.

    Splits on the first ':' only (datetime values keep their colons). None-valued
    keys are dropped by default to keep the card tidy.
    """
    meta = {}
    for line in lines:
        m = _META_LINE_RE.match(line.rstrip())
        if not m:
            continue
        val = _convert_meta_value(m.group(2))
        if drop_none and val is None:
            continue
        meta[m.group(1)] = val
    return meta


def did_energy(did):
    """The beam-energy token (e.g. '10x100') from a DID path, or None."""
    for tok in did.split("/"):
        if ENERGY_RE.match(tok):
            return tok
    return None


def did_to_slug(did):
    """Flatten a DID path into one directory-safe slug that mirrors the DID.

    Everything after the detector segment (epic_craterlake) is joined with '_',
    so it is obvious which dataset a directory came from. 'minQ2=1' becomes
    'q2-gt-1' ('=' is not directory-safe); any other odd character becomes '_'.

      epic:/RECO/26.06.0/epic_craterlake/DIS/NC/10x100/minQ2=1
      -> DIS_NC_10x100_q2-gt-1
    """
    assert did.startswith("epic:/"), did
    parts = did[len("epic:/"):].split("/")
    if "epic_craterlake" in parts:
        tail = parts[parts.index("epic_craterlake") + 1:]
    else:
        tail = parts[3:]  # skip <partition>/<campaign>/<detector>
    tail = [re.sub(r"^minQ2=(\S+)$", r"q2-gt-\1", t) for t in tail if t]
    return re.sub(r"[^A-Za-z0-9._-]", "_", "_".join(tail))


def discover_datasets(config, max_files=0):
    """Return a list of dataset dicts discovered from rucio for this campaign."""
    filters = config.get("rucio_did_filters")
    pattern = config.get("rucio_did_pattern")
    container = config.get("container")
    bind_dirs = list(config.get("bind_dirs", []))
    if not pattern:
        raise SystemExit("--rucio requires 'rucio_did_pattern' in the config.")

    run_rucio = make_rucio_runner(container, bind_dirs)

    print("=" * 70)
    print("RUCIO DATASET DISCOVERY")
    print(f"  container: {container}")
    print(f"  filters:   {filters}")
    print(f"  pattern:   {pattern}")
    print("=" * 70)

    did_args = ["did", "list"]
    if filters:
        did_args += ["--filter", str(filters)]
    did_args += [str(pattern)]
    dids = parse_did_lines(run_rucio(did_args))
    print(f"\nFound {len(dids)} dataset DIDs.\n")

    datasets = []
    for i, did in enumerate(dids, 1):
        print(f"[{i}/{len(dids)}] {did}")
        pfns = parse_pfn_lines(run_rucio(
            ["replica", "list", "file", "--protocols", "root",
             "--pfns", "--rses", "isopenaccess", did]))
        if max_files:
            pfns = pfns[:max_files]
        if not pfns:
            print(f"      WARN: no PFNs for {did} -- skipping")
            continue

        datasets.append({
            "did": did,
            "slug": did_to_slug(did),
            "energy": did_energy(did),
            "rucio_metadata": parse_rucio_metadata(
                run_rucio(["did", "metadata", "list", "--plugin", "ALL", did])),
            "files": pfns,
        })
    return datasets
