#!/usr/bin/env python3
"""
collect_job_stats.py

Pull the recent SLURM jobs of a user from `sacct` and report

  * where the campaign stands — done / running / queued / failed, as a pie
    plus a per-SLURM-state breakdown (TIMEOUT vs OUT_OF_MEMORY vs ...),
  * how much of the *requested* resources the jobs actually used: memory
    (MaxRSS vs --mem-per-cpu), CPU time (TotalCPU vs cores x walltime) and
    walltime (Elapsed vs --time).

Over-requesting is not free on the JLab farm: it is provisioned for 2 GB of
memory per CPU, so asking for more memory makes SLURM allocate (and bill) extra
CPUs, and a walltime much longer than the real runtime pushes jobs down the
queue. This script tells you what to put in the config next campaign.

For "how far did each job get through its file" (parsing *.slurm.log) see
collect_files_status.py.

Usage:
    # last 24 h of your jobs, status pie + histograms into ./job_stats/
    python collect_job_stats.py

    # a whole campaign, only the emcal profile jobs, text only
    python collect_job_stats.py -S 2026-07-20 -n 'llprof-*' --no-plot

    # one specific array job
    python collect_job_stats.py -j 12345678

    # save the raw sacct dump, then re-analyse it offline
    python collect_job_stats.py --save-raw sacct.psv
    python collect_job_stats.py --from-file sacct.psv

Requires `sacct` in PATH (login node). matplotlib is only needed for the plots
(`--no-plot` works without it).
"""

import argparse
import csv
import fnmatch
import os
import re
import shutil
import subprocess
import sys
from collections import defaultdict

# Parent-job line carries the request (ReqMem/ReqTRES/Timelimit) and the
# aggregate TotalCPU; the .batch/.extern step lines carry the measured MaxRSS.
# We therefore ask for both and merge them per job id below.
SACCT_FORMAT = [
    "JobID", "JobName", "State", "Partition", "AllocCPUS", "ReqCPUS",
    "ReqMem", "ReqTRES", "Timelimit", "Elapsed", "TotalCPU", "MaxRSS",
    "MaxVMSize", "ExitCode", "NodeList",
]

UNIT_BYTES = {"K": 1024, "M": 1024 ** 2, "G": 1024 ** 3, "T": 1024 ** 4}
GB = float(1024 ** 3)

# Coarse buckets for the "how is the campaign going" pie. Everything that is
# neither done nor still moving counts as failed — a CANCELLED job is work you
# have to redo just like a TIMEOUT one.
STATUS_GROUPS = [
    ("done",     ["COMPLETED"],                              "#2e8b57"),
    ("running",  ["RUNNING", "COMPLETING", "RESIZING"],      "#4682b4"),
    ("queued",   ["PENDING", "REQUEUED", "SUSPENDED"],       "#daa520"),
    ("failed",   None,                                       "#b22222"),  # catch-all
]
STATUS_ORDER = [name for name, _, _ in STATUS_GROUPS]
STATUS_COLOR = {name: color for name, _, color in STATUS_GROUPS}


def status_group(state):
    for name, states, _ in STATUS_GROUPS:
        if states and state in states:
            return name
    return "failed"


# --------------------------------------------------------------------------
# parsing helpers
# --------------------------------------------------------------------------

def parse_duration(text):
    """SLURM duration -> seconds. Handles [DD-][HH:]MM:SS[.mmm].

    Returns None for UNLIMITED / Partition_Limit / empty.
    """
    text = (text or "").strip()
    if not text or text in ("UNLIMITED", "Partition_Limit", "INVALID"):
        return None
    days = 0
    if "-" in text:
        d, _, text = text.partition("-")
        days = int(d)
    parts = text.split(":")
    try:
        parts = [float(p) for p in parts]
    except ValueError:
        return None
    while len(parts) < 3:            # "MM:SS" -> "00:MM:SS"
        parts.insert(0, 0.0)
    h, m, s = parts[-3:]
    return days * 86400 + h * 3600 + m * 60 + s


