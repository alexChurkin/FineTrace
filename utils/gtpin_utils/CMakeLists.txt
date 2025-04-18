set(FTRACE_GTPIN_TOOL_BASE_DIR ${CMAKE_CURRENT_LIST_DIR} CACHE INTERNAL "")

include("${CMAKE_CURRENT_LIST_DIR}/../../build_utils/CMakeLists.txt")
SetRequiredCMakeVersion()
cmake_minimum_required(VERSION ${REQUIRED_CMAKE_VERSION})

macro(FindGTPinToolUtilsHeaders TARGET)
  target_include_directories(${TARGET}
    PUBLIC "${FTRACE_GTPIN_TOOL_BASE_DIR}/include")
endmacro()

project(FTRACE_GTPin_tool_utils CXX)
SetCompilerFlags()
SetBuildType()

file(GLOB CAPSULE_MACROS "${FTRACE_GTPIN_TOOL_BASE_DIR}/src/macro/*.cpp")

add_library(gtpin_tool_utils STATIC
  "${FTRACE_GTPIN_TOOL_BASE_DIR}/src/profiler.cpp"
  "${FTRACE_GTPIN_TOOL_BASE_DIR}/src/control.cpp"
  "${FTRACE_GTPIN_TOOL_BASE_DIR}/src/tool.cpp" 
  "${FTRACE_GTPIN_TOOL_BASE_DIR}/src/capsule.cpp"
  ${CAPSULE_MACROS}
  "${FTRACE_GTPIN_TOOL_BASE_DIR}/src/results.cpp"
  "${FTRACE_GTPIN_TOOL_BASE_DIR}/src/tool_factory.cpp"
  "${FTRACE_GTPIN_TOOL_BASE_DIR}/src/writer.cpp"
  )
FindGTPinToolUtilsHeaders(gtpin_tool_utils)
set_property(TARGET gtpin_tool_utils PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(gtpin_tool_utils
  PRIVATE "${FTRACE_GTPIN_TOOL_BASE_DIR}/../../sdk/src/utils")
if(CMAKE_INCLUDE_PATH)
  target_include_directories(gtpin_tool_utils
    PUBLIC "${CMAKE_INCLUDE_PATH}")
endif()

FindGTPinLibrary(gtpin_tool_utils)
FindGTPinHeaders(gtpin_tool_utils)
FindGTPinUtils(gtpin_tool_utils)
