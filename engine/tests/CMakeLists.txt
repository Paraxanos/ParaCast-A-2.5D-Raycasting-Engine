foreach(t core render sim map sector)
  add_executable(test_${t} test_${t}.c)
  target_link_libraries(test_${t} PRIVATE paracast)
  target_compile_options(test_${t} PRIVATE ${PC_WARNINGS} ${PC_SAN})
  add_test(NAME ${t} COMMAND test_${t})
endforeach()

# Opens a real window on SDL's dummy video driver, renders and presents 30 frames, exits.
if(TARGET paracast_view)
  add_test(NAME view_smoke COMMAND paracast_view --frames 30 --novsync)
  set_tests_properties(view_smoke PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
endif()
