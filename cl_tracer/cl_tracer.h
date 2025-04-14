#ifndef FTRACE_TOOLS_CL_TRACER_CL_TRACER_H_
#define FTRACE_TOOLS_CL_TRACER_CL_TRACER_H_

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>

#include "cl_ext_collector.h"
#include "cl_ext_callbacks.h"
#include "cl_api_collector.h"
#include "cl_api_callbacks.h"
#include "cl_kernel_collector.h"
#include "trace_options.h"
#include "utils.h"

const char* kChromeTraceFileName = "clt_trace";

class ClTracer {
 public:
  static ClTracer* Create(const TraceOptions& options) {
    cl_device_id cpu_device = utils::cl::GetIntelDevice(CL_DEVICE_TYPE_CPU);
    cl_device_id gpu_device = utils::cl::GetIntelDevice(CL_DEVICE_TYPE_GPU);
    if (cpu_device == nullptr && gpu_device == nullptr) {
      std::cerr << "[WARNING] Intel OpenCL devices are not found" << std::endl;
      return nullptr;
    }

    ClTracer* tracer = new ClTracer(options);
    FTRACE_ASSERT(tracer != nullptr);

    if (tracer->CheckOption(TRACE_DEVICE_TIMING) ||
        tracer->CheckOption(TRACE_KERNEL_SUBMITTING) ||
        tracer->CheckOption(TRACE_DEVICE_TIMELINE) ||
        tracer->CheckOption(TRACE_CHROME_DEVICE_TIMELINE) ||
        tracer->CheckOption(TRACE_CHROME_KERNEL_TIMELINE) ||
        tracer->CheckOption(TRACE_CHROME_DEVICE_STAGES)) {

      FTRACE_ASSERT(!(tracer->CheckOption(TRACE_CHROME_DEVICE_TIMELINE) &&
                   tracer->CheckOption(TRACE_CHROME_DEVICE_STAGES)));
      FTRACE_ASSERT(!(tracer->CheckOption(TRACE_CHROME_DEVICE_TIMELINE) &&
                   tracer->CheckOption(TRACE_CHROME_KERNEL_TIMELINE)));

      ClKernelCollector* cpu_kernel_collector = nullptr;
      ClKernelCollector* gpu_kernel_collector = nullptr;

      OnClKernelFinishCallback callback = nullptr;
      if (tracer->CheckOption(TRACE_DEVICE_TIMELINE) &&
          tracer->CheckOption(TRACE_CHROME_KERNEL_TIMELINE) &&
          tracer->CheckOption(TRACE_CHROME_DEVICE_STAGES)) {
        callback = DeviceAndChromeKernelStagesCallback;
      } else if (tracer->CheckOption(TRACE_DEVICE_TIMELINE) &&
                 tracer->CheckOption(TRACE_CHROME_DEVICE_TIMELINE)) {
        callback = DeviceAndChromeDeviceCallback;
      } else if (tracer->CheckOption(TRACE_DEVICE_TIMELINE) &&
                 tracer->CheckOption(TRACE_CHROME_KERNEL_TIMELINE)) {
        callback = DeviceAndChromeKernelCallback;
      } else if (tracer->CheckOption(TRACE_DEVICE_TIMELINE) &&
                 tracer->CheckOption(TRACE_CHROME_DEVICE_STAGES)) {
        callback = DeviceAndChromeStagesCallback;
      } else if (tracer->CheckOption(TRACE_CHROME_KERNEL_TIMELINE) &&
                 tracer->CheckOption(TRACE_CHROME_DEVICE_STAGES)) {
        callback = ChromeKernelStagesCallback;
      } else if (tracer->CheckOption(TRACE_DEVICE_TIMELINE)) {
        callback = DeviceTimelineCallback;
      } else if (tracer->CheckOption(TRACE_CHROME_DEVICE_TIMELINE)) {
        callback = ChromeDeviceCallback;
      } else if (tracer->CheckOption(TRACE_CHROME_KERNEL_TIMELINE)) {
        callback = ChromeKernelCallback;
      } else if (tracer->CheckOption(TRACE_CHROME_DEVICE_STAGES)) {
        callback = ChromeStagesCallback;
      }

      KernelCollectorOptions kernel_options;
      kernel_options.verbose = tracer->CheckOption(TRACE_VERBOSE);
      kernel_options.demangle = tracer->CheckOption(TRACE_DEMANGLE);

      if (cpu_device != nullptr) {
        cpu_kernel_collector = ClKernelCollector::Create(
            cpu_device, &tracer->correlator_,
            kernel_options, callback, tracer);
        if (cpu_kernel_collector == nullptr) {
          std::cerr <<
            "[WARNING] Unable to create kernel collector for CPU backend" <<
            std::endl;
        }
        tracer->cpu_kernel_collector_ = cpu_kernel_collector;
      }

      if (gpu_device != nullptr) {
        gpu_kernel_collector = ClKernelCollector::Create(
            gpu_device, &tracer->correlator_,
            kernel_options, callback, tracer);
        if (gpu_kernel_collector == nullptr) {
          std::cerr <<
            "[WARNING] Unable to create kernel collector for GPU backend" <<
            std::endl;
        }
        tracer->gpu_kernel_collector_ = gpu_kernel_collector;
      }

      if (cpu_kernel_collector == nullptr && gpu_kernel_collector == nullptr) {
        delete tracer;
        return nullptr;
      }
    }

    if (tracer->CheckOption(TRACE_CALL_LOGGING) ||
        tracer->CheckOption(TRACE_CHROME_CALL_LOGGING) ||
        tracer->CheckOption(TRACE_HOST_TIMING)) {

      ClApiCollector* cpu_api_collector = nullptr;
      ClApiCollector* gpu_api_collector = nullptr;

      OnClFunctionFinishCallback callback = nullptr;
      if (tracer->CheckOption(TRACE_CHROME_CALL_LOGGING)) {
        callback = ChromeLoggingCallback;
      }

      ApiCollectorOptions api_options;
      api_options.call_tracing = tracer->CheckOption(TRACE_CALL_LOGGING);
      api_options.need_tid = tracer->CheckOption(TRACE_TID);
      api_options.need_pid = tracer->CheckOption(TRACE_PID);
      api_options.demangle = tracer->CheckOption(TRACE_DEMANGLE);

      if (cpu_device != nullptr) {
        cpu_api_collector = ClApiCollector::Create(
            cpu_device, &tracer->correlator_,
            api_options, callback, tracer);
        if (cpu_api_collector == nullptr) {
          std::cerr <<
            "[WARNING] Unable to create API collector for CPU backend" <<
            std::endl;
        }
        tracer->cpu_api_collector_ = cpu_api_collector;
      }

      if (gpu_device != nullptr) {
        gpu_api_collector = ClApiCollector::Create(
            gpu_device, &tracer->correlator_,
            api_options, callback, tracer);
        if (gpu_api_collector == nullptr) {
          std::cerr <<
            "[WARNING] Unable to create API collector for GPU backend" <<
            std::endl;
        }
        tracer->gpu_api_collector_ = gpu_api_collector;
      }

      if (gpu_api_collector == nullptr && cpu_api_collector == nullptr) {
        delete tracer;
        return nullptr;
      }

      ClExtCollector::Create(cpu_api_collector, gpu_api_collector);
    }

    return tracer;
  }

