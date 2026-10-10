# tellmeutf_unit_test(<name> <libraries...>): builds <name>.cpp and registers it with CTest.
function(tellmeutf_unit_test name)
  add_executable(${name} ${name}.cpp)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE ${ARGN} tellmeutf_options)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

tellmeutf_unit_test(ucd_test tellmeutf_ucd)
tellmeutf_unit_test(fuzzy_test tellmeutf_ucd)
tellmeutf_unit_test(utf8_test tellmeutf_core)
tellmeutf_unit_test(query_test tellmeutf_core)
tellmeutf_unit_test(options_test tellmeutf_core)
tellmeutf_unit_test(render_test tellmeutf_core)

add_test(NAME e2e COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/e2e.sh $<TARGET_FILE:tellmeutf>)
