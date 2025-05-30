#include "cl_ext_collector.h"

#include "cl_ext_callbacks.h"
#include "cl_api_collector.h"

ClExtCollector* ClExtCollector::instance_ = nullptr;

uint64_t ClExtCollector::GetTimestampGPU() const {
  return gpu_collector_->GetTimestamp();
}

uint64_t ClExtCollector::GetTimestampCPU() const {
  return cpu_collector_->GetTimestamp();
}

void ClExtCollector::AddFunctionTimeCPU(
    const char* function_name, uint64_t time) {
  cpu_collector_->AddFunctionTime(function_name, time);
}

void ClExtCollector::AddFunctionTimeGPU(
    const char* function_name, uint64_t time) {
  gpu_collector_->AddFunctionTime(function_name, time);
}

bool ClExtCollector::IsCallTracingCPU() const {
  return cpu_collector_->options_.call_tracing;
}

bool ClExtCollector::IsCallTracingGPU() const {
  return gpu_collector_->options_.call_tracing;
}

bool ClExtCollector::NeedPidCPU() const {
  return cpu_collector_->NeedPid();
}

bool ClExtCollector::NeedPidGPU() const {
  return gpu_collector_->NeedPid();
}

bool ClExtCollector::NeedTidCPU() const {
  return cpu_collector_->NeedTid();
}

bool ClExtCollector::NeedTidGPU() const {
  return gpu_collector_->NeedTid();
}

void ClExtCollector::LogCPU(const std::string& message) const {
  cpu_collector_->Log(message);
}

void ClExtCollector::LogGPU(const std::string& message) const {
  gpu_collector_->Log(message);
}

void ClExtCollector::CallbackCPU(
    const char* function_name, uint64_t start, uint64_t end) const {
  if (cpu_collector_->callback_ != nullptr) {
    cpu_collector_->callback_(
        cpu_collector_->callback_data_, 0, function_name, start, end);
  }
}

void ClExtCollector::CallbackGPU(
    const char* function_name, uint64_t start, uint64_t end) const {
  if (gpu_collector_->callback_ != nullptr) {
    gpu_collector_->callback_(
        gpu_collector_->callback_data_, 0, function_name, start, end);
  }
}