  ~ClTracer() {
    total_execution_time_ = correlator_.GetTimestamp();

    if (cpu_api_collector_ != nullptr) {
      cpu_api_collector_->DisableTracing();
    }
    if (gpu_api_collector_ != nullptr) {
      gpu_api_collector_->DisableTracing();
    }

    if (cpu_kernel_collector_ != nullptr) {
      cpu_kernel_collector_->DisableTracing();
    }
    if (gpu_kernel_collector_ != nullptr) {
      gpu_kernel_collector_->DisableTracing();
    }

    Report();

    if (cpu_api_collector_ != nullptr) {
      delete cpu_api_collector_;
    }
    if (gpu_api_collector_ != nullptr) {
      delete gpu_api_collector_;
    }

    if (cpu_kernel_collector_ != nullptr) {
      delete cpu_kernel_collector_;
    }
    if (gpu_kernel_collector_ != nullptr) {
      delete gpu_kernel_collector_;
    }

    ClExtCollector::Destroy();

    if (CheckOption(TRACE_LOG_TO_FILE)) {
      std::cerr << "[INFO] Log was stored to " <<
        options_.GetLogFileName() << std::endl;
    }

    if (chrome_logger_ != nullptr) {
      delete chrome_logger_;
      std::cerr << "[INFO] Timeline was stored to " <<
        chrome_trace_file_name_ << std::endl;
    }
  }

