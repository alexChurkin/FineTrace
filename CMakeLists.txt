include("./build_utils/CMakeLists.txt")
SetRequiredCMakeVersion()
cmake_minimum_required(VERSION ${REQUIRED_CMAKE_VERSION})

project(FineTrace CXX)
SetCompilerFlags()
SetBuildType()

# Tool Library

add_library(finetrace_tool SHARED
  "${PROJECT_SOURCE_DIR}/src/loader/init.cc"
  "${PROJECT_SOURCE_DIR}/src/collectors/cl_collector/cl_ext_collector.cc"
  "${PROJECT_SOURCE_DIR}/src/utils/correlator.cc"
  "${PROJECT_SOURCE_DIR}/src/utils/trace_guard.cc"
  src/tool.cc)
target_include_directories(finetrace_tool
  PRIVATE "${PROJECT_SOURCE_DIR}/src"
  PRIVATE "${PROJECT_SOURCE_DIR}/src/utils"
  PRIVATE "${PROJECT_SOURCE_DIR}/src/collectors/cl_collector"
  PRIVATE "${PROJECT_SOURCE_DIR}/src/collectors/ze_collector"
  PRIVATE "${PROJECT_SOURCE_DIR}/src/collectors/metric_collector")
target_compile_definitions(finetrace_tool
  PUBLIC FTRACE_LEVEL_ZERO=1
  PUBLIC FTRACE_KERNEL_INTERVALS=1)
if(CMAKE_INCLUDE_PATH)
  target_include_directories(finetrace_tool
    PUBLIC "${CMAKE_INCLUDE_PATH}")
endif()

FindOpenCLLibrary(finetrace_tool)
FindOpenCLHeaders(finetrace_tool)

GetOpenCLTracingHeaders(finetrace_tool)

FindL0Library(finetrace_tool)
FindL0Headers(finetrace_tool)

FindL0HeadersPath(finetrace_tool "${PROJECT_SOURCE_DIR}/src/collectors/ze_collector/gen_tracing_callbacks.py")

# Loader

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTOOL_NAME=finetrace_tool")
add_executable(finetrace "${PROJECT_SOURCE_DIR}/src/loader/loader.cc")
target_include_directories(finetrace
  PRIVATE "${PROJECT_SOURCE_DIR}/src/utils")
if(UNIX)
  target_link_libraries(finetrace
    dl)
endif()

# Installation

install(TARGETS finetrace finetrace_tool DESTINATION bin)
