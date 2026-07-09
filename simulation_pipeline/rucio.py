"""Rucio dataset discovery for official campaigns.

Absorbed from ai-epic-background's ``42_create_datasets_list.py``. Used only by
``generate_datasets --rucio`` to produce cards for official-campaign files that already
live on the grid (RECO ``edm4eic`` or FULL ``edm4hep``). Auth is assumed handled
by the environment; ``rucio`` is invoked inside the campaign container via
``singularity exec`` so the host needs no rucio on PATH.

``discover_datasets(config)`` returns a list of dicts:

    {did, slug, energy, metadata, rucio_metadata, files: [root://...]}

one per dataset DID matched by the campaign's ``rucio_did_pattern`` /
``rucio_did_filters``. :mod:`simulation_pipeline.generate_datasets` turns each into a card.
"""

import os
import re
import shlex
import subprocess
import sys

ENERGY_RE = re.compile(r"^\d+x\d+$")        # 10x100
FRAME_LEN_RE = re.compile(r"^\d+us$")       # 2us
MINQ2_RE = re.compile(r"minQ2=(\S+)")
Q2_ALT_RE = re.compile(r"q2_(\w+)")
GEN_RE = re.compile(r"^([A-Za-z]+\d*)")     # pythia8 (from pythia8NCDIS...)
XANGLE_RE = re.compile(r"xAngle=(-?\d+(?:\.\d+)?)")
DIV_RE = re.compile(r"(hiDiv|loDiv)(?:_(\d+))?")

DATA_TYPE = {"RECO": "reconstructed", "FULL": "simulated"}
FRAME_TYPE = {"Exact1S": "1sig-per-fr"}
PROCESS = {"DIS": "dis", "SIDIS": "sidis"}
INTERACTION = {"NC": "nc", "CC": "cc"}


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
    """Parse ``rucio did metadata list --plugin ALL`` aligned ``key: value`` output."""
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


def fetch_rucio_metadata(run_rucio, did):
    return parse_rucio_metadata(
        run_rucio(["did", "metadata", "list", "--plugin", "ALL", did]))


def parse_metadata(did, sample_file=None):
    """Extract structured metadata from a DID (+ a sample filename)."""
    assert did.startswith("epic:/"), did
    parts = did[len("epic:/"):].split("/")
    partition = parts[0]
    tail = parts[3:]

    meta = {
        "data_type": DATA_TYPE.get(partition, partition.lower()),
        "campaign": parts[1] if len(parts) > 1 else None,
        "detector": parts[2] if len(parts) > 2 else None,
        "has_background": False,
    }

    for tok in tail:
        sub = tok.split("_")
        if sub[0] == "Bkg":
            meta["has_background"] = True
            for s in sub[1:]:
                if FRAME_LEN_RE.match(s):
                    meta["frame_len"] = s
                elif s in FRAME_TYPE:
                    meta["frame_type"] = FRAME_TYPE[s]
                else:
                    meta.setdefault("frame_type", s.lower())
            break

    if "GoldCt" in tail:
        i = tail.index("GoldCt")
        thick = tail[i + 1] if i + 1 < len(tail) else None
        meta["beampipe"] = "gold-coat" + (f"-{thick}" if thick else "")

    process = next((PROCESS[t] for t in tail if t in PROCESS), None)
    interaction = next((INTERACTION[t] for t in tail if t in INTERACTION), None)
    meta["beam_energy"] = next((t for t in tail if ENERGY_RE.match(t)), None)

    m = MINQ2_RE.search(did) or Q2_ALT_RE.search(did)
    meta["q2"] = f"gt-{m.group(1)}" if m else None

    generator = "pythia8"
    if sample_file:
        fn = os.path.basename(sample_file)
        gm = GEN_RE.match(fn)
        if gm:
            generator = gm.group(1).lower()
        meta["beam_effects"] = "beamEffects" in fn
        xm = XANGLE_RE.search(fn)
        if xm:
            meta["beam_crossing_angle"] = float(xm.group(1))
        dm = DIV_RE.search(fn)
        if dm:
            meta["beam_divergence"] = dm.group(1).lower() + \
                (f"_{dm.group(2)}" if dm.group(2) else "")
    meta["generator"] = generator

    if process:
        bits = [generator] + ([interaction] if interaction else []) + [process]
        meta["physics"] = "-".join(bits)

    return meta


def _rename_token(tok):
    m = re.fullmatch(r"minQ2=(\S+)", tok)
    if m:
        return f"q2-gt-{m.group(1)}"
    return tok


def did_to_slug(did):
    """Flatten a DID path (after ``epic_craterlake``) into one directory slug."""
    assert did.startswith("epic:/"), did
    parts = did[len("epic:/"):].split("/")
    if "epic_craterlake" in parts:
        tail = parts[parts.index("epic_craterlake") + 1:]
    else:
        tail = parts[3:]
    slug = "_".join(_rename_token(t) for t in tail if t)
    return re.sub(r"[^A-Za-z0-9._-]", "_", slug)


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

        meta = parse_metadata(did, sample_file=pfns[0])
        datasets.append({
            "did": did,
            "slug": did_to_slug(did),
            "energy": meta.get("beam_energy"),
            "metadata": meta,
            "rucio_metadata": fetch_rucio_metadata(run_rucio, did),
            "files": pfns,
        })
    return datasets
