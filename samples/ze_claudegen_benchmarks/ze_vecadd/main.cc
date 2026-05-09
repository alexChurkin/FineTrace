#include <math.h>
#include <chrono>
#include <iostream>
#include <vector>

#include "ze_utils.h"
#include "utils.h"

#define ALIGN 64
#define VAL_A 1.0f
#define VAL_B 2.0f
#define MAX_EPS 1.0e-4f

static void RunAndCheck(ze_kernel_handle_t kernel,
                        ze_device_handle_t device,
                        ze_context_handle_t context,
                        int n, int repeat_count) {
  ze_device_mem_alloc_desc_t dev_desc = {ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC};
  ze_host_mem_alloc_desc_t   host_desc = {ZE_STRUCTURE_TYPE_HOST_MEM_ALLOC_DESC};

  void *a = nullptr, *b = nullptr, *c = nullptr;
  size_t bytes = n * sizeof(float);
  zeMemAllocShared(context, &dev_desc, &host_desc, bytes, ALIGN, device, &a);
  zeMemAllocShared(context, &dev_desc, &host_desc, bytes, ALIGN, device, &b);
  zeMemAllocShared(context, &dev_desc, &host_desc, bytes, ALIGN, device, &c);

  float *fa = static_cast<float*>(a);
  float *fb = static_cast<float*>(b);
  float *fc = static_cast<float*>(c);
  for (int i = 0; i < n; ++i) { fa[i] = VAL_A; fb[i] = VAL_B; fc[i] = 0.0f; }

  uint32_t gsx = 0, gsy = 1, gsz = 1;
  zeKernelSuggestGroupSize(kernel, n, 1, 1, &gsx, &gsy, &gsz);
  zeKernelSetGroupSize(kernel, gsx, 1, 1);
  zeKernelSetArgumentValue(kernel, 0, sizeof(a), &a);
  zeKernelSetArgumentValue(kernel, 1, sizeof(b), &b);
  zeKernelSetArgumentValue(kernel, 2, sizeof(c), &c);
  zeKernelSetArgumentValue(kernel, 3, sizeof(n), &n);

  ze_command_queue_desc_t cq_desc = {ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC};
  cq_desc.mode = ZE_COMMAND_QUEUE_MODE_ASYNCHRONOUS;
  ze_command_queue_handle_t queue = nullptr;
  zeCommandQueueCreate(context, device, &cq_desc, &queue);

  ze_command_list_desc_t cl_desc = {ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC};
  ze_command_list_handle_t cmd_list = nullptr;
  zeCommandListCreate(context, device, &cl_desc, &cmd_list);

  ze_event_pool_desc_t ep_desc = {ZE_STRUCTURE_TYPE_EVENT_POOL_DESC, nullptr,
    ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP | ZE_EVENT_POOL_FLAG_HOST_VISIBLE, 1};
  ze_event_pool_handle_t event_pool = nullptr;
  zeEventPoolCreate(context, &ep_desc, 0, nullptr, &event_pool);
  ze_event_desc_t ev_desc = {ZE_STRUCTURE_TYPE_EVENT_DESC, nullptr, 0,
    ZE_EVENT_SCOPE_FLAG_HOST, ZE_EVENT_SCOPE_FLAG_HOST};
  ze_event_handle_t event = nullptr;
  zeEventCreate(event_pool, &ev_desc, &event);

  ze_group_count_t groups = {(uint32_t)((n + gsx - 1) / gsx), 1, 1};
  double total = 0.0;

  for (int r = 0; r < repeat_count; ++r) {
    zeCommandListReset(cmd_list);
    zeEventHostReset(event);
    zeCommandListAppendLaunchKernel(cmd_list, kernel, &groups, event, 0, nullptr);
    zeCommandListClose(cmd_list);
    zeCommandQueueExecuteCommandLists(queue, 1, &cmd_list, nullptr);
    zeCommandQueueSynchronize(queue, UINT64_MAX);

    ze_device_properties_t props = {ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES_1_2};
    zeDeviceGetProperties(device, &props);
    ze_kernel_timestamp_result_t ts = {};
    zeEventQueryKernelTimestamp(event, &ts);
    double t = static_cast<double>(ts.global.kernelEnd - ts.global.kernelStart) / props.timerResolution;
    total += t;
    std::cout << "Kernel time: " << t * 1e3 << " ms" << std::endl;
  }

  float max_err = 0.0f;
  for (int i = 0; i < n; ++i) max_err = fmaxf(max_err, fabsf(fc[i] - (VAL_A + VAL_B)));
  std::cout << "Result: " << (max_err < MAX_EPS ? "CORRECT" : "WRONG")
            << "  max_err=" << max_err << std::endl;
  std::cout << "Avg kernel time: " << total / repeat_count * 1e3 << " ms" << std::endl;
  std::cout << "Effective bandwidth: "
            << 3.0 * bytes / 1e9 / (total / repeat_count) << " GB/s" << std::endl;

  zeEventDestroy(event);
  zeEventPoolDestroy(event_pool);
  zeCommandListDestroy(cmd_list);
  zeCommandQueueDestroy(queue);
  zeMemFree(context, a);
  zeMemFree(context, b);
  zeMemFree(context, c);
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

  std::cout << "Level Zero Vector Addition (n=" << n << ", repeats=" << repeats << ")\n";
  std::cout << "Device: " << utils::ze::GetDeviceName(device) << std::endl;

  ze_context_handle_t context = utils::ze::GetContext(driver);
  FTRACE_ASSERT(context != nullptr);

  auto binary = utils::LoadBinaryFile(utils::GetExecutablePath() + "vecadd.spv");
  if (binary.empty()) { std::cout << "Cannot find vecadd.spv\n"; return 1; }

  ze_module_desc_t mod_desc = {ZE_STRUCTURE_TYPE_MODULE_DESC, nullptr,
    ZE_MODULE_FORMAT_IL_SPIRV, static_cast<uint32_t>(binary.size()),
    binary.data(), nullptr, nullptr};
  ze_module_handle_t module = nullptr;
  zeModuleCreate(context, device, &mod_desc, &module, nullptr);
  FTRACE_ASSERT(module != nullptr);

  ze_kernel_desc_t kern_desc = {ZE_STRUCTURE_TYPE_KERNEL_DESC, nullptr, 0, "vecadd"};
  ze_kernel_handle_t kernel = nullptr;
  zeKernelCreate(module, &kern_desc, &kernel);
  FTRACE_ASSERT(kernel != nullptr);

  RunAndCheck(kernel, device, context, n, repeats);

  zeKernelDestroy(kernel);
  zeModuleDestroy(module);
  zeContextDestroy(context);
  return 0;
}
