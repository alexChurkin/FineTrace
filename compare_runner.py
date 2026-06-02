#!/usr/bin/env python3
"""
compare_runner.py — Run finetrace and VTune on the same samples, then
produce a side-by-side comparison of what each tool collects.

Data categories compared:
  ① Host API calls      finetrace --host-timing          vs VTune hotspots (CPU functions)
  ② Device events       finetrace --device-timeline      vs VTune gputime  (GPU timeline)
  ③ Device timing       finetrace --device-timing        vs VTune gputime  (GPU kernel time)
  ④ GPU metrics         finetrace --aggregation          vs VTune gpu-hotspots per-kernel

Results layout:
  compare_results/
    <sample>/
      finetrace/
        raw.txt                ← full finetrace output (all flags)
      vtune/
        gpu-offload/           ← VTune result dir  (① ② ③)
        gpu-hotspots/          ← VTune result dir  (④)
        reports/
          host_api.csv
          device_timing.csv
          device_metrics.csv
          summary_offload.txt
          summary_hotspots.txt
      comparison.txt           ← side-by-side report

Usage:
  python3 compare_runner.py [--samples ze_gemm cl_gemm ...] [--force]
                            [--no-vtune] [--no-finetrace] [--output-dir DIR]
"""

import argparse
import csv
import glob
import io
import os
import re
import shutil
import subprocess
import sys
import textwrap
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
ROOT        = Path(__file__).parent.resolve()
SAMPLES_DIR = ROOT / "samples"
RODINIA_DIR = SAMPLES_DIR / "cl_rodinia_benchmarks"
FINETRACE   = ROOT / "build" / "finetrace"
DEFAULT_OUT = ROOT / "compare_results"

VTUNE_SEARCH = [
    "/opt/intel/oneapi/vtune/2026.0/bin64/vtune",
    "/opt/intel/oneapi/vtune/latest/bin64/vtune",
    "/opt/intel/oneapi/vtune/bin64/vtune",
    "/opt/intel/vtune_profiler/bin64/vtune",
]

# ---------------------------------------------------------------------------
# VTune discovery
# ---------------------------------------------------------------------------

def find_vtune() -> Optional[str]:
    for env_var in ("VTUNE_HOME", "VTUNE_PROFILER_DIR"):
        home = os.environ.get(env_var)
        if home:
            c = Path(home) / "bin64" / "vtune"
            if c.is_file() and os.access(c, os.X_OK):
                return str(c)
    found = shutil.which("vtune")
    if found:
        return found
    for p in VTUNE_SEARCH:
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    for pat in ("/opt/intel/oneapi/vtune/*/bin64/vtune",
                "/opt/intel/vtune_*/bin64/vtune"):
        for hit in glob.glob(pat):
            if os.access(hit, os.X_OK):
                return hit
    return None

# ---------------------------------------------------------------------------
# Sample registry
# ---------------------------------------------------------------------------

@dataclass
class Sample:
    name:    str
    binary:  Path
    args:    list
    workdir: Path

def get_samples(platform: int = 0, device: int = 0) -> list:
    rd = RODINIA_DIR / "data"
    return [
        Sample("ze_gemm",
               SAMPLES_DIR / "ze_gemm/build/ze_gemm",
               ["512", "128"],
               SAMPLES_DIR / "ze_gemm/build"),
        Sample("cl_gemm",
               SAMPLES_DIR / "cl_gemm/build/cl_gemm",
               ["gpu", "512", "128"],
               SAMPLES_DIR / "cl_gemm/build"),
        Sample("bench_bfs",
               RODINIA_DIR / "bench_bfs/bfs.out",
               [str(rd / "bfs/graph1MW_6.txt"), "-p", str(platform), "-d", str(device)],
               RODINIA_DIR / "bench_bfs"),
        Sample("bench_gaussian",
               RODINIA_DIR / "bench_gaussian/gaussian.out",
               ["-s", "2048", "-p", str(platform), "-d", str(device)],
               RODINIA_DIR / "bench_gaussian"),
        Sample("bench_nw",
               RODINIA_DIR / "bench_nw/nw.out",
               ["8192", "10", "./nw.cl", "-p", str(platform), "-d", str(device)],
               RODINIA_DIR / "bench_nw"),
        Sample("bench_b+tree",
               RODINIA_DIR / "bench_b+tree/b+tree.out",
               ["file", str(rd / "b+tree/mil.txt"),
                "command", str(rd / "b+tree/command.txt"),
                "-p", str(platform), "-d", str(device)],
               RODINIA_DIR / "bench_b+tree"),
    ]

