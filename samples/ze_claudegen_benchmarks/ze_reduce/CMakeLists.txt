include("../../../build_utils/CMakeLists.txt")
SetRequiredCMakeVersion()
cmake_minimum_required(VERSION ${REQUIRED_CMAKE_VERSION})

project(ZeClaudeGen_Reduce CXX)
SetCompilerFlags()
SetBuildType()

add_executable(ze_reduce main.cc)
target_include_directories(ze_reduce
  PRIVATE "${PROJECT_SOURCE_DIR}/../../../src/utils")
if(CMAKE_INCLUDE_PATH)
  target_include_directories(ze_reduce PUBLIC "${CMAKE_INCLUDE_PATH}")
endif()

add_custom_command(TARGET ze_reduce PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${PROJECT_SOURCE_DIR}/reduce.spv ${CMAKE_BINARY_DIR}/reduce.spv)
add_custom_command(TARGET ze_reduce PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${PROJECT_SOURCE_DIR}/reduce.cl ${CMAKE_BINARY_DIR}/reduce.cl)

FindL0Library(ze_reduce)
FindL0Headers(ze_reduce)

if(UNIX)
  target_link_libraries(ze_reduce dl)
endif()
