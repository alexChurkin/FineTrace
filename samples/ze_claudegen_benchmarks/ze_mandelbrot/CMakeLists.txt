include("../../../build_utils/CMakeLists.txt")
SetRequiredCMakeVersion()
cmake_minimum_required(VERSION ${REQUIRED_CMAKE_VERSION})

project(ZeClaudeGen_Mandelbrot CXX)
SetCompilerFlags()
SetBuildType()

add_executable(ze_mandelbrot main.cc)
target_include_directories(ze_mandelbrot
  PRIVATE "${PROJECT_SOURCE_DIR}/../../../src/utils")
if(CMAKE_INCLUDE_PATH)
  target_include_directories(ze_mandelbrot PUBLIC "${CMAKE_INCLUDE_PATH}")
endif()

add_custom_command(TARGET ze_mandelbrot PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${PROJECT_SOURCE_DIR}/mandelbrot.spv ${CMAKE_BINARY_DIR}/mandelbrot.spv)
add_custom_command(TARGET ze_mandelbrot PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${PROJECT_SOURCE_DIR}/mandelbrot.cl ${CMAKE_BINARY_DIR}/mandelbrot.cl)

FindL0Library(ze_mandelbrot)
FindL0Headers(ze_mandelbrot)

if(UNIX)
  target_link_libraries(ze_mandelbrot dl)
endif()