# ---------------------------------------------------------------------------
# Subprocess helpers
# ---------------------------------------------------------------------------

def _run(cmd, cwd=None, env=None, label="") -> tuple:
    """(rc, stdout, stderr)"""
    e = os.environ.copy()
    if env:
        e.update(env)
    print(f"  $ {' '.join(str(c) for c in cmd)}")
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, env=e)
    return r.returncode, r.stdout, r.stderr

# ---------------------------------------------------------------------------
# ① finetrace collection
# ---------------------------------------------------------------------------

FINETRACE_FLAGS = [
    "--host-timing",
    "--call-logging",
    "--device-timing",
    "--device-timeline",
    "--aggregation",
]

def run_finetrace(sample: Sample, out_dir: Path, force: bool) -> bool:
    raw = out_dir / "raw.txt"
    if raw.exists() and not force:
        print(f"  [skip] finetrace already collected (use --force)")
        return True
    out_dir.mkdir(parents=True, exist_ok=True)

    cmd = [str(FINETRACE)] + FINETRACE_FLAGS + [str(sample.binary)] + sample.args
    rc, stdout, stderr = _run(cmd, cwd=sample.workdir)
    combined = stdout + stderr
    raw.write_text(combined)
    if rc != 0:
        print(f"  [error] finetrace rc={rc}")
        return False
    print(f"  [ok] finetrace → {raw.relative_to(ROOT)}")
    return True

# ---------------------------------------------------------------------------
# ② VTune collection
# ---------------------------------------------------------------------------

def vtune_collect(vtune: str, analysis: str, sample: Sample,
                  result_dir: Path, force: bool) -> bool:
    if result_dir.exists() and not force:
        print(f"  [skip] vtune {analysis} already collected")
        return True
    if result_dir.exists():
        shutil.rmtree(result_dir)

    cmd = [vtune, "-collect", analysis, "-no-summary",
           "-result-dir", str(result_dir),
           "--", str(sample.binary)] + sample.args
    rc, stdout, stderr = _run(cmd, cwd=sample.workdir)
    if rc != 0:
        print(f"  [error] vtune {analysis} rc={rc}: {(stdout+stderr)[-400:]}")
        return False
    print(f"  [ok] vtune {analysis} → {result_dir.relative_to(ROOT)}")
    return True


def vtune_report(vtune: str, result_dir: Path, report: str,
                 out_file: Path, extra: list = None) -> bool:
    cmd = [vtune, "-report", report,
           "-result-dir", str(result_dir),
           "-format", "csv", "-csv-delimiter", "comma",
           "-report-output", str(out_file)]
    if extra:
        cmd += extra
    rc, stdout, stderr = _run(cmd)
    ok = rc == 0 and out_file.exists() and out_file.stat().st_size > 0
    if not ok:
        print(f"  [warn] vtune report '{report}' failed or empty", file=sys.stderr)
    return ok


def vtune_summary(vtune: str, result_dir: Path, out_file: Path) -> bool:
    cmd = [vtune, "-report", "summary",
           "-result-dir", str(result_dir),
           "-report-output", str(out_file)]
    rc, _, _ = _run(cmd)
    return rc == 0


def run_vtune(vtune: str, sample: Sample, vtune_dir: Path, force: bool) -> dict:
    """Returns {'offload_ok': bool, 'hotspots_ok': bool}"""
    vtune_dir.mkdir(parents=True, exist_ok=True)
    reports = vtune_dir / "reports"
    reports.mkdir(exist_ok=True)

    # gpu-offload: host API + device timing
    #   -group-by task  → L0/OpenCL host API calls with CPU time + task time + count
    #                      (same rows as finetrace --host-timing + --device-timing)
    offload_dir = vtune_dir / "gpu-offload"
    offload_ok  = vtune_collect(vtune, "gpu-offload", sample, offload_dir, force)
    if offload_ok and offload_dir.exists():
        vtune_report(vtune, offload_dir, "hotspots",
                     reports / "host_api.csv",
                     ["-group-by", "task"])
        vtune_report(vtune, offload_dir, "hotspots",
                     reports / "device_timing.csv",
                     ["-group-by", "computing-task"])
        vtune_summary(vtune, offload_dir,
                      reports / "summary_offload.txt")

    # gpu-hotspots: per-kernel GPU hardware metrics
    #   -group-by computing-task → XVE Active/Stall, Occupancy, L3 BW, pipelines, …
    #                              (same as finetrace --aggregation ComputeBasic)
    hotspots_dir = vtune_dir / "gpu-hotspots"
    hotspots_ok  = vtune_collect(vtune, "gpu-hotspots", sample, hotspots_dir, force)
    if hotspots_ok and hotspots_dir.exists():
        vtune_report(vtune, hotspots_dir, "hotspots",
                     reports / "device_metrics.csv",
                     ["-group-by", "computing-task"])
        vtune_summary(vtune, hotspots_dir,
                      reports / "summary_hotspots.txt")

    return {"offload_ok": offload_ok, "hotspots_ok": hotspots_ok}

