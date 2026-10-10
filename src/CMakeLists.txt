# tellmeutf_ucd: lookup library over the generated tables.
include(ucd.cmake)

# tellmeutf_core: everything except main().
add_library(tellmeutf_core STATIC
  utf8.cpp
  query.cpp
  options.cpp
  render.cpp)
target_include_directories(tellmeutf_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tellmeutf_core PUBLIC tellmeutf_ucd PRIVATE tellmeutf_options)

add_executable(tellmeutf main.cpp)
target_compile_definitions(tellmeutf PRIVATE TELLMEUTF_VERSION="${PROJECT_VERSION}")
target_link_libraries(tellmeutf PRIVATE tellmeutf_core tellmeutf_options)

install(TARGETS tellmeutf RUNTIME DESTINATION bin)
