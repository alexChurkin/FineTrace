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
  std::cout << "Options:" << std::endl;
  std::cout <<
    "--call-logging [-c]                   " <<
    "Trace host API calls" <<
    std::endl;
  std::cout <<
    "--host-timing  [-h]                   " <<
    "Report host API execution time" <<
    std::endl;
  std::cout <<
    "--chrome-call-logging                 " <<
    "Dump host API calls to JSON file" <<
    std::endl;
  std::cout << std::endl;
  std::cout <<
    "--device-timeline [-t]                " <<
    "Trace device activities" <<
    std::endl;
  std::cout <<
    "--device-timing [-d]                  " <<
    "Report kernels execution time" <<
    std::endl;
  std::cout <<
    "--chrome-device-timeline              " <<
    "Dump device activities to JSON file per command queue" <<
    std::endl;
  std::cout <<
    "--chrome-kernel-timeline              " <<
    "Dump device activities to JSON file per kernel name" <<
    std::endl;
  std::cout << std::endl;
  std::cout <<
    "--kernel-submission [-s]              " <<
    "Report append (queued), submit and execute intervals for kernels" <<
    std::endl;
  std::cout <<
    "--chrome-device-stages                " <<
    "Dump device activities by stages to JSON file" <<
    std::endl;
  std::cout << std::endl;
  std::cout <<
    "--verbose [-v]                        " <<
    "Enable verbose mode to show more kernel information" <<
    std::endl;
  std::cout <<
    "--demangle                            " <<
    "Demangle DPC++ kernel names" <<
    std::endl;
  std::cout <<
    "--kernels-per-tile                    " <<
    "Dump kernel information per tile" <<
    std::endl;
  std::cout <<
    "--tid                                 " <<
    "Print thread ID into host API trace" <<
    std::endl;
  std::cout <<
    "--pid                                 " <<
    "Print process ID into host API and device activity trace" <<
    std::endl;
  std::cout << std::endl;
  std::cout <<
    "--output [-o] <filename>              " <<
    "Print console logs into the file" <<
    std::endl;
  std::cout <<
    "--conditional-collection              " <<
    "Enable conditional collection mode" <<
    std::endl;
  std::cout <<
    "--version                             " <<
    "Print version" <<
    std::endl;
  std::cout << std::endl;
  std::cout << "Metric Profiling Options:" << std::endl;
  std::cout <<
    "--raw-metrics [-m]                    " <<
    "Collect raw metric stream for the device" <<
    std::endl;
  std::cout <<
    "--kernel-intervals [-i]               " <<
    "Collect raw kernel intervals for the device" <<
    std::endl;
  std::cout <<
    "--kernel-metrics [-k]                 " <<
    "Collect over-time metrics for each kernel instance" <<
    std::endl;
  std::cout <<
    "--aggregation [-a]                    " <<
    "Collect aggregated metrics for each kernel (time-based mode)" <<
    std::endl;
  std::cout <<
    "--kernel-query [-q]                   " <<
    "Collect aggregated metrics for each kernel (query-based mode)" <<
    std::endl;
  std::cout <<
    "--metric-device <ID>                  " <<
    "Target device for profiling (default is 0)" <<
    std::endl;
  std::cout <<
    "--group [-g] <NAME>                   " <<
    "Target metric group to collect (default is ComputeBasic)" <<
    std::endl;
  std::cout <<
    "--metric-sampling-interval <VALUE>    " <<
    "Sampling interval for metrics collection in us (default is 1000 us)" <<
    std::endl;
  std::cout <<
    "--raw-data-path [-p] <DIRECTORY>      " <<
    "Path to store raw metric data (default is current directory)" <<
    std::endl;
  std::cout <<
    "--finalize [-f] <FILENAME>            " <<
    "Print output from collected result file" <<
    std::endl;
  std::cout <<
    "--no-finalize                         " <<
    "Do not finalize and do not report collection results" <<
    std::endl;
  std::cout <<
    "--device-list                         " <<
    "Print list of available devices" <<
    std::endl;
  std::cout <<
    "--metric-list                         " <<
    "Print list of available metrics" <<
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

  if (IsMetricMode()) {
    ze_result_t status = zeInit(ZE_INIT_FLAG_GPU_ONLY);
    if (status == ZE_RESULT_ERROR_DEPENDENCY_UNAVAILABLE) {
      std::cerr <<
        "[WARNING] Unable to initialize Level Zero Metrics API" << std::endl;
      std::cerr << "  Please check that metrics libraries are installed " <<
        "and /proc/sys/dev/i915/perf_stream_paranoid is set to 0" << std::endl;
    } else if (status == ZE_RESULT_SUCCESS) {
      metric_profiler = MetricProfiler::Create(options);
    }
  }

  tracer = UnifiedTracer::Create(options);
}

void DisableProfiling() {
  if (tracer != nullptr) {
    delete tracer;
  }
  if (metric_profiler != nullptr) {
    delete metric_profiler;
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
