#include <iostream>

#include "metric_profiler.h"
#include "prof_utils.h"
#include "result_storage.h"
#include "unified_tracer.h"

static UnifiedTracer* tracer = nullptr;
static MetricProfiler* metric_profiler = nullptr;

void Finalize();

extern "C" FTRACE_EXPORT
void Usage() {
  std::cout <<
    "Usage: ./finetrace [options] <application> <args>" <<
    std::endl;

  std::cout << std::endl;
  std::cout << "Host Tracing Options:" << std::endl;
  std::cout <<
    "  --call-logging [-c]                 " <<
    "Print every host API call with arguments and return value" <<
    std::endl;
  std::cout <<
    "  --host-timing  [-h]                 " <<
    "Report total/average/min/max time per host API function" <<
    std::endl;
  std::cout <<
    "  --chrome-call-logging               " <<
    "Dump host API calls to a JSON trace file (chrome://tracing)" <<
    std::endl;

  std::cout << std::endl;
  std::cout << "Device Tracing Options:" << std::endl;
  std::cout <<
    "  --device-timeline [-t]              " <<
    "Print per-kernel timestamps: queued/submit/start/end" <<
    std::endl;
  std::cout <<
    "  --device-timing [-d]                " <<
    "Report total/average/min/max execution time per kernel" <<
    std::endl;
  std::cout <<
    "  --chrome-device-timeline            " <<
    "Dump device activities per command queue to JSON" <<
    std::endl;
  std::cout <<
    "  --chrome-kernel-timeline            " <<
    "Dump device activities per kernel name to JSON" <<
    std::endl;

  std::cout << std::endl;
  std::cout << "Kernel Submission Options:" << std::endl;
  std::cout <<
    "  --kernel-submission [-s]            " <<
    "Report append/submit/execute intervals per kernel" <<
    std::endl;
  std::cout <<
    "  --chrome-device-stages              " <<
    "Dump per-kernel stage breakdown (append/submit/execute) to JSON" <<
    std::endl;

  std::cout << std::endl;
  std::cout << "Output Modifiers:" << std::endl;
  std::cout <<
    "  --verbose [-v]                      " <<
    "Show extended kernel info (SIMD width, group size, transfer size)" <<
    std::endl;
  std::cout <<
    "  --demangle                          " <<
    "Demangle DPC++ kernel names" <<
    std::endl;
  std::cout <<
    "  --kernels-per-tile                  " <<
    "Report timing separately for each GPU tile" <<
    std::endl;
  std::cout <<
    "  --tid                               " <<
    "Include thread ID in host API trace output" <<
    std::endl;
  std::cout <<
    "  --pid                               " <<
    "Include process ID in host API and device activity output" <<
    std::endl;

  std::cout << std::endl;
  std::cout << "General Options:" << std::endl;
  std::cout <<
    "  --output [-o] <filename>            " <<
    "Redirect all console output to a file" <<
    std::endl;
  std::cout <<
    "  --conditional-collection            " <<
    "Enable collection only when FTRACE_ENABLE_COLLECTION=1 is set" <<
    std::endl;
  std::cout <<
    "  --version                           " <<
    "Print version" <<
    std::endl;

  std::cout << std::endl;
  std::cout << "Metric Hardware Profiling Options (Level Zero GPU only):" << std::endl;
  std::cout << std::endl;
  std::cout << "  Collection modes (choose one):" << std::endl;
  std::cout <<
    "  --aggregation [-a]                  " <<
    "Per-kernel aggregated HW counters via time-based metric stream" <<
    std::endl;
  std::cout <<
    "  --kernel-query [-q]                 " <<
    "Per-kernel aggregated HW counters via event-based query (no sampling)" <<
    std::endl;
  std::cout <<
    "  --kernel-metrics [-k]               " <<
    "Per-kernel raw metric samples aligned to kernel intervals" <<
    std::endl;
  std::cout <<
    "  --raw-metrics [-m]                  " <<
    "Continuous raw metric stream for the entire run (no per-kernel split)" <<
    std::endl;
  std::cout <<
    "  --kernel-intervals [-i]             " <<
    "Raw kernel start/end timestamps only (no metric values)" <<
    std::endl;
  std::cout << std::endl;
  std::cout << "  Metric group and device:" << std::endl;
  std::cout <<
    "  --group [-g] <NAME>                 " <<
    "Metric group to collect (default: ComputeBasic; see --metric-list)" <<
    std::endl;
  std::cout <<
    "  --metric-device <ID>                " <<
    "Target device index (default: 0; see --device-list)" <<
    std::endl;
  std::cout <<
    "  --metric-sampling-interval <VALUE>  " <<
    "Sampling interval in us for stream modes (default: 1000 us)" <<
    std::endl;
  std::cout << std::endl;
  std::cout << "  Data path and finalization:" << std::endl;
  std::cout <<
    "  --raw-data-path [-p] <DIRECTORY>    " <<
    "Directory for intermediate raw data files (default: current dir)" <<
    std::endl;
  std::cout <<
    "  --no-finalize                       " <<
    "Save raw data only; skip post-processing and result output" <<
    std::endl;
  std::cout <<
    "  --finalize [-f] <result.PID.bin>    " <<
    "Post-process a previously saved result file and print metrics" <<
    std::endl;
  std::cout << std::endl;
  std::cout << "  Discovery:" << std::endl;
  std::cout <<
    "  --device-list                       " <<
    "Print available Level Zero devices and exit" <<
    std::endl;
  std::cout <<
    "  --metric-list                       " <<
    "Print available metric groups and their metrics, then exit" <<
    std::endl;
}

