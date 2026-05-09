include("../../../build_utils/CMakeLists.txt")
SetRequiredCMakeVersion()
cmake_minimum_required(VERSION ${REQUIRED_CMAKE_VERSION})

project(ZeClaudeGen_VecAdd CXX)
SetCompilerFlags()
SetBuildType()

add_executable(ze_vecadd main.cc)
target_include_directories(ze_vecadd
  PRIVATE "${PROJECT_SOURCE_DIR}/../../../src/utils")
if(CMAKE_INCLUDE_PATH)
  target_include_directories(ze_vecadd PUBLIC "${CMAKE_INCLUDE_PATH}")
endif()

add_custom_command(TARGET ze_vecadd PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${PROJECT_SOURCE_DIR}/vecadd.spv ${CMAKE_BINARY_DIR}/vecadd.spv)
add_custom_command(TARGET ze_vecadd PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${PROJECT_SOURCE_DIR}/vecadd.cl ${CMAKE_BINARY_DIR}/vecadd.cl)

FindL0Library(ze_vecadd)
FindL0Headers(ze_vecadd)

if(UNIX)
  target_link_libraries(ze_vecadd dl)
endif()