def parse_mem(text, default_unit="K"):
    """SLURM memory string -> bytes.

    Accepts '1234K', '12.5M', '2G' and the old per-cpu/per-node ReqMem
    suffixes '2Gc' / '2Gn' (the trailing c/n is stripped by the caller, which
    knows whether to multiply by the cpu count).
    """
    text = (text or "").strip()
    if not text:
        return None
    m = re.match(r"^([0-9.]+)\s*([KMGT])?", text, re.IGNORECASE)
    if not m:
        return None
    value = float(m.group(1))
    unit = (m.group(2) or default_unit).upper()
    return value * UNIT_BYTES.get(unit, 1)


def parse_req_mem(req_mem, req_tres, alloc_cpus, req_cpus):
    """Total memory requested by the job, in bytes.

    ReqTRES ('billing=1,cpu=1,mem=2G,node=1') is the total and is preferred.
    ReqMem is the fallback; on older SLURM it carries a 'c' (per cpu) or 'n'
    (per node) suffix that has to be expanded.
    """
    m = re.search(r"mem=([0-9.]+[KMGT]?)", req_tres or "", re.IGNORECASE)
    if m:
        return parse_mem(m.group(1), default_unit="M")

    req_mem = (req_mem or "").strip()
    if not req_mem:
        return None
    per = req_mem[-1] if req_mem[-1] in "cn" else None
    value = parse_mem(req_mem.rstrip("cn"), default_unit="M")
    if value is None:
        return None
    if per == "c":
        return value * max(1, alloc_cpus or req_cpus or 1)
    return value                      # per node (1 node) or already total


def base_job_id(job_id):
    """'12345_7.batch' -> '12345_7' (array task), '12345.extern' -> '12345'."""
    return job_id.split(".", 1)[0]


def is_step(job_id):
    return "." in job_id


# --------------------------------------------------------------------------
# sacct
# --------------------------------------------------------------------------

def run_sacct(args):
    if not shutil.which("sacct"):
        sys.exit("ERROR: sacct not found in PATH — run this on a SLURM login node "
                 "(or analyse a saved dump with --from-file).")

    cmd = ["sacct", "--parsable2", "--noheader", "--units=K",
           f"--format={','.join(SACCT_FORMAT)}"]
    if args.jobs:
        cmd += ["-j", args.jobs]
    else:
        cmd += ["-u", args.user, "-S", args.since]
        if args.until:
            cmd += ["-E", args.until]
    if args.state:
        cmd += ["-s", args.state]

    print("Running:", " ".join(cmd))
    out = subprocess.run(cmd, capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"ERROR: sacct failed ({out.returncode}):\n{out.stderr.strip()}")
    return out.stdout


def parse_sacct(raw, name_filter=None):
    """Merge parent-job and step lines into one record per job."""
    jobs = {}
    steps = defaultdict(list)

    for line in raw.splitlines():
        line = line.rstrip("\n")
        if not line.strip():
            continue
        fields = line.split("|")
        if len(fields) < len(SACCT_FORMAT):
            continue
        rec = dict(zip(SACCT_FORMAT, fields))
        if is_step(rec["JobID"]):
            steps[base_job_id(rec["JobID"])].append(rec)
        else:
            jobs[rec["JobID"]] = rec

    result = []
    for job_id, rec in jobs.items():
        name = rec["JobName"]
        if name_filter and not fnmatch.fnmatch(name, name_filter):
            continue

        alloc_cpus = int(rec["AllocCPUS"] or 0)
        req_cpus = int(rec["ReqCPUS"] or 0)
        cpus = alloc_cpus or req_cpus or 1

        # MaxRSS lives on the steps; take the peak over them (.batch is the
        # payload, .extern is prologue noise but harmless in a max).
        max_rss = max(
            [parse_mem(s["MaxRSS"]) or 0.0 for s in steps.get(job_id, [])] or [0.0])
        max_vmsize = max(
            [parse_mem(s["MaxVMSize"]) or 0.0 for s in steps.get(job_id, [])] or [0.0])

        elapsed = parse_duration(rec["Elapsed"])
        total_cpu = parse_duration(rec["TotalCPU"])
        # TotalCPU is only aggregated on the parent line in some SLURM
        # versions; fall back to summing the steps.
        if not total_cpu:
            total_cpu = sum(parse_duration(s["TotalCPU"]) or 0.0
                            for s in steps.get(job_id, []))
        timelimit = parse_duration(rec["Timelimit"])
        req_mem = parse_req_mem(rec["ReqMem"], rec["ReqTRES"], alloc_cpus, req_cpus)

        state = rec["State"].split()[0]             # 'CANCELLED by 1234' -> 'CANCELLED'
        result.append({
            "job_id": job_id,
            "name": name,
            "state": state,
            "group": status_group(state),
            "partition": rec["Partition"],
            "cpus": cpus,
            "exit_code": rec["ExitCode"],
            "node": rec["NodeList"],
            "req_mem_gb": (req_mem / GB) if req_mem else None,
            "max_rss_gb": max_rss / GB,
            "max_vmsize_gb": max_vmsize / GB,
            "elapsed_s": elapsed,
            "timelimit_s": timelimit,
            "total_cpu_s": total_cpu,
            "mem_eff": (max_rss / req_mem * 100.0) if (req_mem and max_rss) else None,
            "time_eff": (elapsed / timelimit * 100.0)
                        if (timelimit and elapsed) else None,
            "cpu_eff": (total_cpu / (elapsed * cpus) * 100.0)
                       if (elapsed and total_cpu) else None,
        })

    result.sort(key=lambda r: r["job_id"])
    return result


