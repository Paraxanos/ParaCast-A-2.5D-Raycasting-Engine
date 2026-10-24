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
  src/map/map.c
  src/map/loader.c
  src/render/fb.c
  src/render/raycast.c
  src/render/sector.c
  src/render/post/present.c
  src/debug/automap.c
  src/debug/clipview.c
  src/sim/player.c
  src/platform/file.c
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

# Tools and tests find the shipped maps here unless given an explicit path.
target_compile_definitions(paracast PUBLIC PC_MAPS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/maps")

if(NOT ENGINE_SDL STREQUAL "OFF")
  find_package(SDL2 CONFIG QUIET)
  if(TARGET SDL2::SDL2)
    set(PC_SDL_LIB SDL2::SDL2)
  else()
    find_path(SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2)
    find_library(SDL2_LIBRARY SDL2)
    if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
      add_library(pc_sdl2 INTERFACE)
      target_include_directories(pc_sdl2 SYSTEM INTERFACE ${SDL2_INCLUDE_DIR})
      target_link_libraries(pc_sdl2 INTERFACE ${SDL2_LIBRARY})
      set(PC_SDL_LIB pc_sdl2)
    endif()
  endif()
  if(NOT PC_SDL_LIB AND ENGINE_SDL STREQUAL "ON")
    message(FATAL_ERROR "ENGINE_SDL=ON but SDL2 was not found (apt install libsdl2-dev)")
  endif()
endif()

if(PC_SDL_LIB)
  message(STATUS "SDL2 viewer: enabled")
  add_library(paracast_sdl STATIC src/platform/sdl.c)
  target_link_libraries(paracast_sdl PUBLIC paracast ${PC_SDL_LIB})
  target_compile_options(paracast_sdl PRIVATE ${PC_WARNINGS} ${PC_SAN})

  add_executable(paracast_view tools/view.c)
  target_link_libraries(paracast_view PRIVATE paracast_sdl)
  target_compile_options(paracast_view PRIVATE ${PC_WARNINGS} ${PC_SAN})
else()
  message(STATUS "SDL2 viewer: disabled (SDL2 not found or ENGINE_SDL=OFF)")
endif()

if(ENGINE_TESTS)
  add_subdirectory(tests)
endif()
