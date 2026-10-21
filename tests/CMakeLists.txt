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

# Semantic search. wordpiece_test and semantic_index_test need no model runtime.
tellmeutf_unit_test(wordpiece_test tellmeutf_semantic_base)
target_compile_definitions(wordpiece_test PRIVATE TELLMEUTF_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
tellmeutf_unit_test(semantic_index_test tellmeutf_semantic_base)
if(TELLMEUTF_SEMANTIC)
  # These need the files from scripts/fetch-semantic.sh and skip (status 77) without them.
  tellmeutf_unit_test(embedder_parity_test tellmeutf_semantic)
  target_compile_definitions(embedder_parity_test PRIVATE TELLMEUTF_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  tellmeutf_unit_test(semantic_e2e_test tellmeutf_semantic)
  set_tests_properties(embedder_parity_test semantic_e2e_test PROPERTIES SKIP_RETURN_CODE 77)
else()
  # The disabled facade is only linked when semantic search is off.
  tellmeutf_unit_test(semantic_disabled_test tellmeutf_semantic)
endif()
