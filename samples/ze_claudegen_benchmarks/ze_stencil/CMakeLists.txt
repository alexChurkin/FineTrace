include("../../../build_utils/CMakeLists.txt")
SetRequiredCMakeVersion()
cmake_minimum_required(VERSION ${REQUIRED_CMAKE_VERSION})

project(ZeClaudeGen_Stencil CXX)
SetCompilerFlags()
SetBuildType()

add_executable(ze_stencil main.cc)
target_include_directories(ze_stencil
  PRIVATE "${PROJECT_SOURCE_DIR}/../../../src/utils")
if(CMAKE_INCLUDE_PATH)
  target_include_directories(ze_stencil PUBLIC "${CMAKE_INCLUDE_PATH}")
endif()

add_custom_command(TARGET ze_stencil PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${PROJECT_SOURCE_DIR}/stencil.spv ${CMAKE_BINARY_DIR}/stencil.spv)
add_custom_command(TARGET ze_stencil PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${PROJECT_SOURCE_DIR}/stencil.cl ${CMAKE_BINARY_DIR}/stencil.cl)

FindL0Library(ze_stencil)
FindL0Headers(ze_stencil)

if(UNIX)
  target_link_libraries(ze_stencil dl)
endif()