  bool CheckOption(unsigned option) {
    return options_.CheckFlag(option);
  }

  ClTracer(const ClTracer& copy) = delete;
  ClTracer& operator=(const ClTracer& copy) = delete;

 private:
  ClTracer(const TraceOptions& options)
      : options_(options),
        correlator_(options.GetLogFileName(),
          CheckOption(TRACE_CONDITIONAL_COLLECTION)) {
#if !defined(_WIN32)
    uint64_t monotonic_time = utils::GetTime(CLOCK_MONOTONIC);
    uint64_t real_time = utils::GetTime(CLOCK_REALTIME);
#endif

    if (CheckOption(TRACE_CHROME_CALL_LOGGING) ||
        CheckOption(TRACE_CHROME_DEVICE_TIMELINE) ||
        CheckOption(TRACE_CHROME_KERNEL_TIMELINE) ||
        CheckOption(TRACE_CHROME_DEVICE_STAGES)) {
      chrome_trace_file_name_ =
        TraceOptions::GetChromeTraceFileName(kChromeTraceFileName);
      chrome_logger_ = new Logger(chrome_trace_file_name_.c_str());
      FTRACE_ASSERT(chrome_logger_ != nullptr);

      std::stringstream stream;
      stream << "[" << std::endl;
      stream << "{\"ph\":\"M\", \"name\":\"process_name\", \"pid\":\"" <<
        utils::GetPid() << "\", \"args\":{\"name\":\"" <<
        utils::GetExecutableName() << "\"}}," << std::endl;

      stream << "{\"ph\":\"M\", \"name\":\"start_time\", \"pid\":\"" <<
        utils::GetPid() << "\", \"args\":{";
#if defined(_WIN32)
      stream << "\"QueryPerformanceCounter\":\"" <<
        correlator_.GetStartPoint() << "\"";
#else
      stream << "\"CLOCK_MONOTONIC_RAW\":\"" <<
        correlator_.GetStartPoint() << "\", ";
      stream << "\"CLOCK_MONOTONIC\":\"" <<
        monotonic_time << "\", ";
      stream << "\"CLOCK_REALTIME\":\"" <<
        real_time << "\"";
#endif
      stream << "}}," << std::endl;

      chrome_logger_->Log(stream.str());
    }
    if (CheckOption(TRACE_DEVICE_TIMELINE)) {
      std::stringstream stream;
#if defined(_WIN32)
      stream <<
        "Device Timeline: start time (QueryPerformanceCounter) [ns] = " <<
        correlator_.GetStartPoint() << std::endl;
#else
      stream <<
        "Device Timeline: start time (CLOCK_MONOTONIC_RAW) [ns] = " <<
        correlator_.GetStartPoint() << std::endl;
      stream <<
        "Device Timeline: start time (CLOCK_MONOTONIC) [ns] = " <<
        monotonic_time << std::endl;
      stream <<
        "Device Timeline: start time (CLOCK_REALTIME) [ns] = " <<
        real_time << std::endl;
#endif
      correlator_.Log(stream.str());
    }
  }

  static uint64_t CalculateTotalTime(const ClApiCollector* collector) {
    FTRACE_ASSERT(collector != nullptr);
    uint64_t total_time = 0;

    const ClFunctionInfoMap& function_info_map = collector->GetFunctionInfoMap();
    if (function_info_map.size() != 0) {
      for (auto& value : function_info_map) {
        total_time += value.second.total_time;
      }
    }

    return total_time;
  }

