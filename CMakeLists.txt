cmake_minimum_required(VERSION 3.16)
project(Paracast LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ENGINE_OPENMP   "Link OpenMP (used from Phase 3 on)" OFF)
option(ENGINE_SANITIZE "Build with ASan + UBSan"            OFF)
set(ENGINE_SDL AUTO CACHE STRING "Build the SDL2 viewer: AUTO, ON or OFF")
set_property(CACHE ENGINE_SDL PROPERTY STRINGS AUTO ON OFF)
option(ENGINE_TESTS   "Build the test suite"               ON)

if(ENGINE_TESTS)
  enable_testing()
endif()

add_subdirectory(engine)
