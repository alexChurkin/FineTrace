include("./build_utils/CMakeLists.txt")
SetRequiredCMakeVersion()
cmake_minimum_required(VERSION ${REQUIRED_CMAKE_VERSION})

project(FineTrace CXX)
SetCompilerFlags()
SetBuildType()

# Tool Library

add_library(finetrace_tool SHARED
  "${PROJECT_SOURCE_DIR}/loader/init.cc"
  "${PROJECT_SOURCE_DIR}/cl_tracer/cl_ext_collector.cc"
  "${PROJECT_SOURCE_DIR}/utils/correlator.cc"
  "${PROJECT_SOURCE_DIR}/utils/trace_guard.cc"
  tool.cc)
target_include_directories(finetrace_tool
  PRIVATE "${PROJECT_SOURCE_DIR}"
  PRIVATE "${PROJECT_SOURCE_DIR}/utils"
  PRIVATE "${PROJECT_SOURCE_DIR}/utils"
  PRIVATE "${PROJECT_SOURCE_DIR}/cl_tracer"
  PRIVATE "${PROJECT_SOURCE_DIR}/ze_tracer")
target_compile_definitions(finetrace_tool PUBLIC FTRACE_LEVEL_ZERO=1)
if(CMAKE_INCLUDE_PATH)
  target_include_directories(finetrace_tool
    PUBLIC "${CMAKE_INCLUDE_PATH}")
endif()

FindOpenCLLibrary(finetrace_tool)
FindOpenCLHeaders(finetrace_tool)

GetOpenCLTracingHeaders(finetrace_tool)

FindL0Library(finetrace_tool)
FindL0Headers(finetrace_tool)

FindL0HeadersPath(finetrace_tool "${PROJECT_SOURCE_DIR}/ze_tracer/gen_tracing_callbacks.py")

# Loader

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTOOL_NAME=finetrace_tool")
add_executable(finetrace "${PROJECT_SOURCE_DIR}/loader/loader.cc")
target_include_directories(finetrace
  PRIVATE "${PROJECT_SOURCE_DIR}/utils")
if(UNIX)
  target_link_libraries(finetrace
    dl)
endif()

# Installation

install(TARGETS finetrace finetrace_tool DESTINATION bin)