  static uint64_t CalculateTotalTime(const ClKernelCollector* collector) {
    FTRACE_ASSERT(collector != nullptr);
    uint64_t total_time = 0;

    const ClKernelInfoMap& kernel_info_map = collector->GetKernelInfoMap();
    if (kernel_info_map.size() != 0) {
      for (auto& value : kernel_info_map) {
        total_time += value.second.execute_time;
      }
    }

    return total_time;
  }

  void PrintBackendTable(
      const ClApiCollector* collector, const char* device_type) {
    FTRACE_ASSERT(collector != nullptr);
    FTRACE_ASSERT(device_type != nullptr);

    uint64_t total_duration = CalculateTotalTime(collector);
    if (total_duration > 0) {
      std::stringstream stream;
      stream << std::endl;
      stream << "== " << device_type << " Backend: ==" << std::endl;
      stream << std::endl;
      correlator_.Log(stream.str());
      collector->PrintFunctionsTable();
    }
  }

  void PrintBackendTable(
      const ClKernelCollector* collector, const char* device_type) {
    FTRACE_ASSERT(collector != nullptr);
    FTRACE_ASSERT(device_type != nullptr);

    uint64_t total_duration = CalculateTotalTime(collector);
    if (total_duration > 0) {
      std::stringstream stream;
      stream << std::endl;
      stream << "== " << device_type << " Backend: ==" << std::endl;
      stream << std::endl;
      correlator_.Log(stream.str());
      collector->PrintKernelsTable();
    }
  }

  void PrintSubmissionTable(
      const ClKernelCollector* collector, const char* device_type) {
    FTRACE_ASSERT(collector != nullptr);
    FTRACE_ASSERT(device_type != nullptr);

    uint64_t total_duration = CalculateTotalTime(collector);
    if (total_duration > 0) {
      std::stringstream stream;
      stream << std::endl;
      stream << "== " << device_type << " Backend: ==" << std::endl;
      stream << std::endl;
      correlator_.Log(stream.str());
      collector->PrintSubmissionTable();
    }
  }

  template <class Collector>
  void ReportTiming(
      const Collector* cpu_collector,
      const Collector* gpu_collector,
      const char* type) {
    FTRACE_ASSERT (cpu_collector != nullptr || gpu_collector != nullptr);

    std::string cpu_title =
      std::string("Total ") + std::string(type) +
      " Time for CPU backend (ns): ";
    std::string gpu_title =
      std::string("Total ") + std::string(type) +
      " Time for GPU backend (ns): ";
    size_t title_width = (std::max)(cpu_title.size(), gpu_title.size());
    const size_t time_width = 20;

    std::stringstream stream;
    stream << std::endl;
    stream << "=== " << type << " Timing Results: ===" << std::endl;
    stream << std::endl;
    stream << std::setw(title_width) << "Total Execution Time (ns): " <<
      std::setw(time_width) << total_execution_time_ << std::endl;

    if (cpu_collector != nullptr) {
      stream << std::setw(title_width) << cpu_title <<
        std::setw(time_width) << CalculateTotalTime(cpu_collector) <<
        std::endl;
    }
    if (gpu_collector != nullptr) {
      stream << std::setw(title_width) << gpu_title <<
        std::setw(time_width) << CalculateTotalTime(gpu_collector) <<
        std::endl;
    }

    correlator_.Log(stream.str());

    if (cpu_collector != nullptr) {
      PrintBackendTable(cpu_collector, "CPU");
    }
    if (gpu_collector != nullptr) {
      PrintBackendTable(gpu_collector, "GPU");
    }

    correlator_.Log("\n");
  }