# --------------------------------------------------------------------------
# stats / reporting
# --------------------------------------------------------------------------

def percentile(values, q):
    """Nearest-rank percentile, q in [0, 100]. `values` need not be sorted."""
    if not values:
        return None
    s = sorted(values)
    k = max(0, min(len(s) - 1, int(round(q / 100.0 * len(s) + 0.5)) - 1))
    return s[k]


def summarize(values, unit="", scale=1.0):
    v = [x * scale for x in values if x is not None]
    if not v:
        return "        (no data)"
    return ("        min {:8.2f}{u}  median {:8.2f}{u}  mean {:8.2f}{u}\n"
            "        p95 {:8.2f}{u}  p99    {:8.2f}{u}  max  {:8.2f}{u}").format(
        min(v), percentile(v, 50), sum(v) / len(v),
        percentile(v, 95), percentile(v, 99), max(v), u=unit)


def fmt_hms(seconds):
    if seconds is None:
        return "  --:--:--"
    seconds = int(seconds)
    return f"{seconds // 3600:3d}:{seconds % 3600 // 60:02d}:{seconds % 60:02d}"


def print_report(jobs, top=10):
    print("\n" + "=" * 78)
    print(f"SLURM JOB RESOURCE REPORT   ({len(jobs)} jobs)")
    print("=" * 78)

    total = len(jobs)
    groups = defaultdict(int)
    states = defaultdict(int)
    states_by_group = defaultdict(lambda: defaultdict(int))
    for j in jobs:
        groups[j["group"]] += 1
        states[j["state"]] += 1
        states_by_group[j["group"]][j["state"]] += 1

    print("\nStatus:")
    for name in STATUS_ORDER:
        n = groups.get(name, 0)
        if not n:
            continue
        bar = "#" * int(round(n / total * 40))
        print(f"  {name:<9} {n:7d}  {n / total * 100:5.1f}%  {bar}")
        # Break the coarse bucket down so 'failed' is actionable.
        detail = sorted(states_by_group[name].items(), key=lambda kv: -kv[1])
        if len(detail) > 1:
            print("            " + ", ".join(f"{s} {c}" for s, c in detail))

    finished = groups.get("done", 0) + groups.get("failed", 0)
    in_flight = groups.get("running", 0) + groups.get("queued", 0)
    if finished:
        print(f"\n  Success rate  : {groups.get('done', 0) / finished * 100:5.1f}% "
              f"of {finished} finished job(s)")
    if in_flight:
        print(f"  Still to go   : {in_flight} job(s) "
              f"({in_flight / total * 100:.1f}% of the campaign)")

    # Only completed jobs carry meaningful peak usage: a job killed at 3 %
    # of its work says nothing about what a full job needs.
    done = [j for j in jobs if j["state"] == "COMPLETED"]
    pool = done or jobs
    print(f"\nResource statistics over {len(pool)} "
          f"{'COMPLETED' if done else '(no COMPLETED — using all)'} jobs:")

    req_mems = sorted({round(j["req_mem_gb"], 3) for j in pool
                       if j["req_mem_gb"]})
    req_times = sorted({j["timelimit_s"] for j in pool if j["timelimit_s"]})
    print(f"\n  Requested memory   : {', '.join(f'{m:g} GB' for m in req_mems) or 'n/a'}"
          f"   (total per job)")
    print(f"  Requested walltime : "
          f"{', '.join(fmt_hms(t).strip() for t in req_times) or 'n/a'}")
    print(f"  CPUs per job       : "
          f"{', '.join(str(c) for c in sorted({j['cpus'] for j in pool}))}")

    print("\n  Peak memory (MaxRSS), GB:")
    print(summarize([j["max_rss_gb"] for j in pool]))
    print("\n  Memory used / requested, %:")
    print(summarize([j["mem_eff"] for j in pool], unit="%"))
    print("\n  Walltime (Elapsed), hours:")
    print(summarize([j["elapsed_s"] for j in pool], scale=1 / 3600.0, unit=" h"))
    print("\n  Walltime used / requested, %:")
    print(summarize([j["time_eff"] for j in pool], unit="%"))
    print("\n  CPU efficiency (TotalCPU / cores*Elapsed), %:")
    print(summarize([j["cpu_eff"] for j in pool], unit="%"))

    # ---- concrete advice for the next config -----------------------------
    print("\n" + "-" * 78)
    print("SUGGESTED REQUESTS (p99 of observed usage + 25 % headroom)")
    print("-" * 78)
    rss_p99 = percentile([j["max_rss_gb"] for j in pool if j["max_rss_gb"]], 99)
    el_p99 = percentile([j["elapsed_s"] for j in pool if j["elapsed_s"]], 99)
    if rss_p99:
        want = rss_p99 * 1.25
        print(f"  memory   : {want:.2f} GB/job  "
              f"(p99 peak {rss_p99:.2f} GB, requested "
              f"{req_mems[-1] if req_mems else float('nan'):g} GB)")
        if req_mems and want < req_mems[-1] * 0.5:
            print(f"             -> over-requested by "
                  f"{req_mems[-1] / want:.1f}x; on a 2 GB/CPU farm this bills "
                  f"idle CPUs")
    if el_p99:
        want = el_p99 * 1.25
        print(f"  walltime : {fmt_hms(want).strip()}  "
              f"(p99 elapsed {fmt_hms(el_p99).strip()}, requested "
              f"{fmt_hms(req_times[-1]).strip() if req_times else 'n/a'})")
        if el_p99 < 120:
            print("             -> jobs shorter than 2 min: farm admins flag "
                  "these; batch more work per job")

    # ---- worst offenders --------------------------------------------------
    if top:
        heavy = sorted([j for j in pool if j["max_rss_gb"]],
                       key=lambda j: -j["max_rss_gb"])[:top]
        if heavy:
            print("\n" + "-" * 78)
            print(f"TOP {len(heavy)} JOBS BY PEAK MEMORY")
            print("-" * 78)
            print(f"  {'JobID':<16} {'Name':<24} {'MaxRSS':>8} {'mem%':>6} "
                  f"{'Elapsed':>10} {'cpu%':>6}")
            for j in heavy:
                print(f"  {j['job_id']:<16} {j['name'][:24]:<24} "
                      f"{j['max_rss_gb']:7.2f}G "
                      f"{(j['mem_eff'] or 0):5.1f}% {fmt_hms(j['elapsed_s']):>10} "
                      f"{(j['cpu_eff'] or 0):5.1f}%")

    failed = [j for j in jobs if j["group"] == "failed"]
    if failed:
        print("\n" + "-" * 78)
        print(f"NON-COMPLETED JOBS ({len(failed)})")
        print("-" * 78)
        by_reason = defaultdict(list)
        for j in failed:
            by_reason[(j["state"], j["exit_code"])].append(j)
        for (state, code), group in sorted(by_reason.items(),
                                           key=lambda kv: -len(kv[1])):
            sample = ", ".join(j["job_id"] for j in group[:3])
            print(f"  {state:<12} exit {code:<8} {len(group):6d}   e.g. {sample}")
            if state == "OUT_OF_MEMORY":
                print("      -> raise slurm_mem_per_cpu (note: >2G bills extra CPUs)")
            elif state == "TIMEOUT":
                print("      -> raise slurm_time for this dataset")

    print("=" * 78)
    return pool


