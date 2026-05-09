#include <iostream>
#include <numeric>
#include <vector>

#include "ze_utils.h"
#include "utils.h"

#define ALIGN 64

static void RunAndCheck(ze_kernel_handle_t kernel,
                        ze_device_handle_t device,
                        ze_context_handle_t context,
                        int width, int height, int max_iter,
                        float x_min, float x_max, float y_min, float y_max,
                        int repeat_count) {
  size_t bytes = (size_t)width * height * sizeof(int);

  ze_device_mem_alloc_desc_t dev_desc = {ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC};
  void *dev_out = nullptr;
  zeMemAllocDevice(context, &dev_desc, bytes, ALIGN, device, &dev_out);

  uint32_t gsx = 0, gsy = 0, gsz = 1;
  zeKernelSuggestGroupSize(kernel, width, height, 1, &gsx, &gsy, &gsz);
  zeKernelSetGroupSize(kernel, gsx, gsy, 1);
  zeKernelSetArgumentValue(kernel, 0, sizeof(dev_out), &dev_out);
  zeKernelSetArgumentValue(kernel, 1, sizeof(width),    &width);
  zeKernelSetArgumentValue(kernel, 2, sizeof(height),   &height);
  zeKernelSetArgumentValue(kernel, 3, sizeof(max_iter), &max_iter);
  zeKernelSetArgumentValue(kernel, 4, sizeof(x_min),    &x_min);
  zeKernelSetArgumentValue(kernel, 5, sizeof(x_max),    &x_max);
  zeKernelSetArgumentValue(kernel, 6, sizeof(y_min),    &y_min);
  zeKernelSetArgumentValue(kernel, 7, sizeof(y_max),    &y_max);

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

  ze_group_count_t groups = {(uint32_t)((width  + gsx - 1) / gsx),
                              (uint32_t)((height + gsy - 1) / gsy), 1};
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

  std::vector<int> host(width * height);
  zeCommandListReset(cmd_list);
  zeCommandListAppendMemoryCopy(cmd_list, host.data(), dev_out, bytes, nullptr, 0, nullptr);
  zeCommandListClose(cmd_list);
  zeCommandQueueExecuteCommandLists(queue, 1, &cmd_list, nullptr);
  zeCommandQueueSynchronize(queue, UINT64_MAX);

  long long checksum = 0;
  for (int v : host) checksum += v;
  std::cout << "Checksum (sum of escape counts): " << checksum << std::endl;
  std::cout << "Avg kernel time: " << total / repeat_count * 1e3 << " ms" << std::endl;

  zeEventDestroy(event);
  zeEventPoolDestroy(event_pool);
  zeCommandListDestroy(cmd_list);
  zeCommandQueueDestroy(queue);
  zeMemFree(context, dev_out);
}

int main(int argc, char* argv[]) {
  ze_result_t status = zeInit(ZE_INIT_FLAG_GPU_ONLY);
  FTRACE_ASSERT(status == ZE_RESULT_SUCCESS);

  ze_device_handle_t device = utils::ze::GetGpuDevice();
  ze_driver_handle_t driver = utils::ze::GetGpuDriver();
  if (!device || !driver) { std::cout << "No GPU device\n"; return 1; }

  int width = 3840, height = 2160, max_iter = 256, repeats = 4;
  if (argc > 1) width    = std::stoi(argv[1]);
  if (argc > 2) height   = std::stoi(argv[2]);
  if (argc > 3) max_iter = std::stoi(argv[3]);
  if (argc > 4) repeats  = std::stoi(argv[4]);

  std::cout << "Level Zero Mandelbrot (" << width << "x" << height
            << ", max_iter=" << max_iter << ", repeats=" << repeats << ")\n";
  std::cout << "Device: " << utils::ze::GetDeviceName(device) << std::endl;

  ze_context_handle_t context = utils::ze::GetContext(driver);
  FTRACE_ASSERT(context != nullptr);

  auto binary = utils::LoadBinaryFile(utils::GetExecutablePath() + "mandelbrot.spv");
  if (binary.empty()) { std::cout << "Cannot find mandelbrot.spv\n"; return 1; }

  ze_module_desc_t mod_desc = {ZE_STRUCTURE_TYPE_MODULE_DESC, nullptr,
    ZE_MODULE_FORMAT_IL_SPIRV, static_cast<uint32_t>(binary.size()),
    binary.data(), nullptr, nullptr};
  ze_module_handle_t module = nullptr;
  zeModuleCreate(context, device, &mod_desc, &module, nullptr);
  FTRACE_ASSERT(module != nullptr);

  ze_kernel_desc_t kern_desc = {ZE_STRUCTURE_TYPE_KERNEL_DESC, nullptr, 0, "mandelbrot"};
  ze_kernel_handle_t kernel = nullptr;
  zeKernelCreate(module, &kern_desc, &kernel);
  FTRACE_ASSERT(kernel != nullptr);

  RunAndCheck(kernel, device, context,
              width, height, max_iter,
              -2.5f, 1.0f, -1.25f, 1.25f, repeats);

  zeKernelDestroy(kernel);
  zeModuleDestroy(module);
  zeContextDestroy(context);
  return 0;
}
