execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE PC_GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT PC_GIT_HASH)
  set(PC_GIT_HASH unknown)
endif()

set(PC_WARNINGS -Wall -Wextra -Wpedantic -Werror)
if(ENGINE_SANITIZE)
  set(PC_SAN -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

add_library(paracast STATIC
  src/core/arena.c
  src/core/hash.c
  src/core/lut.c
  src/assets/assets.c
  src/map/gridmap.c
  src/render/fb.c
  src/render/raycast.c
  src/render/post/present.c
  src/platform/ppm.c
  src/platform/timer.c)
target_include_directories(paracast PUBLIC include src)
target_compile_options(paracast PRIVATE ${PC_WARNINGS} ${PC_SAN})
target_link_libraries(paracast PUBLIC m ${PC_SAN})

if(ENGINE_OPENMP)
  find_package(OpenMP REQUIRED COMPONENTS C)
  target_link_libraries(paracast PUBLIC OpenMP::OpenMP_C)
endif()

foreach(tool demo bench)
  add_executable(paracast_${tool} tools/${tool}.c)
  target_link_libraries(paracast_${tool} PRIVATE paracast)
  target_compile_options(paracast_${tool} PRIVATE ${PC_WARNINGS} ${PC_SAN})
endforeach()
target_compile_definitions(paracast_bench PRIVATE
  PC_GIT_HASH="${PC_GIT_HASH}" PC_BUILD_TYPE="${CMAKE_BUILD_TYPE}")

if(ENGINE_TESTS)
  add_subdirectory(tests)
endif()