# ---------------------------------------------------------------------------
# Parsing helpers
# ---------------------------------------------------------------------------

def _ns(sec: float) -> int:
    return int(sec * 1_000_000_000)


def _parse_csv(path: Path) -> tuple:
    """(fieldnames, rows). Skips VTune comment lines (#)."""
    if not path.exists() or path.stat().st_size == 0:
        return [], []
    lines = path.read_text(errors="replace").splitlines()
    data = [l for l in lines if l.strip() and not l.startswith("#")]
    if not data:
        return [], []
    reader = csv.DictReader(io.StringIO("\n".join(data)))
    rows = list(reader)
    return list(reader.fieldnames or []), rows


def _first_float(row: dict, *cols) -> Optional[float]:
    for c in cols:
        for k in row:
            if k.strip().lower() == c.lower():
                try:
                    return float(row[k].strip().replace(",", ""))
                except ValueError:
                    pass
    return None


def _first_str(row: dict, *cols) -> str:
    for c in cols:
        for k in row:
            if k.strip().lower() == c.lower():
                v = row[k].strip()
                if v:
                    return v
    return ""

# ---------------------------------------------------------------------------
# Parsing finetrace raw output
# ---------------------------------------------------------------------------

def parse_ft_host_timing(raw: str) -> list:
    """Returns [(fn, calls, total_ns, avg_ns, min_ns, max_ns)]"""
    rows = []
    in_section = False
    for line in raw.splitlines():
        if "=== API Timing Results" in line:
            in_section = True
        if not in_section:
            continue
        # CSV data line: Function, Calls, Time (ns), Time (%), Avg, Min, Max
        m = re.match(
            r'^\s*(\S.*?),\s*(\d+),\s*(\d+),\s*([\d.]+),\s*(\d+),\s*(\d+),\s*(\d+)',
            line)
        if m:
            fn, calls, total, pct, avg, mn, mx = m.groups()
            rows.append((fn.strip(), int(calls), int(total),
                          int(avg), int(mn), int(mx)))
    return rows


def parse_ft_device_timing(raw: str) -> list:
    """Returns [(kernel, calls, total_ns, avg_ns, min_ns, max_ns)]"""
    rows = []
    in_section = False
    for line in raw.splitlines():
        if "=== Device Timing Results" in line:
            in_section = True
        if not in_section:
            continue
        m = re.match(
            r'^\s*(\S.*?),\s*(\d+),\s*(\d+),\s*([\d.]+),\s*(\d+),\s*(\d+),\s*(\d+)',
            line)
        if m:
            k, calls, total, pct, avg, mn, mx = m.groups()
            rows.append((k.strip(), int(calls), int(total),
                          int(avg), int(mn), int(mx)))
    return rows


def parse_ft_device_timeline(raw: str) -> list:
    """Returns [(kernel_tag, append_ns, submit_ns, start_ns, end_ns)]"""
    rows = []
    pat = re.compile(
        r'Device Timeline.*?:\s+(.+?)\s+\[ns\]\s*=\s*(\d+)\s+\(append\)\s+(\d+)\s+\(submit\)\s+(\d+)\s+\(start\)\s+(\d+)\s+\(end\)')
    for line in raw.splitlines():
        m = pat.search(line)
        if m:
            kernel, app, sub, sta, end = m.groups()
            rows.append((kernel.strip(), int(app), int(sub), int(sta), int(end)))
    return rows


def parse_ft_metrics(raw: str) -> list:
    """Returns list of dicts per kernel from Aggregated Kernel Metrics section."""
    rows = []
    in_section = False
    header = []
    for line in raw.splitlines():
        if "== Aggregated Kernel Metrics ==" in line:
            in_section = True
            continue
        if not in_section:
            continue
        line = line.strip()
        if not line:
            continue
        # Header lines start with "Kernel,"
        if line.startswith("Kernel,"):
            header = [h.strip() for h in line.split(",")]
            continue
        if header and line:
            vals = line.split(",")
            if len(vals) >= len(header):
                rows.append(dict(zip(header, vals)))
    return rows


# ---------------------------------------------------------------------------
# Parsing VTune CSV reports
# ---------------------------------------------------------------------------

