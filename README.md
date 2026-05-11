# FineTrace – Tracing & Profiling Tool

## Overview

FineTrace is a tracing and profiling tool for GPU compute applications built on Intel runtimes for OpenCL™ and oneAPI Level Zero. It supports DPC++, Intel® ISPC, OpenMP GPU offload programs, and any application that uses these APIs.

FineTrace works as a loader: it sets up the environment, injects the tracing library via `LD_PRELOAD`, and launches the target application transparently.

---

## Quick Start

```sh
cd finetrace
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Basic device timing
finetrace --device-timing ./samples/ze_gemm/build/ze_gemm

# Host API timing + device timing
finetrace --host-timing --device-timing ./samples/ze_gemm/build/ze_gemm

# GPU hardware metrics (ComputeBasic group)
finetrace --aggregation --device-timing ./samples/ze_gemm/build/ze_gemm
```

---

## Options Reference

### Host Tracing

| Option | Short | Description |
|---|---|---|
| `--call-logging` | `-c` | Print every host API call with arguments and return value |
| `--host-timing` | `-h` | Report total/average/min/max time per host API function |
| `--chrome-call-logging` | | Dump host API calls to a JSON file (`chrome://tracing`) |

**Example `--call-logging` output:**
```
>>>> [271632470] clCreateBuffer: context = 0x... flags = 4 size = 4194304 ...
<<<< [271640078] clCreateBuffer [7608 ns] result = 0x... -> CL_SUCCESS (0)
>>>> [272171119] clEnqueueWriteBuffer: commandQueue = 0x... blockingWrite = 1 ...
<<<< [272698660] clEnqueueWriteBuffer [527541 ns] -> CL_SUCCESS (0)
```

**Example `--host-timing` output:**
```
=== API Timing Results: ===

             Total Execution Time (ns):   372547856
    Total API Time for L0 backend (ns):   355680113

== L0 Backend: ==

                              Function,  Calls,       Time (ns),  Time (%),  Average (ns),  Min (ns),  Max (ns)
                zeEventHostSynchronize,     32,       181510841,     51.03,       5672213,       72,    45327080
                        zeModuleCreate,      1,        96564991,     27.15,      96564991, 96564991,    96564991
     zeCommandQueueExecuteCommandLists,      8,        76576727,     21.53,       9572090,    20752,    76024831
```

---

### Device Tracing

| Option | Short | Description |
|---|---|---|
| `--device-timeline` | `-t` | Print per-kernel timestamps: queued/submit/start/end |
| `--device-timing` | `-d` | Report total/average/min/max execution time per kernel |
| `--chrome-device-timeline` | | Dump per-command-queue activity to JSON |
| `--chrome-kernel-timeline` | | Dump per-kernel-name activity to JSON |

Memory transfer direction suffixes in Level Zero output:
- `M2D` / `D2M` — system memory (`malloc`) ↔ device
- `H2D` / `D2H` — USM host memory ↔ device
- `S` — USM shared memory

**Example `--device-timing` output:**
```
=== Device Timing Results: ===

                Total Execution Time (ns):            295236137
    Total Device Time for L0 backend (ns):            177147822

== L0 Backend: ==

                            Kernel,  Calls,    Time (ns),  Time (%),  Average (ns),  Min (ns),  Max (ns)
                              GEMM,      4,   172104499,     97.15,      43026124,  42814000,  43484166
zeCommandListAppendMemoryCopy(M2D),      8,     2934831,      1.66,        366853,    286500,    585333
zeCommandListAppendMemoryCopy(D2M),      4,     2099164,      1.18,        524791,    497666,    559666
        zeCommandListAppendBarrier,      8,        9328,      0.01,          1166,      1166,      1166
```

**Example `--device-timeline` output:**
```
Device Timeline (queue: 0x...): clEnqueueWriteBuffer [ns] = 317341082 (queued) 317355010 (submit) 317452332 (start) 317980165 (end)
Device Timeline (queue: 0x...): GEMM [ns] = 318185764 (queued) 318200629 (submit) 318550014 (start) 361260930 (end)
```

