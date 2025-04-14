#ifndef FTRACE_TOOLS_UTILS_CORRELATOR_H_
#define FTRACE_TOOLS_UTILS_CORRELATOR_H_

#include <map>
#include <vector>

#ifdef FTRACE_LEVEL_ZERO
#include <level_zero/ze_api.h>
#endif // FTRACE_LEVEL_ZERO

#include "logger.h"
#include "finetrace_assert.h"
#include "utils.h"

struct ApiCollectorOptions {
  bool call_tracing = false;
  bool need_tid = false;
  bool need_pid = false;
  bool demangle = false;
};

struct KernelCollectorOptions {
  bool verbose = false;
  bool demangle = false;
  bool kernels_per_tile = false;
};

class Correlator {
 public:
  Correlator(const std::string& log_file, bool conditional_collection)
      : logger_(log_file), conditional_collection_(conditional_collection),
        base_time_(utils::GetSystemTime()) {}

  void Log(const std::string& text) {
    logger_.Log(text);
  }

  uint64_t GetTimestamp() const {
    return utils::GetSystemTime() - base_time_;
  }

  uint64_t GetTimestamp(uint64_t timestamp) const {
    FTRACE_ASSERT(timestamp > base_time_);
    return timestamp - base_time_;
  }

  uint64_t GetStartPoint() const {
    return base_time_;
  }

  uint64_t GetKernelId() const {
    return kernel_id_;
  }

  void SetKernelId(uint64_t kernel_id) {
    kernel_id_ = kernel_id;
  }

  bool IsCollectionEnabled() const {
    if (conditional_collection_) {
      std::string enabled = utils::GetEnv("FTRACE_ENABLE_COLLECTION");
      if (enabled.empty() || enabled == "0") {
        return false;
      }
    }
    return true;
  }

#ifdef FTRACE_LEVEL_ZERO

  std::vector<uint64_t> GetKernelId(
      ze_command_list_handle_t command_list) {
    FTRACE_ASSERT(command_list != nullptr);
    if (kernel_id_map_.count(command_list) > 0) {
      return kernel_id_map_[command_list];
    } else {
      return std::vector<uint64_t>();
    }
  }

  void CreateKernelIdList(ze_command_list_handle_t command_list) {
    FTRACE_ASSERT(kernel_id_map_.count(command_list) == 0);
    kernel_id_map_[command_list] = std::vector<uint64_t>();
  }

  void RemoveKernelIdList(ze_command_list_handle_t command_list) {
    FTRACE_ASSERT(kernel_id_map_.count(command_list) == 1);
    kernel_id_map_.erase(command_list);
  }

  void ResetKernelIdList(ze_command_list_handle_t command_list) {
    FTRACE_ASSERT(kernel_id_map_.count(command_list) == 1);
    kernel_id_map_[command_list].clear();
  }

  void AddKernelId(ze_command_list_handle_t command_list, uint64_t kernel_id) {
    FTRACE_ASSERT(kernel_id_map_.count(command_list) == 1);
    kernel_id_map_[command_list].push_back(kernel_id);
  }

  std::vector<uint64_t> GetCallId(
      ze_command_list_handle_t command_list) {
    FTRACE_ASSERT(command_list != nullptr);
    if (call_id_map_.count(command_list) > 0) {
      return call_id_map_[command_list];
    } else {
      return std::vector<uint64_t>();
    }
  }

  void CreateCallIdList(ze_command_list_handle_t command_list) {
    FTRACE_ASSERT(call_id_map_.count(command_list) == 0);
    call_id_map_[command_list] = std::vector<uint64_t>();
  }

  void RemoveCallIdList(ze_command_list_handle_t command_list) {
    FTRACE_ASSERT(call_id_map_.count(command_list) == 1);
    call_id_map_.erase(command_list);
  }

  void ResetCallIdList(ze_command_list_handle_t command_list) {
    FTRACE_ASSERT(call_id_map_.count(command_list) == 1);
    call_id_map_[command_list].clear();
  }

  void AddCallId(ze_command_list_handle_t command_list, uint64_t call_id) {
    FTRACE_ASSERT(call_id_map_.count(command_list) == 1);
    call_id_map_[command_list].push_back(call_id);
  }

#endif // FTRACE_LEVEL_ZERO

 private:
  uint64_t base_time_;
  Logger logger_;
  bool conditional_collection_;
  static thread_local uint64_t kernel_id_;
#ifdef FTRACE_LEVEL_ZERO
  std::map<ze_command_list_handle_t, std::vector<uint64_t> > kernel_id_map_;
  std::map<ze_command_list_handle_t, std::vector<uint64_t> > call_id_map_;
#endif // FTRACE_LEVEL_ZERO
};

#endif // FTRACE_TOOLS_UTILS_CORRELATOR_H_