  void ReportKernelSubmission(
      const ClKernelCollector* cpu_collector,
      const ClKernelCollector* gpu_collector,
      const char* type) {
    FTRACE_ASSERT (cpu_collector != nullptr || gpu_collector != nullptr);

    std::string cpu_title =
      std::string("Total ") + std::string(type) +
      " Time for CPU backend (ns): ";
    std::string gpu_title =
      std::string("Total ") + std::string(type) +
      " Time for GPU backend (ns): ";
    size_t title_width = (std::max)(cpu_title.size(), gpu_title.size());
    const size_t time_width = 20;

    std::stringstream stream;
    stream << std::endl;
    stream << "=== Kernel Submission Results: ===" << std::endl;
    stream << std::endl;
    stream << std::setw(title_width) << "Total Execution Time (ns): " <<
      std::setw(time_width) << total_execution_time_ << std::endl;

    if (cpu_collector != nullptr) {
      stream << std::setw(title_width) << cpu_title <<
        std::setw(time_width) << CalculateTotalTime(cpu_collector) <<
        std::endl;
    }
    if (gpu_collector != nullptr) {
      stream << std::setw(title_width) << gpu_title <<
        std::setw(time_width) << CalculateTotalTime(gpu_collector) <<
        std::endl;
    }

    correlator_.Log(stream.str());

    if (cpu_collector != nullptr) {
      PrintSubmissionTable(cpu_collector, "CPU");
    }
    if (gpu_collector != nullptr) {
      PrintSubmissionTable(gpu_collector, "GPU");
    }

    correlator_.Log("\n");
  }

  void Report() {
    if (CheckOption(TRACE_HOST_TIMING)) {
      ReportTiming(cpu_api_collector_, gpu_api_collector_, "API");
    }
    if (CheckOption(TRACE_DEVICE_TIMING)) {
      ReportTiming(cpu_kernel_collector_, gpu_kernel_collector_, "Device");
    }
    if (CheckOption(TRACE_KERNEL_SUBMITTING)) {
      ReportKernelSubmission(
          cpu_kernel_collector_, gpu_kernel_collector_, "Device");
    }
    correlator_.Log("\n");
  }

  static void DeviceTimelineCallback(
      void* data,
      const std::string& queue,
      const std::string& id,
      const std::string& name,
      uint64_t queued,
      uint64_t submitted,
      uint64_t started,
      uint64_t ended) {
    ClTracer* tracer = reinterpret_cast<ClTracer*>(data);
    FTRACE_ASSERT(tracer != nullptr);

    std::stringstream stream;
    if (tracer->CheckOption(TRACE_PID)) {
      stream << "<PID:" << utils::GetPid() << "> ";
    }
    stream << "Device Timeline (queue: " << queue <<
      "): " << name << "<" << id << "> [ns] = " <<
      queued << " (queued) " <<
      submitted << " (submit) " <<
      started << " (start) " <<
      ended << " (end)" << std::endl;
    tracer->correlator_.Log(stream.str());
  }

  static void ChromeDeviceCallback(
      void* data,
      const std::string& queue,
      const std::string& id,
      const std::string& name,
      uint64_t queued,
      uint64_t submitted,
      uint64_t started,
      uint64_t ended) {
    ClTracer* tracer = reinterpret_cast<ClTracer*>(data);
    FTRACE_ASSERT(tracer != nullptr);

    std::stringstream stream;
    stream << "{\"ph\":\"X\", \"pid\":\"" << utils::GetPid() <<
      "\", \"tid\":\"" << queue <<
      "\", \"name\":\"" << name <<
      "\", \"ts\": " << started / NSEC_IN_USEC <<
      ", \"dur\":" << (ended - started) / NSEC_IN_USEC <<
      ", \"args\": {\"id\": \"" << id << "\"}"
      "}," << std::endl;

    FTRACE_ASSERT(tracer->chrome_logger_ != nullptr);
    tracer->chrome_logger_->Log(stream.str());
  }

  static void ChromeKernelCallback(
      void* data,
      const std::string& queue,
      const std::string& id,
      const std::string& name,
      uint64_t queued,
      uint64_t submitted,
      uint64_t started,
      uint64_t ended) {
    ClTracer* tracer = reinterpret_cast<ClTracer*>(data);
    FTRACE_ASSERT(tracer != nullptr);

    std::stringstream stream;
    stream << "{\"ph\":\"X\", \"pid\":\"" << utils::GetPid() <<
      "\", \"tid\":\"" << name <<
      "\", \"name\":\"" << name <<
      "\", \"ts\": " << started / NSEC_IN_USEC <<
      ", \"dur\":" << (ended - started) / NSEC_IN_USEC <<
      ", \"args\": {\"id\": \"" << id << "\"}"
      "}," << std::endl;

    FTRACE_ASSERT(tracer->chrome_logger_ != nullptr);
    tracer->chrome_logger_->Log(stream.str());
  }