extern "C" FTRACE_EXPORT
int ParseArgs(int argc, char* argv[]) {
  bool metric_list = false;
  bool finalization = false;

  int app_index = 1;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--call-logging") == 0 ||
        strcmp(argv[i], "-c") == 0) {
      utils::SetEnv("FINETRACE_CallLogging", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--host-timing") == 0 ||
               strcmp(argv[i], "-h") == 0) {
      utils::SetEnv("FINETRACE_HostTiming", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--device-timing") == 0 ||
               strcmp(argv[i], "-d") == 0) {
      utils::SetEnv("FINETRACE_DeviceTiming", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--kernel-submission") == 0 ||
               strcmp(argv[i], "-s") == 0) {
      utils::SetEnv("FINETRACE_KernelSubmission", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--device-timeline") == 0 ||
               strcmp(argv[i], "-t") == 0) {
      utils::SetEnv("FINETRACE_DeviceTimeline", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--chrome-call-logging") == 0) {
      utils::SetEnv("FINETRACE_ChromeCallLogging", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--chrome-device-timeline") == 0) {
      utils::SetEnv("FINETRACE_ChromeDeviceTimeline", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--chrome-kernel-timeline") == 0) {
      utils::SetEnv("FINETRACE_ChromeKernelTimeline", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--chrome-device-stages") == 0) {
      utils::SetEnv("FINETRACE_ChromeDeviceStages", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--verbose") == 0 ||
               strcmp(argv[i], "-v") == 0) {
      utils::SetEnv("FINETRACE_Verbose", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--demangle") == 0) {
      utils::SetEnv("FINETRACE_Demangle", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--kernels-per-tile") == 0) {
      utils::SetEnv("FINETRACE_KernelsPerTile", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--tid") == 0) {
      utils::SetEnv("FINETRACE_Tid", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--pid") == 0) {
      utils::SetEnv("FINETRACE_Pid", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--output") == 0 ||
               strcmp(argv[i], "-o") == 0) {
      utils::SetEnv("FINETRACE_LogToFile", "1");
      ++i;
      if (i >= argc) {
        std::cerr << "[ERROR] Log file name is not specified" << std::endl;
        return -1;
      }
      utils::SetEnv("FINETRACE_LogFilename", argv[i]);
      app_index += 2;
    } else if (strcmp(argv[i], "--conditional-collection") == 0) {
      utils::SetEnv("FINETRACE_ConditionalCollection", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--version") == 0) {
#ifdef FTRACE_VERSION
      std::cout << TOSTRING(FTRACE_VERSION) << std::endl;
#endif
      return 0;
    } else if (strcmp(argv[i], "--raw-metrics") == 0 ||
               strcmp(argv[i], "-m") == 0) {
      utils::SetEnv("FINETRACE_RawMetrics", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--kernel-intervals") == 0 ||
               strcmp(argv[i], "-i") == 0) {
      utils::SetEnv("FINETRACE_MetricKernelIntervals", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--kernel-metrics") == 0 ||
               strcmp(argv[i], "-k") == 0) {
      utils::SetEnv("FINETRACE_KernelMetrics", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--aggregation") == 0 ||
               strcmp(argv[i], "-a") == 0) {
      utils::SetEnv("FINETRACE_Aggregation", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--kernel-query") == 0 ||
               strcmp(argv[i], "-q") == 0) {
      utils::SetEnv("FINETRACE_KernelQuery", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--metric-device") == 0) {
      ++i;
      if (i >= argc) {
        std::cerr << "[ERROR] Device ID is not specified" << std::endl;
        return -1;
      }
      utils::SetEnv("FINETRACE_MetricDeviceId", argv[i]);
      app_index += 2;
    } else if (strcmp(argv[i], "--group") == 0 ||
               strcmp(argv[i], "-g") == 0) {
      ++i;
      if (i >= argc) {
        std::cerr << "[ERROR] Metric group is not specified" << std::endl;
        return -1;
      }
      utils::SetEnv("FINETRACE_MetricGroup", argv[i]);
      app_index += 2;
    } else if (strcmp(argv[i], "--metric-sampling-interval") == 0) {
      ++i;
      if (i >= argc) {
        std::cerr << "[ERROR] Sampling interval is not specified" << std::endl;
        return -1;
      }
      utils::SetEnv("FINETRACE_MetricSamplingInterval", argv[i]);
      app_index += 2;
    } else if (strcmp(argv[i], "--raw-data-path") == 0 ||
               strcmp(argv[i], "-p") == 0) {
      ++i;
      if (i >= argc) {
        std::cerr << "[ERROR] Raw data path is not specified" << std::endl;
        return -1;
      }
      utils::SetEnv("FINETRACE_RawDataPath", argv[i]);
      app_index += 2;
    } else if (strcmp(argv[i], "--finalize") == 0 ||
               strcmp(argv[i], "-f") == 0) {
      ++i;
      if (i >= argc) {
        std::cerr << "[ERROR] Result file name is not specified" << std::endl;
        return -1;
      }
      utils::SetEnv("FINETRACE_MetricResultFile", argv[i]);
      app_index += 2;
      finalization = true;
    } else if (strcmp(argv[i], "--no-finalize") == 0) {
      utils::SetEnv("FINETRACE_NoFinalize", "1");
      ++app_index;
    } else if (strcmp(argv[i], "--device-list") == 0) {
      PrintDeviceList();
      return 0;
    } else if (strcmp(argv[i], "--metric-list") == 0) {
      metric_list = true;
      ++app_index;
    } else {
      break;
    }
  }

  if (utils::GetEnv("FINETRACE_ChromeDeviceTimeline") == "1" &&
      utils::GetEnv("FINETRACE_ChromeDeviceStages") == "1") {
    std::cerr <<
      "[ERROR] Options --chrome-device-timeline and " <<
      "--chrome-device-stages can't be used together, " <<
      "choose one of them" << std::endl;
    return -1;
  }
  if (utils::GetEnv("FINETRACE_ChromeDeviceTimeline") == "1" &&
      utils::GetEnv("FINETRACE_ChromeKernelTimeline") == "1") {
    std::cerr <<
      "[ERROR] Options --chrome-device-timeline and " <<
      "--chrome-kernel-timeline can't be used together, " <<
      "choose one of them" << std::endl;
    return -1;
  }

  if (utils::GetEnv("FINETRACE_KernelQuery") == "1") {
    if (utils::GetEnv("FINETRACE_RawMetrics") == "1" ||
        utils::GetEnv("FINETRACE_MetricKernelIntervals") == "1" ||
        utils::GetEnv("FINETRACE_KernelMetrics") == "1" ||
        utils::GetEnv("FINETRACE_Aggregation") == "1") {
      std::cerr << "[ERROR] --kernel-query cannot be combined with other metric modes" <<
        std::endl;
      return -1;
    }
  }

  if (finalization) {
    Finalize();
    return 0;
  }

  if (metric_list) {
    std::string value = utils::GetEnv("FINETRACE_MetricDeviceId");
    uint32_t device_id = value.empty() ? 0 : std::stoul(value);
    PrintMetricList(device_id);
    return 0;
  }

  return app_index;
}

extern "C" FTRACE_EXPORT
void SetToolEnv() {
  utils::SetEnv("ZE_ENABLE_TRACING_LAYER", "1");
  utils::SetEnv("ZET_ENABLE_METRICS", "1");
  utils::SetEnv("ZES_ENABLE_SYSMAN", "1");
}

static bool IsMetricMode() {
  return utils::GetEnv("FINETRACE_RawMetrics") == "1" ||
         utils::GetEnv("FINETRACE_MetricKernelIntervals") == "1" ||
         utils::GetEnv("FINETRACE_KernelMetrics") == "1" ||
         utils::GetEnv("FINETRACE_Aggregation") == "1" ||
         utils::GetEnv("FINETRACE_KernelQuery") == "1";
}

static TraceOptions ReadArgs() {
  std::string value;
  uint64_t flags = 0;
  std::string log_file;

  value = utils::GetEnv("FINETRACE_CallLogging");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_CALL_LOGGING);
  }

  value = utils::GetEnv("FINETRACE_HostTiming");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_HOST_TIMING);
  }

  value = utils::GetEnv("FINETRACE_DeviceTiming");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_DEVICE_TIMING);
  }

  value = utils::GetEnv("FINETRACE_KernelSubmission");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_KERNEL_SUBMITTING);
  }

  value = utils::GetEnv("FINETRACE_DeviceTimeline");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_DEVICE_TIMELINE);
  }

  value = utils::GetEnv("FINETRACE_ChromeCallLogging");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_CHROME_CALL_LOGGING);
  }

  value = utils::GetEnv("FINETRACE_ChromeDeviceTimeline");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_CHROME_DEVICE_TIMELINE);
  }

  value = utils::GetEnv("FINETRACE_ChromeKernelTimeline");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_CHROME_KERNEL_TIMELINE);
  }

  value = utils::GetEnv("FINETRACE_ChromeDeviceStages");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_CHROME_DEVICE_STAGES);
  }

  value = utils::GetEnv("FINETRACE_Verbose");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_VERBOSE);
  }

  value = utils::GetEnv("FINETRACE_Demangle");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_DEMANGLE);
  }

  value = utils::GetEnv("FINETRACE_KernelsPerTile");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_KERNELS_PER_TILE);
  }

  value = utils::GetEnv("FINETRACE_Tid");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_TID);
  }

  value = utils::GetEnv("FINETRACE_Pid");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_PID);
  }

  value = utils::GetEnv("FINETRACE_LogToFile");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_LOG_TO_FILE);
    log_file = utils::GetEnv("FINETRACE_LogFilename");
    FTRACE_ASSERT(!log_file.empty());
  }

  value = utils::GetEnv("FINETRACE_ConditionalCollection");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_CONDITIONAL_COLLECTION);
  }

  value = utils::GetEnv("FINETRACE_RawMetrics");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_RAW_METRICS);
  }

  value = utils::GetEnv("FINETRACE_MetricKernelIntervals");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_KERNEL_INTERVALS);
  }

  value = utils::GetEnv("FINETRACE_KernelMetrics");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_KERNEL_METRICS);
  }

  value = utils::GetEnv("FINETRACE_Aggregation");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_AGGREGATION);
  }

  value = utils::GetEnv("FINETRACE_KernelQuery");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_METRIC_QUERY);
  }

  value = utils::GetEnv("FINETRACE_NoFinalize");
  if (!value.empty() && value == "1") {
    flags |= (1ULL << TRACE_NO_FINALIZE);
  }

  TraceOptions options(flags, log_file);

  value = utils::GetEnv("FINETRACE_MetricGroup");
  if (!value.empty()) {
    options.SetMetricGroup(value);
  }

  value = utils::GetEnv("FINETRACE_MetricSamplingInterval");
  if (!value.empty()) {
    uint32_t interval_us = std::stoul(value);
    options.SetSamplingInterval(interval_us * 1000);
  }

  value = utils::GetEnv("FINETRACE_RawDataPath");
  if (!value.empty()) {
    options.SetRawDataPath(value);
  }

  value = utils::GetEnv("FINETRACE_MetricDeviceId");
  if (!value.empty()) {
    options.SetDeviceId(std::stoul(value));
  }

  value = utils::GetEnv("FINETRACE_MetricResultFile");
  if (!value.empty()) {
    options.SetResultFile(value);
  } else if (IsMetricMode()) {
    options.SetResultFile(ResultStorage::GetResultFileName(
        options.GetRawDataPath(), utils::GetPid()));
  }

  return options;
}

