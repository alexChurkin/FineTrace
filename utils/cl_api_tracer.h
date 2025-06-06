#ifndef FTRACE_UTILS_CL_API_TRACER_H_
#define FTRACE_UTILS_CL_API_TRACER_H_

#include <CL/tracing_api.h>

#include "finetrace_assert.h"

class ClApiTracer {
 public:
  ClApiTracer(
      cl_device_id device, cl_tracing_callback callback, void* user_data) {
    FTRACE_ASSERT(device != nullptr);

    bool loaded = LoadTracingFunctions(device);
    if (!loaded) {
      return;
    }

    cl_int status = CL_SUCCESS;
    status = clCreateTracingHandle_(device, callback, user_data, &handle_);
    if (status != CL_SUCCESS) {
      handle_ = nullptr;
    }
  }

  ClApiTracer(const ClApiTracer& that) = delete;

  ~ClApiTracer() {
    if (handle_ != nullptr) {
      cl_int status = CL_SUCCESS;
      status = clDestroyTracingHandle_(handle_);
      FTRACE_ASSERT(status == CL_SUCCESS);
    }
  }

  bool SetTracingFunction(cl_function_id function) {
    if (!IsValid()) {
      return false;
    }

    cl_int status = CL_SUCCESS;
    status = clSetTracingPoint_(handle_, function, CL_TRUE);
    if (status != CL_SUCCESS) {
      return false;
    }

    return true;
  }

  bool Enable() {
    if (!IsValid()) {
      return false;
    }

    cl_int status = CL_SUCCESS;
    status = clEnableTracing_(handle_);
    if (status != CL_SUCCESS) {
      return false;
    }

    return true;
  }

  bool Disable() {
    if (!IsValid()) {
      return false;
    }

    cl_int status = CL_SUCCESS;
    status = clDisableTracing_(handle_);
    if (status != CL_SUCCESS) {
      return false;
    }

    return true;
  }

  bool IsValid() const {
    return (handle_ != nullptr);
  }

 private:
  bool LoadTracingFunctions(cl_device_id device) {
    FTRACE_ASSERT(device != nullptr);

    cl_int status = CL_SUCCESS;

    cl_platform_id platform = nullptr;
    status = clGetDeviceInfo(device, CL_DEVICE_PLATFORM, sizeof(platform),
                             &platform, nullptr);
    FTRACE_ASSERT(status == CL_SUCCESS);

    clCreateTracingHandle_ =
      reinterpret_cast<decltype(clCreateTracingHandleINTEL)*>(
        clGetExtensionFunctionAddressForPlatform(
          platform, "clCreateTracingHandleINTEL"));
    clSetTracingPoint_ =
      reinterpret_cast<decltype(clSetTracingPointINTEL)*>(
        clGetExtensionFunctionAddressForPlatform(
          platform, "clSetTracingPointINTEL"));
    clDestroyTracingHandle_ =
      reinterpret_cast<decltype(clDestroyTracingHandleINTEL)*>(
        clGetExtensionFunctionAddressForPlatform(
          platform, "clDestroyTracingHandleINTEL"));
    clEnableTracing_ =
      reinterpret_cast<decltype(clEnableTracingINTEL)*>(
        clGetExtensionFunctionAddressForPlatform(
          platform, "clEnableTracingINTEL"));
    clDisableTracing_ =
      reinterpret_cast<decltype(clDisableTracingINTEL)*>(
        clGetExtensionFunctionAddressForPlatform(
          platform, "clDisableTracingINTEL"));
    clGetTracingState_ =
      reinterpret_cast<decltype(clGetTracingStateINTEL)*>(
        clGetExtensionFunctionAddressForPlatform(
          platform, "clGetTracingStateINTEL"));
    
    if (clCreateTracingHandle_ == nullptr ||
        clSetTracingPoint_ == nullptr ||
        clDestroyTracingHandle_ == nullptr ||
        clEnableTracing_ == nullptr ||
        clDisableTracing_ == nullptr ||
        clGetTracingState_ == nullptr) {
      return false;
    }

    return true;
  }

  cl_tracing_handle handle_ = nullptr;

  decltype(clCreateTracingHandleINTEL)* clCreateTracingHandle_ = nullptr;
  decltype(clSetTracingPointINTEL)* clSetTracingPoint_ = nullptr;
  decltype(clDestroyTracingHandleINTEL)* clDestroyTracingHandle_ = nullptr;
  decltype(clEnableTracingINTEL)* clEnableTracing_ = nullptr;
  decltype(clDisableTracingINTEL)* clDisableTracing_ = nullptr;
  decltype(clGetTracingStateINTEL)* clGetTracingState_ = nullptr;
};

#endif // FTRACE_UTILS_CL_API_TRACER_H_