  static void ChromeStagesCallback(
      void* data,
      const std::string& queue,
      const std::string& id,
      const std::string& name,
      uint64_t queued,
      uint64_t submitted,
      uint64_t started,
      uint64_t ended) {
    ClTracer* tracer = reinterpret_cast<ClTracer*>(data);
    FTRACE_ASSERT(tracer != nullptr);
    FTRACE_ASSERT(tracer->chrome_logger_ != nullptr);
    std::stringstream stream;

    std::string tid = id + "." + queue;

    FTRACE_ASSERT(submitted > queued);
    stream << "{\"ph\":\"X\", \"pid\":\"" << utils::GetPid() <<
      "\", \"tid\":\"" << tid <<
      "\", \"name\":\"" << name << " (Queued)" <<
      "\", \"ts\": " << queued / NSEC_IN_USEC <<
      ", \"dur\":" << (submitted - queued) / NSEC_IN_USEC <<
      ", \"cname\":\"thread_state_runnable\"" <<
      ", \"args\": {\"id\": \"" << id << "\"}"
      "}," << std::endl;
    tracer->chrome_logger_->Log(stream.str());
    stream.str(std::string());

    FTRACE_ASSERT(started > submitted);
    stream << "{\"ph\":\"X\", \"pid\":\"" << utils::GetPid() <<
      "\", \"tid\":\"" << tid <<
      "\", \"name\":\"" << name << " (Submitted)" <<
      "\", \"ts\": " << submitted / NSEC_IN_USEC <<
      ", \"dur\":" << (started - submitted) / NSEC_IN_USEC <<
      ", \"cname\":\"cq_build_running\"" <<
      ", \"args\": {\"id\": \"" << id << "\"}"
      "}," << std::endl;
    tracer->chrome_logger_->Log(stream.str());
    stream.str(std::string());

    FTRACE_ASSERT(ended > started);
    stream << "{\"ph\":\"X\", \"pid\":\"" << utils::GetPid() <<
      "\", \"tid\":\"" << tid <<
      "\", \"name\":\"" << name << " (Executed)" <<
      "\", \"ts\": " << started / NSEC_IN_USEC <<
      ", \"dur\":" << (ended - started) / NSEC_IN_USEC <<
      ", \"cname\":\"thread_state_iowait\"" <<
      ", \"args\": {\"id\": \"" << id << "\"}"
      "}," << std::endl;
    tracer->chrome_logger_->Log(stream.str());
  }

  static void ChromeKernelStagesCallback(
      void* data,
      const std::string& queue,
      const std::string& id,
      const std::string& name,
      uint64_t queued,
      uint64_t submitted,
      uint64_t started,
      uint64_t ended) {
    ClTracer* tracer = reinterpret_cast<ClTracer*>(data);
    FTRACE_ASSERT(tracer != nullptr);
    FTRACE_ASSERT(tracer->chrome_logger_ != nullptr);
    std::stringstream stream;

    FTRACE_ASSERT(submitted > queued);
    stream << "{\"ph\":\"X\", \"pid\":\"" << utils::GetPid() <<
      "\", \"tid\":\"" << name <<
      "\", \"name\":\"" << name << " (Queued)" <<
      "\", \"ts\": " << queued / NSEC_IN_USEC <<
      ", \"dur\":" << (submitted - queued) / NSEC_IN_USEC <<
      ", \"cname\":\"thread_state_runnable\"" <<
      ", \"args\": {\"id\": \"" << id << "\"}"
      "}," << std::endl;
    tracer->chrome_logger_->Log(stream.str());
    stream.str(std::string());

    FTRACE_ASSERT(started > submitted);
    stream << "{\"ph\":\"X\", \"pid\":\"" << utils::GetPid() <<
      "\", \"tid\":\"" << name <<
      "\", \"name\":\"" << name << " (Submitted)" <<
      "\", \"ts\": " << submitted / NSEC_IN_USEC <<
      ", \"dur\":" << (started - submitted) / NSEC_IN_USEC <<
      ", \"cname\":\"cq_build_running\"" <<
      ", \"args\": {\"id\": \"" << id << "\"}"
      "}," << std::endl;
    tracer->chrome_logger_->Log(stream.str());
    stream.str(std::string());

    FTRACE_ASSERT(ended > started);
    stream << "{\"ph\":\"X\", \"pid\":\"" << utils::GetPid() <<
      "\", \"tid\":\"" << name <<
      "\", \"name\":\"" << name << " (Executed)" <<
      "\", \"ts\": " << started / NSEC_IN_USEC <<
      ", \"dur\":" << (ended - started) / NSEC_IN_USEC <<
      ", \"cname\":\"thread_state_iowait\"" <<
      ", \"args\": {\"id\": \"" << id << "\"}"
      "}," << std::endl;
    tracer->chrome_logger_->Log(stream.str());
  }

