# gen_ucd: build-time generator, UCD text files -> C++ tables.
add_executable(gen_ucd gen_ucd.cpp)
target_link_libraries(gen_ucd PRIVATE tellmeutf_options)
