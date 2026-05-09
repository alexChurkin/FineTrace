#include <math.h>
#include <iostream>
#include <vector>

#include "ze_utils.h"
#include "utils.h"

#define ALIGN 64

static void RunAndCheck(ze_kernel_handle_t kernel,
                        ze_device_handle_t device,
                        ze_context_handle_t context,
                        int width, int height, int iters) {
  size_t bytes = (size_t)width * height * sizeof(float);

  ze_device_mem_alloc_desc_t dev_desc = {ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC};
  void *dev_in = nullptr, *dev_out = nullptr;
  zeMemAllocDevice(context, &dev_desc, bytes, ALIGN, device, &dev_in);
  zeMemAllocDevice(context, &dev_desc, bytes, ALIGN, device, &dev_out);

  std::vector<float> host(width * height, 0.0f);
  for (int y = height / 4; y < 3 * height / 4; ++y)
    for (int x = width / 4; x < 3 * width / 4; ++x)
      host[y * width + x] = 1.0f;

  ze_command_queue_desc_t cq_desc = {ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC};
  cq_desc.mode = ZE_COMMAND_QUEUE_MODE_ASYNCHRONOUS;
  ze_command_queue_handle_t queue = nullptr;
  zeCommandQueueCreate(context, device, &cq_desc, &queue);

  ze_command_list_desc_t cl_desc = {ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC};
  ze_command_list_handle_t cmd_list = nullptr;
  zeCommandListCreate(context, device, &cl_desc, &cmd_list);

  uint32_t gsx = 0, gsy = 0, gsz = 1;
  zeKernelSuggestGroupSize(kernel, width, height, 1, &gsx, &gsy, &gsz);
  zeKernelSetGroupSize(kernel, gsx, gsy, 1);

  zeCommandListAppendMemoryCopy(cmd_list, dev_in, host.data(), bytes, nullptr, 0, nullptr);
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

  ze_group_count_t groups = {(uint32_t)((width  + gsx - 1) / gsx),
                              (uint32_t)((height + gsy - 1) / gsy), 1};
  double total = 0.0;

  for (int it = 0; it < iters; ++it) {
    zeCommandListReset(cmd_list);
    zeEventHostReset(event);
    void *in  = (it % 2 == 0) ? dev_in  : dev_out;
    void *out = (it % 2 == 0) ? dev_out : dev_in;
    zeKernelSetArgumentValue(kernel, 0, sizeof(in),  &in);
    zeKernelSetArgumentValue(kernel, 1, sizeof(out), &out);
    zeKernelSetArgumentValue(kernel, 2, sizeof(int), &width);
    zeKernelSetArgumentValue(kernel, 3, sizeof(int), &height);
    zeCommandListAppendLaunchKernel(cmd_list, kernel, &groups, event, 0, nullptr);
    zeCommandListClose(cmd_list);
    zeCommandQueueExecuteCommandLists(queue, 1, &cmd_list, nullptr);
    zeCommandQueueSynchronize(queue, UINT64_MAX);

    ze_device_properties_t props = {ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES_1_2};
    zeDeviceGetProperties(device, &props);
    ze_kernel_timestamp_result_t ts = {};
    zeEventQueryKernelTimestamp(event, &ts);
    total += static_cast<double>(ts.global.kernelEnd - ts.global.kernelStart) / props.timerResolution;
  }

  void *final_buf = (iters % 2 == 0) ? dev_in : dev_out;
  zeCommandListReset(cmd_list);
  zeCommandListAppendMemoryCopy(cmd_list, host.data(), final_buf, bytes, nullptr, 0, nullptr);
  zeCommandListClose(cmd_list);
  zeCommandQueueExecuteCommandLists(queue, 1, &cmd_list, nullptr);
  zeCommandQueueSynchronize(queue, UINT64_MAX);

  int cx = width / 2, cy = height / 2;
  std::cout << "Centre value after " << iters << " iters: " << host[cy * width + cx] << std::endl;
  std::cout << "Avg kernel time: " << total / iters * 1e3 << " ms" << std::endl;
  std::cout << "Effective GFLOP/s: "
            << 5.0 * (width - 2) * (height - 2) / (total / iters) / 1e9 << std::endl;

  zeEventDestroy(event);
  zeEventPoolDestroy(event_pool);
  zeCommandListDestroy(cmd_list);
  zeCommandQueueDestroy(queue);
  zeMemFree(context, dev_in);
  zeMemFree(context, dev_out);
}

int main(int argc, char* argv[]) {
  ze_result_t status = zeInit(ZE_INIT_FLAG_GPU_ONLY);
  FTRACE_ASSERT(status == ZE_RESULT_SUCCESS);

  ze_device_handle_t device = utils::ze::GetGpuDevice();
  ze_driver_handle_t driver = utils::ze::GetGpuDriver();
  if (!device || !driver) { std::cout << "No GPU device\n"; return 1; }

  int width = 1024, height = 1024, iters = 100;
  if (argc > 1) width  = std::stoi(argv[1]);
  if (argc > 2) height = std::stoi(argv[2]);
  if (argc > 3) iters  = std::stoi(argv[3]);

  std::cout << "Level Zero 2D Jacobi Stencil (" << width << "x" << height
            << ", iters=" << iters << ")\n";
  std::cout << "Device: " << utils::ze::GetDeviceName(device) << std::endl;

  ze_context_handle_t context = utils::ze::GetContext(driver);
  FTRACE_ASSERT(context != nullptr);

  auto binary = utils::LoadBinaryFile(utils::GetExecutablePath() + "stencil.spv");
  if (binary.empty()) { std::cout << "Cannot find stencil.spv\n"; return 1; }

  ze_module_desc_t mod_desc = {ZE_STRUCTURE_TYPE_MODULE_DESC, nullptr,
    ZE_MODULE_FORMAT_IL_SPIRV, static_cast<uint32_t>(binary.size()),
    binary.data(), nullptr, nullptr};
  ze_module_handle_t module = nullptr;
  zeModuleCreate(context, device, &mod_desc, &module, nullptr);
  FTRACE_ASSERT(module != nullptr);

  ze_kernel_desc_t kern_desc = {ZE_STRUCTURE_TYPE_KERNEL_DESC, nullptr, 0, "jacobi"};
  ze_kernel_handle_t kernel = nullptr;
  zeKernelCreate(module, &kern_desc, &kernel);
  FTRACE_ASSERT(kernel != nullptr);

  RunAndCheck(kernel, device, context, width, height, iters);

  zeKernelDestroy(kernel);
  zeModuleDestroy(module);
  zeContextDestroy(context);
  return 0;
}