def parse_vtune_host_timing(csv_path: Path) -> list:
    """
    Returns [(fn, task_time_ns, cpu_time_ns, calls)] from
    hotspots -group-by task report.
    Columns: Task Type, CPU Time, Task Time, Task Count, Average Task Time, …
    """
    _, rows = _parse_csv(csv_path)
    result = []
    for row in rows:
        fn = _first_str(row, "Task Type", "Function", "function")
        if not fn or fn.startswith("[Outside"):
            continue
        task_t   = _first_float(row, "Task Time", "Task Time:Execution")
        cpu_t    = _first_float(row, "CPU Time", "CPU Time:Execution")
        calls_v  = _first_str(row, "Task Count", "Call Count", "Calls", "Count")
        try:
            calls = int(calls_v.replace(",", ""))
        except ValueError:
            calls = 0
        result.append((fn, _ns(task_t or 0), _ns(cpu_t or 0), calls))
    return result   # (fn, task_ns, cpu_ns, calls)


def parse_vtune_device_timing(csv_path: Path) -> list:
    """
    Returns [(kernel, total_ns, avg_ns, calls)] from
    hotspots -group-by computing-task on gpu-offload.
    Columns: Computing Task, Computing Task:Total Time, Computing Task:Average Time,
             Computing Task:Instance Count, …
    """
    _, rows = _parse_csv(csv_path)
    result = []
    for row in rows:
        k = _first_str(row, "Computing Task", "GPU Function", "Kernel", "Function")
        if not k or k.startswith("[Outside"):
            continue
        total = _first_float(row, "Computing Task:Total Time", "Task Time",
                              "Total Time", "GPU Time")
        avg   = _first_float(row, "Computing Task:Average Time", "Average Task Time",
                              "Average Time")
        cnt_v = _first_str(row, "Computing Task:Instance Count", "Task Count",
                           "Instance Count", "Calls", "Count")
        try:
            calls = int(cnt_v.replace(",", ""))
        except ValueError:
            calls = 1
        result.append((k, _ns(total or 0), _ns(avg or 0), calls))
    return result   # (kernel, total_ns, avg_ns, calls)


def parse_vtune_device_metrics(csv_path: Path) -> list:
    """
    Returns list of dicts (one per GPU kernel) from
    hotspots -group-by computing-task on gpu-hotspots.
    Columns include: Computing Task, XVE Array:Active(%), XVE Array:Stalled(%),
                     XVE Threads Occupancy(%), GPU L3:Average Bandwidth GB/s, …
    """
    fieldnames, rows = _parse_csv(csv_path)
    results = []
    for row in rows:
        k = _first_str(row, "Computing Task", "GPU Function", "Kernel", "Function")
        if not k or k.startswith("[Outside"):
            continue
        results.append({"Kernel": k,
                         **{f.strip(): row[f].strip() for f in row if row[f].strip()}})
    return results


# ---------------------------------------------------------------------------
# Comparison report builder
# ---------------------------------------------------------------------------

DIVIDER = "=" * 80
SUBDIV  = "-" * 80

def _tbl_row(label, ft_val, vt_val, width=30):
    return f"  {label:<{width}}  {str(ft_val):<35}  {str(vt_val)}"


def _section(title: str) -> str:
    return f"\n{DIVIDER}\n{title}\n{DIVIDER}\n"