# --------------------------------------------------------------------------
# plots
# --------------------------------------------------------------------------

def _pyplot():
    """Import matplotlib lazily; None if it is not installed."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        return plt
    except ImportError:
        print("\nWARN: matplotlib not installed — skipping plots "
              "(pip install matplotlib, or pass --no-plot)")
        return None


def make_status_plot(jobs, out_dir, prefix):
    """Pie of done/running/queued/failed + a bar of the exact SLURM states."""
    plt = _pyplot()
    if plt is None:
        return None

    groups = defaultdict(int)
    states = defaultdict(int)
    for j in jobs:
        groups[j["group"]] += 1
        states[j["state"]] += 1

    present = [g for g in STATUS_ORDER if groups.get(g)]
    counts = [groups[g] for g in present]
    total = sum(counts)

    fig, (ax_pie, ax_bar) = plt.subplots(
        1, 2, figsize=(13, 5.5), gridspec_kw={"width_ratios": [1, 1.3]})

    wedges, _, autotexts = ax_pie.pie(
        counts,
        labels=[f"{g}\n{groups[g]}" for g in present],
        colors=[STATUS_COLOR[g] for g in present],
        autopct=lambda pct: f"{pct:.1f}%" if pct >= 3 else "",
        startangle=90, counterclock=False,
        wedgeprops={"edgecolor": "white", "linewidth": 1.5},
        textprops={"fontsize": 10})
    for t in autotexts:
        t.set_color("white")
        t.set_fontweight("bold")
    ax_pie.set_title(f"Job status — {total} jobs", fontsize=12)

    # Exact states, so 'failed' is broken into TIMEOUT / OUT_OF_MEMORY / ...
    ordered = sorted(states.items(), key=lambda kv: (STATUS_ORDER.index(
        status_group(kv[0])), -kv[1]))
    labels = [s for s, _ in ordered]
    values = [c for _, c in ordered]
    bars = ax_bar.barh(range(len(labels)), values,
                       color=[STATUS_COLOR[status_group(s)] for s in labels])
    ax_bar.set_yticks(range(len(labels)))
    ax_bar.set_yticklabels(labels, fontsize=9)
    ax_bar.invert_yaxis()
    ax_bar.set_xlabel("jobs")
    ax_bar.set_title("SLURM states", fontsize=12)
    for bar, value in zip(bars, values):
        ax_bar.text(bar.get_width() + total * 0.01,
                    bar.get_y() + bar.get_height() / 2,
                    f"{value}  ({value / total * 100:.1f}%)",
                    va="center", fontsize=9)
    ax_bar.set_xlim(0, max(values) * 1.25)

    fig.tight_layout()
    path = os.path.join(out_dir, f"{prefix}_status.png")
    fig.savefig(path, dpi=110)
    plt.close(fig)
    print(f"Saved {path}")
    return path


def make_plots(pool, out_dir, prefix):
    plt = _pyplot()
    if plt is None:
        return None

    def vlines(ax, values, label):
        for i, v in enumerate(sorted(set(values))):
            ax.axvline(v, color="crimson", ls="--", lw=1.2,
                       label=label if i == 0 else None)

    panels = [
        ("Peak memory (MaxRSS)", "GB",
         [j["max_rss_gb"] for j in pool if j["max_rss_gb"]],
         [j["req_mem_gb"] for j in pool if j["req_mem_gb"]], "requested"),
        ("Memory used / requested", "%",
         [j["mem_eff"] for j in pool if j["mem_eff"] is not None], [100.0], "100 %"),
        ("Walltime (Elapsed)", "hours",
         [j["elapsed_s"] / 3600.0 for j in pool if j["elapsed_s"]],
         [j["timelimit_s"] / 3600.0 for j in pool if j["timelimit_s"]], "time limit"),
        ("Walltime used / requested", "%",
         [j["time_eff"] for j in pool if j["time_eff"] is not None], [100.0], "100 %"),
        ("CPU time (TotalCPU)", "hours",
         [j["total_cpu_s"] / 3600.0 for j in pool if j["total_cpu_s"]], [], None),
        ("CPU efficiency", "%",
         [j["cpu_eff"] for j in pool if j["cpu_eff"] is not None], [100.0], "100 %"),
    ]

    fig, axes = plt.subplots(2, 3, figsize=(16, 9))
    for ax, (title, unit, values, refs, ref_label) in zip(axes.flat, panels):
        if values:
            ax.hist(values, bins=40, color="steelblue", edgecolor="k", lw=0.4)
            if refs:
                vlines(ax, refs, ref_label)
                ax.legend(fontsize=8)
            med = percentile(values, 50)
            ax.axvline(med, color="darkgreen", lw=1.2,
                       label=f"median {med:.2f}")
            ax.legend(fontsize=8)
        else:
            ax.text(0.5, 0.5, "no data", ha="center", va="center",
                    transform=ax.transAxes, color="gray")
        ax.set_title(title, fontsize=11)
        ax.set_xlabel(unit)
        ax.set_ylabel("jobs")

    fig.suptitle(f"SLURM resource usage — {len(pool)} jobs", fontsize=13)
    fig.tight_layout()
    path = os.path.join(out_dir, f"{prefix}_resources.png")
    fig.savefig(path, dpi=110)
    plt.close(fig)
    print(f"\nSaved {path}")
    return path


def write_csv(jobs, path):
    cols = ["job_id", "name", "state", "partition", "cpus", "exit_code", "node",
            "req_mem_gb", "max_rss_gb", "max_vmsize_gb", "elapsed_s",
            "timelimit_s", "total_cpu_s", "mem_eff", "time_eff", "cpu_eff"]
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=cols)
        writer.writeheader()
        writer.writerows(jobs)
    print(f"Saved {path}")


# --------------------------------------------------------------------------

def parse_args():
    p = argparse.ArgumentParser(
        description="Histogram SLURM resource usage vs. what was requested.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Usage:")[1] if "Usage:" in __doc__ else None)
    p.add_argument("-u", "--user", default=os.environ.get("USER", ""),
                   help="user whose jobs to pull (default: $USER)")
    p.add_argument("-S", "--since", default="now-1days",
                   help="sacct start time, e.g. now-3days or 2026-07-20 "
                        "(default: now-1days)")
    p.add_argument("-E", "--until", default=None, help="sacct end time")
    p.add_argument("-j", "--jobs", default=None,
                   help="explicit job id list (comma separated); overrides -u/-S")
    p.add_argument("-n", "--name", default=None,
                   help="keep only jobs whose name matches this glob, "
                        "e.g. 'llprof-*'")
    p.add_argument("-s", "--state", default=None,
                   help="sacct state filter, e.g. COMPLETED,FAILED")
    p.add_argument("-o", "--out-dir", default="job_stats",
                   help="directory for plots/CSV (default: ./job_stats)")
    p.add_argument("--prefix", default="jobstats", help="output file prefix")
    p.add_argument("--top", type=int, default=10,
                   help="list N heaviest jobs (0 to disable)")
    p.add_argument("--no-plot", action="store_true", help="text report only")
    p.add_argument("--csv", action="store_true",
                   help="also write the parsed per-job table as CSV")
    p.add_argument("--save-raw", metavar="FILE",
                   help="save the raw sacct output for offline re-analysis")
    p.add_argument("--from-file", metavar="FILE",
                   help="read raw sacct output from FILE instead of running sacct")
    return p.parse_args()


def main():
    args = parse_args()

    if args.from_file:
        with open(args.from_file) as f:
            raw = f.read()
        print(f"Read sacct dump from {args.from_file}")
    else:
        if not args.jobs and not args.user:
            sys.exit("ERROR: no user — pass --user or set $USER.")
        raw = run_sacct(args)

    if args.save_raw:
        with open(args.save_raw, "w") as f:
            f.write(raw)
        print(f"Saved raw sacct output to {args.save_raw}")

    jobs = parse_sacct(raw, name_filter=args.name)
    if not jobs:
        sys.exit("No jobs matched. Widen --since, or check --name / --user.")

    pool = print_report(jobs, top=args.top)

    if not args.no_plot or args.csv:
        os.makedirs(args.out_dir, exist_ok=True)
    if not args.no_plot:
        make_status_plot(jobs, args.out_dir, args.prefix)
        make_plots(pool, args.out_dir, args.prefix)
    if args.csv:
        write_csv(jobs, os.path.join(args.out_dir, f"{args.prefix}_jobs.csv"))


if __name__ == "__main__":
    main()
