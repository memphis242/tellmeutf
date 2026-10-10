# Placeholder: replaced by the generator rule and the tellmeutf_ucd target.
add_library(tellmeutf_ucd STATIC ucd.cpp)
target_include_directories(tellmeutf_ucd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tellmeutf_ucd PRIVATE tellmeutf_options)