void EnableProfiling() {
  TraceOptions options = ReadArgs();

  tracer = UnifiedTracer::Create(options);

  if (IsMetricMode()) {
    ze_result_t status = zeInit(ZE_INIT_FLAG_GPU_ONLY);
    if (status == ZE_RESULT_ERROR_DEPENDENCY_UNAVAILABLE) {
      std::cerr <<
        "[WARNING] Unable to initialize Level Zero Metrics API" << std::endl;
      std::cerr << "  Please check that metrics libraries are installed " <<
        "and /proc/sys/dev/i915/perf_stream_paranoid is set to 0" << std::endl;
    } else if (status == ZE_RESULT_SUCCESS) {
      ClKernelCollector* shared_cl = (tracer != nullptr)
          ? tracer->GetClGpuKernelCollector()
          : nullptr;
      metric_profiler = MetricProfiler::Create(options, shared_cl);
    }
  }
}

void DisableProfiling() {
  // MetricProfiler first: its DumpResultFile may read from a shared
  // ClKernelCollector owned by UnifiedTracer, so UnifiedTracer must
  // still be alive when MetricProfiler destructs.
  if (metric_profiler != nullptr) {
    delete metric_profiler;
  }
  if (tracer != nullptr) {
    delete tracer;
  }
}

void Finalize() {
  utils::SetEnv("ZET_ENABLE_METRICS", "1");

  ze_result_t status = zeInit(ZE_INIT_FLAG_GPU_ONLY);
  if (status == ZE_RESULT_ERROR_DEPENDENCY_UNAVAILABLE) {
    std::cerr <<
      "[WARNING] Unable to initialize Level Zero Metrics API" << std::endl;
    std::cerr << "  Please check that metrics libraries are installed " <<
      "and /proc/sys/dev/i915/perf_stream_paranoid is set to 0" << std::endl;
    return;
  }
  FTRACE_ASSERT(status == ZE_RESULT_SUCCESS);

  TraceOptions options = ReadArgs();
  MetricFinalizer* finalizer = MetricFinalizer::Create(options);
  if (finalizer != nullptr) {
    finalizer->Report();
    delete finalizer;
  }
}