---

### Kernel Submission

| Option | Short | Description |
|---|---|---|
| `--kernel-submission` | `-s` | Report append/submit/execute intervals per kernel |
| `--chrome-device-stages` | | Dump per-kernel stage breakdown to JSON |

**Example `--kernel-submission` output:**
```
=== Kernel Submission Results: ===

                            Kernel,  Calls,  Append (ns),  Append (%),  Submit (ns),  Submit (%),  Execute (ns),  Execute (%)
                              GEMM,      4,      553087,       10.79,    12441082,        3.03,    169770832,       97.24
zeCommandListAppendMemoryCopy(M2D),      8,     2898413,       56.53,    20843165,        5.08,      2843832,        1.63
```

---

### Output Modifiers

| Option | Short | Description |
|---|---|---|
| `--verbose` | `-v` | Show SIMD width, group sizes, transfer sizes |
| `--demangle` | | Demangle DPC++ kernel names |
| `--kernels-per-tile` | | Report timing separately per GPU tile |
| `--tid` | | Include thread ID in host API trace |
| `--pid` | | Include process ID in output |

**Example `--device-timing --verbose` output:**
```
== CL GPU Backend: ==

                                  Kernel,  Calls,    Time (ns),  Time (%),  Average (ns),  Min (ns),  Max (ns)
GEMM[SIMD32, {1024, 1024, 1}, {0, 0, 0}],      4,   172101915,     96.93,      43025478, 42804333,  43375416
     clEnqueueWriteBuffer[4194304 bytes],      8,     3217914,      1.81,        402239,   277416,    483750
```

---

### General Options

| Option | Short | Description |
|---|---|---|
| `--output <file>` | `-o` | Redirect all console output to a file |
| `--conditional-collection` | | Collect only while `FTRACE_ENABLE_COLLECTION=1` is set |
| `--version` | | Print version |

**Conditional collection** — enables fine-grained control from within the application:
```cpp
setenv("FTRACE_ENABLE_COLLECTION", "1", 1);  // start collecting
// ... region of interest ...
unsetenv("FTRACE_ENABLE_COLLECTION");         // stop collecting
```

---

## GPU Hardware Metric Profiling

FineTrace integrates Level Zero metric APIs to collect GPU hardware performance counters.
All metric modes require a Level Zero GPU device and work alongside any tracing option.

### Collection Modes

| Option | Short | Description |
|---|---|---|
| `--aggregation` | `-a` | Per-kernel aggregated HW counters via time-based metric stream |
| `--kernel-query` | `-q` | Per-kernel aggregated HW counters via event-based query (no sampling) |
| `--kernel-metrics` | `-k` | Per-kernel raw metric samples aligned to kernel start/end timestamps |
| `--raw-metrics` | `-m` | Continuous raw metric stream for the entire run (no per-kernel split) |
| `--kernel-intervals` | `-i` | Raw kernel start/end timestamps only (no metric values) |

**`--aggregation`** is the recommended starting point. It uses a background metric stream and correlates samples to kernel execution intervals automatically.

**`--kernel-query`** is more precise (event-based, zero sampling overhead) but does not work with overlapping kernels or multi-engine workloads.

### Metric Groups

Use `--group` to select which counters to collect. Default is `ComputeBasic`.

```sh
finetrace --metric-list    # list all available groups and their metrics
finetrace --device-list    # list available devices
```

Key named groups on Intel Arc:

| Group | Focus |
|---|---|
| `ComputeBasic` | XveActive, FpuActive, GpuBusy, CsThreads, GTI throughput *(default)* |
| `XveActivity` | Detailed EU pipeline breakdown (EM, FPU, XMX) |
| `L3` | L3 cache hit rate, bandwidth, evictions |
| `SLMProfile` | Shared local memory throughput and utilization |
| `L1ProfileSlmBankConflicts` | SLM bank conflicts (critical for GEMM-like kernels) |
| `DataportReads` / `DataportWrites` | Load/store through the dataport |
| `GpuBusyness` | High-level GPU/render/compute occupancy |
| `LoadStoreCacheProfile` | L1/SLM/HDC load-store breakdown |