  static void DeviceAndChromeDeviceCallback(
      void* data,
      const std::string& queue,
      const std::string& id,
      const std::string& name,
      uint64_t queued,
      uint64_t submitted,
      uint64_t started,
      uint64_t ended) {
    DeviceTimelineCallback(
        data, queue, id, name, queued, submitted, started, ended);
    ChromeDeviceCallback(
        data, queue, id, name, queued, submitted, started, ended);
  }

  static void DeviceAndChromeKernelCallback(
      void* data,
      const std::string& queue,
      const std::string& id,
      const std::string& name,
      uint64_t queued,
      uint64_t submitted,
      uint64_t started,
      uint64_t ended) {
    DeviceTimelineCallback(
        data, queue, id, name, queued, submitted, started, ended);
    ChromeKernelCallback(
        data, queue, id, name, queued, submitted, started, ended);
  }

  static void DeviceAndChromeStagesCallback(
      void* data,
      const std::string& queue,
      const std::string& id,
      const std::string& name,
      uint64_t queued,
      uint64_t submitted,
      uint64_t started,
      uint64_t ended) {
    DeviceTimelineCallback(
        data, queue, id, name, queued, submitted, started, ended);
    ChromeStagesCallback(
        data, queue, id, name, queued, submitted, started, ended);
  }

  static void DeviceAndChromeKernelStagesCallback(
      void* data,
      const std::string& queue,
      const std::string& id,
      const std::string& name,
      uint64_t queued,
      uint64_t submitted,
      uint64_t started,
      uint64_t ended) {
    DeviceTimelineCallback(
        data, queue, id, name, queued, submitted, started, ended);
    ChromeKernelStagesCallback(
        data, queue, id, name, queued, submitted, started, ended);
  }

  static void ChromeLoggingCallback(
      void* data, uint64_t id, const std::string& name,
      uint64_t started, uint64_t ended) {
    ClTracer* tracer = reinterpret_cast<ClTracer*>(data);
    FTRACE_ASSERT(tracer != nullptr);

    std::stringstream stream;
    stream << "{\"ph\":\"X\", \"pid\":\"" <<
      utils::GetPid() << "\", \"tid\":\"" << utils::GetTid() <<
      "\", \"name\":\"" << name <<
      "\", \"ts\": " << started / NSEC_IN_USEC <<
      ", \"dur\":" << (ended - started) / NSEC_IN_USEC <<
      ", \"args\": {\"id\": \"" << id << "\"}"
      "}," << std::endl;
    FTRACE_ASSERT(tracer->chrome_logger_ != nullptr);
    tracer->chrome_logger_->Log(stream.str());
  }

 private:
  TraceOptions options_;

  Correlator correlator_;
  uint64_t total_execution_time_ = 0;

  ClApiCollector* cpu_api_collector_ = nullptr;
  ClApiCollector* gpu_api_collector_ = nullptr;

  ClKernelCollector* cpu_kernel_collector_ = nullptr;
  ClKernelCollector* gpu_kernel_collector_ = nullptr;

  std::string chrome_trace_file_name_;
  Logger* chrome_logger_ = nullptr;
};

#endif // FTRACE_TOOLS_CL_TRACER_CL_TRACER_H_