def build_comparison(sample_name: str,
                     ft_raw_path: Path,
                     vtune_reports: Path) -> str:
    """Build the full side-by-side comparison text."""
    out = []
    out.append(_section(f"COMPARISON REPORT: {sample_name}"))
    out.append(f"{'Tool':<35}  {'finetrace':<35}  {'VTune 2026'}")
    out.append(f"{'Flags':<35}  {'--host-timing --call-logging':<35}")
    out.append(f"{'':35}  {'--device-timing --device-timeline':<35}  {'gpu-offload + gpu-hotspots'}")
    out.append(f"{'':35}  {'--aggregation':35}")
    out.append("")

    raw = ft_raw_path.read_text(errors="replace") if ft_raw_path.exists() else ""

    # ─── ① HOST API CALLS ──────────────────────────────────────────────────
    out.append(_section("① HOST API CALLS  (CPU-side API timing)"))
    out.append("  finetrace: --host-timing  →  total/avg/min/max per L0 / OpenCL call")
    out.append("  VTune:     gpu-offload -group-by task  →  Task Time + CPU Time per API call")
    out.append("")

    ft_host = parse_ft_host_timing(raw)
    vt_host = parse_vtune_host_timing(vtune_reports / "host_api.csv")

    # ft: (fn, calls, total_ns, avg_ns, min_ns, max_ns)
    # vt: (fn, task_ns, cpu_ns, calls)
    ft_by_fn = {r[0]: r for r in ft_host}
    vt_by_fn = {r[0]: r for r in vt_host}
    all_fns  = sorted(set(ft_by_fn) | set(vt_by_fn),
                      key=lambda f: -(ft_by_fn.get(f, ("",0,0,0,0,0))[2] +
                                      vt_by_fn.get(f, ("",0,0,0))[1]))

    out.append(f"  {'Function':<50}  {'FT Calls':>8}  {'FT Total(ns)':>13}  "
               f"{'FT Avg(ns)':>11}  {'VT TaskT(ns)':>13}  {'VT CpuT(ns)':>12}  {'VT Calls':>8}")
    out.append("  " + SUBDIV)

    for fn in all_fns[:35]:
        ft = ft_by_fn.get(fn, ("", 0, 0, 0, 0, 0))
        vt = vt_by_fn.get(fn, ("", 0, 0, 0))
        def f(v): return str(v) if v else "-"
        out.append(f"  {fn:<50}  {f(ft[1]):>8}  {f(ft[2]):>13}  "
                   f"{f(ft[3]):>11}  {f(vt[1]):>13}  {f(vt[2]):>12}  {f(vt[3]):>8}")

    if not ft_host and not vt_host:
        out.append("  (no data from either tool)")
    elif not vt_host:
        out.append("\n  NOTE: VTune host_api.csv empty — check gpu-offload collection")
    elif not ft_host:
        out.append("\n  NOTE: finetrace host timing not found in raw output")

    # ─── ② DEVICE TIMELINE ─────────────────────────────────────────────────
    out.append(_section("② DEVICE TIMELINE  (per-event: append / submit / start / end)"))
    out.append("  finetrace --device-timeline:")
    out.append("")

    ft_tl = parse_ft_device_timeline(raw)
    if ft_tl:
        out.append(f"  {'Kernel':<50}  {'Append':>14}  {'Submit':>14}  "
                   f"{'Start':>14}  {'End':>14}  {'Exec (ns)':>12}")
        out.append("  " + SUBDIV)
        shown = ft_tl[:30]
        for kernel, app, sub, sta, end in shown:
            exec_ns = end - sta
            out.append(f"  {kernel:<50}  {app:>14}  {sub:>14}  "
                       f"{sta:>14}  {end:>14}  {exec_ns:>12}")
        if len(ft_tl) > 30:
            out.append(f"  ... ({len(ft_tl) - 30} more events — see finetrace/raw.txt)")
    else:
        out.append("  (no timeline events in finetrace output)")

    out.append("")
    out.append("  VTune (gpu-offload) device timeline:")
    out.append("  → VTune does not export per-event append/submit timestamps to CSV.")
    out.append("    Open the gpu-offload result dir in VTune GUI for the full timeline:")
    out.append(f"    vtune-gui <compare_results/{sample_name}/vtune/gpu-offload>")

    # ─── ③ DEVICE TIMING ───────────────────────────────────────────────────
    out.append(_section("③ DEVICE TIMING  (per-kernel aggregate: calls / time / avg)"))
    out.append("  finetrace: --device-timing  →  total/avg/min/max per kernel")
    out.append("  VTune:     gpu-offload -group-by computing-task  →  Total/Avg Time + Instance Count")
    out.append("")

    ft_dt = parse_ft_device_timing(raw)
    vt_dt = parse_vtune_device_timing(vtune_reports / "device_timing.csv")

    # ft: (kernel, calls, total_ns, avg_ns, min_ns, max_ns)
    # vt: (kernel, total_ns, avg_ns, calls)
    ft_dk = {r[0]: r for r in ft_dt}
    vt_dk = {r[0]: r for r in vt_dt}
    all_k = sorted(set(ft_dk) | set(vt_dk),
                   key=lambda k: -(ft_dk.get(k, ("",0,0,0,0,0))[2] +
                                   vt_dk.get(k, ("",0,0,0))[1]))

    out.append(f"  {'Kernel':<50}  {'FT Calls':>8}  {'FT Total(ns)':>13}  "
               f"{'FT Avg(ns)':>11}  {'VT Total(ns)':>13}  {'VT Avg(ns)':>11}  {'VT Calls':>8}")
    out.append("  " + SUBDIV)

    def f(v): return str(v) if v else "-"

    for k in all_k:
        ft = ft_dk.get(k, ("", 0, 0, 0, 0, 0))
        vt = vt_dk.get(k, ("", 0, 0, 0))
        out.append(f"  {k:<50}  {f(ft[1]):>8}  {f(ft[2]):>13}  "
                   f"{f(ft[3]):>11}  {f(vt[1]):>13}  {f(vt[2]):>11}  {f(vt[3]):>8}")

    if not ft_dt and not vt_dt:
        out.append("  (no data from either tool)")

    # ─── ④ GPU METRICS ─────────────────────────────────────────────────────
    out.append(_section("④ GPU HARDWARE METRICS  (per-kernel, aggregated)"))
    out.append("  finetrace: --aggregation (ComputeBasic via L0 Metrics API)")
    out.append("  VTune:     gpu-hotspots -group-by computing-task")
    out.append("")

    ft_metrics = parse_ft_metrics(raw)
    vt_metrics = parse_vtune_device_metrics(vtune_reports / "device_metrics.csv")

    # Collapse finetrace rows per kernel: prefer the sample with the highest
    # CsThreads (most GPU activity) — the first sample may fall on the metric-
    # stream startup boundary and show zeros for all ratio metrics.
    ft_by_kernel = {}
    def _cs(row):
        try:
            return int(row.get("CsThreads[threads]", "0") or "0")
        except ValueError:
            return 0
    for row in ft_metrics:
        base = row.get("Kernel", "?").split("[")[0].strip()
        if base not in ft_by_kernel or _cs(row) > _cs(ft_by_kernel[base]):
            ft_by_kernel[base] = row

    # Collapse VTune rows: same
    vt_by_kernel = {}
    for row in vt_metrics:
        base = row.get("Kernel", "?").split("[")[0].strip()
        if base not in vt_by_kernel:
            vt_by_kernel[base] = row

    # Mapping: (finetrace_col, vt_col_substr, description)
    # VTune computing-task columns verified against gpu-hotspots 2026.0 output.
    METRIC_MAP = [
        ("KernelTime[ns]",              "Computing Task:Total Time",        "Kernel exec time"),
        ("GpuTime[ns]",                 "Computing Task:Total Time",        "GPU time (same column in VTune)"),
        ("CsThreads[threads]",          "Computing Threads Started",        "Compute threads launched"),
        ("XveActive[%]",                "XVE Array:Active",                 "XVE active (any pipeline)"),
        ("XveStall[%]",                 "XVE Array:Stalled",                "XVE stalled"),
        ("FpuActive[%]",                "XVE Pipelines:ALU0 active",        "FPU/ALU0 active"),
        ("EmActive[%]",                 "XVE Pipelines:ALU1 active",        "EM/ALU1 active"),
        ("XmxActive[%]",                "XVE Pipelines:XMX active",         "XMX (matrix) active"),
        ("XveThreadOccupancy[%]",       "XVE Threads Occupancy",            "Thread occupancy"),
        ("GtiReadThroughput[bytes]",    "GPU L3:Average Bandwidth, GB/s:Read",  "GTI/L3 read BW (FT=bytes, VT=GB/s)"),
        ("GtiWriteThroughput[bytes]",   "GPU L3:Average Bandwidth, GB/s:Write", "GTI/L3 write BW (FT=bytes, VT=GB/s)"),
        ("SlmReads[messages]",          "XVE Instructions:Send Instructions",   "SLM/Send instructions"),
    ]

    all_kernels = sorted(set(ft_by_kernel) | set(vt_by_kernel))

    if not all_kernels:
        out.append("  (no metric data — check --aggregation and gpu-hotspots collection)")
    else:
        for kname in all_kernels:
            ft_row = ft_by_kernel.get(kname, {})
            vt_row = vt_by_kernel.get(kname, {})

            ft_full = ft_row.get("Kernel", kname)
            vt_full = vt_row.get("Kernel", kname)
            out.append(f"  ┌─ Kernel: {kname}")
            out.append(f"  │  finetrace key: {ft_full}")
            out.append(f"  │  VTune key:     {vt_full}")
            out.append(f"  │")
            out.append(f"  │  {'Metric':<40}  {'finetrace':>18}  {'VTune':>18}  Notes")
            out.append(f"  │  " + "-"*90)

            for ft_col, vt_substr, note in METRIC_MAP:
                ft_val = ft_row.get(ft_col, "-").strip() if ft_row else "-"

                # fuzzy-find the VTune column
                vt_val = "-"
                if vt_row:
                    for k in vt_row:
                        if vt_substr.lower() in k.lower():
                            raw_vt = vt_row[k].strip()
                            # VTune time cols are in seconds — convert to ns for comparison
                            if "Total Time" in k or "Average Time" in k:
                                try:
                                    vt_val = str(_ns(float(raw_vt))) + " ns"
                                except ValueError:
                                    vt_val = raw_vt
                            else:
                                vt_val = raw_vt
                            break

                # Annotate unit difference where it matters
                if vt_val != "-" and ft_val != "-" and "GB/s" in vt_substr:
                    note += " ⚠ units differ"

                out.append(f"  │  {ft_col:<40}  {ft_val:>18}  {vt_val:>18}  {note}")
            out.append("")

    # SIMD / work-size info that VTune adds
    if vt_by_kernel:
        out.append("  VTune additional info (no finetrace equivalent):")
        extra_cols = ["Work Size:Global", "Work Size:Local", "Computing Task:SIMD Width",
                      "Transfer Size:Host-to-Device", "Transfer Size:Device-to-Host",
                      "GPU Memory Bandwidth, GB/sec:Read", "GPU Memory Bandwidth, GB/sec:Write",
                      "GPU Barriers", "GPU Atomics"]
        for kname, vt_row in vt_by_kernel.items():
            out.append(f"    {kname}:")
            for col in extra_cols:
                val = vt_row.get(col, "").strip()
                if val and val != "0":
                    out.append(f"      {col:<45} {val}")
        out.append("")

    # ─── ⑤ COVERAGE SUMMARY ────────────────────────────────────────────────
    out.append(_section("⑤ COVERAGE SUMMARY"))
    out.append(f"  {'Category':<45}  {'finetrace':<25}  {'VTune'}")
    out.append("  " + SUBDIV)
    rows_cov = [
        ("Host API calls (names + timing)",
         f"✓ {len(ft_host)} API functions",
         f"✓ {len(vt_host)} API tasks" if vt_host else "✗ empty CSV"),
        ("Host API per-call logging (args + retval)",
         "✓ --call-logging",
         "✗ not available"),
        ("Device timeline (append/submit/start/end ns)",
         f"✓ {len(ft_tl)} events",
         "⚡ GUI only (timeline not in CSV)"),
        ("Device kernel timing (aggregate per kernel)",
         f"✓ {len(ft_dt)} kernels",
         f"✓ {len(vt_dt)} kernels" if vt_dt else "? check device_timing.csv"),
        ("GPU hardware metrics (per-kernel aggregated)",
         f"✓ {len(ft_by_kernel)} kernels (ComputeBasic)",
         f"✓ {len(vt_by_kernel)} kernels" if vt_by_kernel else "? check device_metrics.csv"),
        ("Chrome JSON trace export",
         "✓ --chrome-* flags",
         "✓ via VTune GUI"),
        ("Multi-tile / sub-device breakdown",
         "✓ --kernels-per-tile",
         "✓ implicit in computing-task"),
        ("OpenCL + Level Zero unified",
         "✓ both backends",
         "✓ both backends"),
    ]
    for cat, ft_v, vt_v in rows_cov:
        out.append(f"  {cat:<45}  {ft_v:<25}  {vt_v}")

    out.append("")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# Per-sample orchestration