### Options

| Option | Short | Default | Description |
|---|---|---|---|
| `--group <NAME>` | `-g` | `ComputeBasic` | Metric group to collect |
| `--metric-device <ID>` | | `0` | Target device index |
| `--metric-sampling-interval <us>` | | `1000` | Sampling interval in microseconds (stream modes) |
| `--raw-data-path <DIR>` | `-p` | `.` | Directory for intermediate raw data files |
| `--no-finalize` | | | Save raw data only; skip post-processing |
| `--finalize <file>` | `-f` | | Post-process a previously saved `result.PID.bin` |

### Examples

```sh
# ComputeBasic metrics + device timing (recommended first run)
finetrace --aggregation --device-timing ./samples/ze_gemm/build/ze_gemm

# Event-based query (more precise, no sampling overhead)
finetrace --kernel-query --device-timing ./samples/ze_gemm/build/ze_gemm

# SLM bank conflicts (important for memory-bound kernels)
finetrace --kernel-query -g L1ProfileSlmBankConflicts ./samples/ze_gemm/build/ze_gemm

# L3 cache profile
finetrace --aggregation -g L3 --device-timing ./samples/ze_gemm/build/ze_gemm

# All tracing + metrics in one shot
finetrace \
  --host-timing --call-logging --device-timing \
  --aggregation -g ComputeBasic \
  ./samples/ze_gemm/build/ze_gemm

# Two-phase: collect now, finalize later
finetrace --aggregation --no-finalize ./samples/ze_gemm/build/ze_gemm
finetrace --finalize result.<PID>.bin
```

**Example `--aggregation` output:**
```
=== Profiling Results ===

Total Execution Time: 116065933 ns

== Aggregated Kernel Metrics ==

Kernel,SubDeviceId,KernelTime[ns],GpuTime[ns],GpuCoreClocks[cycles],AvgGpuCoreFrequencyMHz[MHz],GpuBusy[%],CsThreads[threads],XveActive[%],XveStall[%],FpuActive[%],...
GEMM[SIMD32 {1; 1024; 1} {1024; 1; 1}],0,32788229,32426637,28422798,881,100,23166,42.36,51.61,4.24,...
```

---

## Build

### Prerequisites

- [CMake](https://cmake.org/) 3.12+
- [Git](https://git-scm.com/) 1.8+
- [Python](https://www.python.org/) 2.7+
- [OpenCL™ ICD Loader](https://github.com/KhronosGroup/OpenCL-ICD-Loader)
- [oneAPI Level Zero loader](https://github.com/oneapi-src/level-zero)
- [Intel® Graphics Compute Runtime](https://github.com/intel/compute-runtime) (GPU)
- [Intel® CPU OpenCL Runtime](https://software.intel.com/en-us/articles/opencl-drivers#cpu-section) (CPU, optional)

### Supported OS

- Linux

### Build Steps

```sh
cd finetrace
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Optional: install to system
cmake --install build
```

---

## Overhead Benchmarking

The `script_run_samples.sh` script measures wall-clock overhead across multiple tracing configurations:

| Variant | Flags |
|---|---|
| `clean` | *(no finetrace)* |
| `host-timing` | `--host-timing` |
| `call-logging` | `--call-logging` |
| `host+call` | `--host-timing --call-logging` |
| `metrics` | `--aggregation` *(GPU only)* |
| `all` | `--host-timing --call-logging --aggregation` *(GPU only)* |

```sh
# Build samples first
./script_build_samples.sh

# Run overhead benchmarks (both CPU and GPU)
./script_run_samples.sh

# GPU only, 10 repeats
./script_run_samples.sh --gpu -n 10

# Generate Excel report with charts
python3 script_run_samples.py
```

The Python script produces `finetrace_overhead_stat.xlsx` with per-mode sheets, absolute time charts, and overhead-percentage charts relative to the `clean` baseline.
