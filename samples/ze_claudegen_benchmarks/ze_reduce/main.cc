#include <math.h>
#include <chrono>
#include <iostream>
#include <numeric>
#include <vector>

#include "ze_utils.h"
#include "utils.h"

#define ALIGN 64
#define GROUP_SIZE 256

static void RunAndCheck(ze_kernel_handle_t kernel,
                        ze_device_handle_t device,
                        ze_context_handle_t context,
                        const std::vector<float>& host_input,
                        int repeat_count) {
  int n = static_cast<int>(host_input.size());
  int num_groups = (n + GROUP_SIZE - 1) / GROUP_SIZE;
  size_t input_bytes   = n          * sizeof(float);
  size_t partial_bytes = num_groups * sizeof(float);

  ze_device_mem_alloc_desc_t dev_desc = {ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC};
  void *dev_input = nullptr, *dev_partial = nullptr;
  zeMemAllocDevice(context, &dev_desc, input_bytes,   ALIGN, device, &dev_input);
  zeMemAllocDevice(context, &dev_desc, partial_bytes, ALIGN, device, &dev_partial);

  zeKernelSetGroupSize(kernel, GROUP_SIZE, 1, 1);
  zeKernelSetArgumentValue(kernel, 0, sizeof(dev_input),   &dev_input);
  zeKernelSetArgumentValue(kernel, 1, sizeof(dev_partial), &dev_partial);
  zeKernelSetArgumentValue(kernel, 2, sizeof(n),           &n);
  zeKernelSetArgumentValue(kernel, 3, GROUP_SIZE * sizeof(float), nullptr);

  ze_command_queue_desc_t cq_desc = {ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC};
  cq_desc.mode = ZE_COMMAND_QUEUE_MODE_ASYNCHRONOUS;
  ze_command_queue_handle_t queue = nullptr;
  zeCommandQueueCreate(context, device, &cq_desc, &queue);

  ze_command_list_desc_t cl_desc = {ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC};
  ze_command_list_handle_t cmd_list = nullptr;
  zeCommandListCreate(context, device, &cl_desc, &cmd_list);

  // Upload once
  zeCommandListAppendMemoryCopy(cmd_list, dev_input, host_input.data(), input_bytes, nullptr, 0, nullptr);
  zeCommandListClose(cmd_list);
  zeCommandQueueExecuteCommandLists(queue, 1, &cmd_list, nullptr);
  zeCommandQueueSynchronize(queue, UINT64_MAX);

  ze_event_pool_desc_t ep_desc = {ZE_STRUCTURE_TYPE_EVENT_POOL_DESC, nullptr,
    ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP | ZE_EVENT_POOL_FLAG_HOST_VISIBLE, 1};
  ze_event_pool_handle_t event_pool = nullptr;
  zeEventPoolCreate(context, &ep_desc, 0, nullptr, &event_pool);
  ze_event_desc_t ev_desc = {ZE_STRUCTURE_TYPE_EVENT_DESC, nullptr, 0,
    ZE_EVENT_SCOPE_FLAG_HOST, ZE_EVENT_SCOPE_FLAG_HOST};
  ze_event_handle_t event = nullptr;
  zeEventCreate(event_pool, &ev_desc, &event);

  ze_group_count_t groups = {(uint32_t)num_groups, 1, 1};
  std::vector<float> partial(num_groups);
  double total = 0.0;
  double cpu_sum = std::accumulate(host_input.begin(), host_input.end(), 0.0);

  for (int r = 0; r < repeat_count; ++r) {
    zeCommandListReset(cmd_list);
    zeEventHostReset(event);
    zeCommandListAppendLaunchKernel(cmd_list, kernel, &groups, event, 0, nullptr);
    zeCommandListAppendBarrier(cmd_list, nullptr, 0, nullptr);
    zeCommandListAppendMemoryCopy(cmd_list, partial.data(), dev_partial, partial_bytes, nullptr, 0, nullptr);
    zeCommandListClose(cmd_list);
    zeCommandQueueExecuteCommandLists(queue, 1, &cmd_list, nullptr);
    zeCommandQueueSynchronize(queue, UINT64_MAX);

    ze_device_properties_t props = {ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES_1_2};
    zeDeviceGetProperties(device, &props);
    ze_kernel_timestamp_result_t ts = {};
    zeEventQueryKernelTimestamp(event, &ts);
    double t = static_cast<double>(ts.global.kernelEnd - ts.global.kernelStart) / props.timerResolution;
    total += t;

    double gpu_sum = std::accumulate(partial.begin(), partial.end(), 0.0);
    double rel_err = fabs(gpu_sum - cpu_sum) / fabs(cpu_sum);
    std::cout << "Kernel time: " << t * 1e3 << " ms"
              << "  gpu_sum=" << gpu_sum
              << "  rel_err=" << rel_err
              << (rel_err < 1e-4 ? "  CORRECT" : "  WRONG") << std::endl;
  }
  std::cout << "Avg kernel time: " << total / repeat_count * 1e3 << " ms" << std::endl;

  zeEventDestroy(event);
  zeEventPoolDestroy(event_pool);
  zeCommandListDestroy(cmd_list);
  zeCommandQueueDestroy(queue);
  zeMemFree(context, dev_input);
  zeMemFree(context, dev_partial);
}

int main(int argc, char* argv[]) {
  ze_result_t status = zeInit(ZE_INIT_FLAG_GPU_ONLY);
  FTRACE_ASSERT(status == ZE_RESULT_SUCCESS);

  ze_device_handle_t device = utils::ze::GetGpuDevice();
  ze_driver_handle_t driver = utils::ze::GetGpuDriver();
  if (!device || !driver) { std::cout << "No GPU device\n"; return 1; }

  int n = 1 << 24;
  int repeats = 4;
  if (argc > 1) n       = std::stoi(argv[1]);
  if (argc > 2) repeats = std::stoi(argv[2]);

  std::cout << "Level Zero Sum Reduction (n=" << n << ", repeats=" << repeats << ")\n";
  std::cout << "Device: " << utils::ze::GetDeviceName(device) << std::endl;

  ze_context_handle_t context = utils::ze::GetContext(driver);
  FTRACE_ASSERT(context != nullptr);

  auto binary = utils::LoadBinaryFile(utils::GetExecutablePath() + "reduce.spv");
  if (binary.empty()) { std::cout << "Cannot find reduce.spv\n"; return 1; }

  ze_module_desc_t mod_desc = {ZE_STRUCTURE_TYPE_MODULE_DESC, nullptr,
    ZE_MODULE_FORMAT_IL_SPIRV, static_cast<uint32_t>(binary.size()),
    binary.data(), nullptr, nullptr};
  ze_module_handle_t module = nullptr;
  zeModuleCreate(context, device, &mod_desc, &module, nullptr);
  FTRACE_ASSERT(module != nullptr);

  ze_kernel_desc_t kern_desc = {ZE_STRUCTURE_TYPE_KERNEL_DESC, nullptr, 0, "reduce_sum"};
  ze_kernel_handle_t kernel = nullptr;
  zeKernelCreate(module, &kern_desc, &kernel);
  FTRACE_ASSERT(kernel != nullptr);

  std::vector<float> input(n, 1.0f);
  RunAndCheck(kernel, device, context, input, repeats);

  zeKernelDestroy(kernel);
  zeModuleDestroy(module);
  zeContextDestroy(context);
  return 0;
}