# ---------------------------------------------------------------------------

def run_sample(sample: Sample, out_root: Path, vtune: Optional[str],
               run_ft: bool, run_vt: bool, force: bool):
    sdir      = out_root / sample.name
    ft_dir    = sdir / "finetrace"
    vtune_dir = sdir / "vtune"
    sdir.mkdir(parents=True, exist_ok=True)

    print(f"\n{'='*60}\nSample: {sample.name}\n{'='*60}")

    if not sample.binary.exists():
        print(f"  [skip] binary not found: {sample.binary}")
        print(f"         Run: ./script_build_samples.sh")
        return

    # finetrace
    ft_ok = True
    if run_ft:
        print("\n[finetrace]")
        ft_ok = run_finetrace(sample, ft_dir, force)

    # VTune
    vt_status = {}
    if run_vt and vtune:
        print("\n[VTune]")
        vt_status = run_vtune(vtune, sample, vtune_dir, force)

    # Comparison report
    print("\n[comparison report]")
    ft_raw     = ft_dir / "raw.txt"
    vtune_rpts = vtune_dir / "reports"
    comparison = build_comparison(sample.name, ft_raw, vtune_rpts)
    cmp_file   = sdir / "comparison.txt"
    cmp_file.write_text(comparison)
    print(f"  → {cmp_file.relative_to(ROOT)}")

    # Print to stdout too
    print("\n" + comparison)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args():
    p = argparse.ArgumentParser(
        description="Run finetrace AND VTune on the same samples, produce "
                    "side-by-side comparison.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent("""\
            Prerequisites:
              • finetrace built:  ./script_build_finetrace.sh
              • samples built:    ./script_build_samples.sh
              • VTune installed:  sudo apt install intel-oneapi-vtune
              • perf paranoid:    echo 1 | sudo tee /proc/sys/kernel/perf_event_paranoid
              • i915 paranoid:    echo 0 | sudo tee /proc/sys/dev/i915/perf_stream_paranoid
        """),
    )
    p.add_argument("--vtune", metavar="PATH",
                   help="Path to vtune binary (auto-detected)")
    p.add_argument("--samples", nargs="+", metavar="NAME",
                   help="Specific samples to run (default: all)")
    p.add_argument("--output-dir", default=str(DEFAULT_OUT), metavar="DIR")
    p.add_argument("--platform", type=int, default=0,
                   help="OpenCL platform index (0=GPU)")
    p.add_argument("--device", type=int, default=0)
    p.add_argument("--no-vtune",     action="store_true",
                   help="Skip VTune (finetrace only)")
    p.add_argument("--no-finetrace", action="store_true",
                   help="Skip finetrace (VTune only)")
    p.add_argument("--force", action="store_true",
                   help="Re-collect even if results already exist")
    p.add_argument("--list", action="store_true",
                   help="List samples and exit")
    return p.parse_args()


def main():
    sys.stdout.reconfigure(line_buffering=True)
    sys.stderr.reconfigure(line_buffering=True)

    args = parse_args()
    all_samples = get_samples(args.platform, args.device)

    if args.list:
        for s in all_samples:
            status = "OK" if s.binary.exists() else "NOT BUILT"
            print(f"  {s.name:<25} [{status}]  {s.binary}")
        return

    samples = all_samples
    if args.samples:
        names   = set(args.samples)
        samples = [s for s in all_samples if s.name in names]
        missing = names - {s.name for s in samples}
        if missing:
            print(f"[warn] unknown samples: {', '.join(sorted(missing))}")

    run_vt = not args.no_vtune
    run_ft = not args.no_finetrace

    # ── Paranoid / access knobs ──────────────────────────────────────────────
    # Must be set before any collection; try sudo automatically, print
    # manual commands if sudo is not available.
    _PARANOID = [
        ("/proc/sys/dev/i915/perf_stream_paranoid",  "0",
         "finetrace --aggregation (L0 Metrics API)"),
        ("/proc/sys/kernel/perf_event_paranoid",      "1",
         "VTune CPU-side sampling (gpu-offload / gpu-hotspots)"),
        ("/proc/sys/kernel/kptr_restrict",            "0",
         "VTune kernel symbol resolution"),
    ]
    print("[paranoid] Checking / setting required kernel knobs:")
    for path, need, why in _PARANOID:
        if not os.path.exists(path):
            continue
        cur = Path(path).read_text().strip()
        ok  = int(cur) <= int(need)
        if ok:
            print(f"  [ok]   {path} = {cur}")
        else:
            print(f"  [need] {path} = {cur} → {need}  ({why})")
            try:
                subprocess.run(["sudo", "tee", path], input=need, text=True,
                               capture_output=True, check=True)
                print(f"         → done")
            except subprocess.CalledProcessError:
                print(f"  [warn] sudo failed — run manually:", file=sys.stderr)
                print(f"         echo {need} | sudo tee {path}", file=sys.stderr)
    print()

    vtune = None
    if run_vt:
        vtune = args.vtune or find_vtune()
        if not vtune:
            print("[error] VTune not found. Install with: sudo apt install intel-oneapi-vtune\n"
                  "        Then source /opt/intel/oneapi/vtune/latest/env/vars.sh\n"
                  "        Or pass --vtune /path/to/vtune", file=sys.stderr)
            sys.exit(1)
        print(f"VTune: {vtune}")

    if run_ft and not FINETRACE.exists():
        print(f"[error] finetrace not found at {FINETRACE}\n"
              "        Build with: ./script_build_finetrace.sh", file=sys.stderr)
        sys.exit(1)

    out_root = Path(args.output_dir)
    out_root.mkdir(parents=True, exist_ok=True)

    for sample in samples:
        run_sample(sample, out_root, vtune, run_ft, run_vt, args.force)

    print(f"\n{'='*60}")
    print(f"Results in: {out_root}")
    for s in samples:
        cmp = out_root / s.name / "comparison.txt"
        tag = "OK" if cmp.exists() else "MISSING"
        print(f"  {s.name:<25} [{tag}]  → {cmp}")


if __name__ == "__main__":
    main()
