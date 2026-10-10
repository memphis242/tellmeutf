# Semantic search (docs/design.md, "Semantic search").
#
# tellmeutf_semantic_base: pieces without any model runtime (tokenizer, index,
# paths). Always built; the tests use it directly.
add_library(tellmeutf_semantic_base STATIC
  wordpiece.cpp
  semantic_paths.cpp
  semantic_index.cpp)
target_include_directories(tellmeutf_semantic_base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tellmeutf_semantic_base PRIVATE tellmeutf_options)

# tellmeutf_semantic: the facade declared in semantic.hpp.
if(TELLMEUTF_SEMANTIC)
  set(ONNXRUNTIME_ROOT "" CACHE PATH
    "Unpacked ONNX Runtime release (the directory holding include/ and lib/); scripts/fetch-semantic.sh prepares one")
  if(ONNXRUNTIME_ROOT STREQUAL "")
    if(DEFINED ENV{XDG_DATA_HOME} AND NOT "$ENV{XDG_DATA_HOME}" STREQUAL "")
      set(tellmeutf_default_ort "$ENV{XDG_DATA_HOME}/tellmeutf/semantic/onnxruntime")
    else()
      set(tellmeutf_default_ort "$ENV{HOME}/.local/share/tellmeutf/semantic/onnxruntime")
    endif()
  else()
    set(tellmeutf_default_ort "${ONNXRUNTIME_ROOT}")
  endif()
  # The CMake and pkg-config files inside the release archive point at paths
  # that do not exist in it, so the header and library are located directly.
  find_path(ort_include onnxruntime_cxx_api.h
    HINTS ${tellmeutf_default_ort}/include NO_DEFAULT_PATH NO_CACHE)
  find_library(ort_library onnxruntime
    HINTS ${tellmeutf_default_ort}/lib NO_DEFAULT_PATH NO_CACHE)
  if(NOT ort_include OR NOT ort_library)
    message(FATAL_ERROR
      "TELLMEUTF_SEMANTIC=ON needs ONNX Runtime, but include/onnxruntime_cxx_api.h and lib/libonnxruntime.so were not "
      "found under ${tellmeutf_default_ort}. Run scripts/fetch-semantic.sh and pass the "
      "-DONNXRUNTIME_ROOT=<dir>/onnxruntime it prints, or point ONNXRUNTIME_ROOT at an unpacked "
      "onnxruntime-linux-* release.")
  endif()
  get_filename_component(TELLMEUTF_ORT_LIB_DIR ${ort_library} DIRECTORY)

  add_library(tellmeutf_onnxruntime SHARED IMPORTED)
  set_target_properties(tellmeutf_onnxruntime PROPERTIES IMPORTED_LOCATION ${ort_library})
  # SYSTEM keeps the strict project warnings off the vendor headers.
  target_include_directories(tellmeutf_onnxruntime SYSTEM INTERFACE ${ort_include})

  # Executables find libonnxruntime.so next to where the fetch script put it,
  # in the build tree and after install.
  set(CMAKE_BUILD_RPATH ${TELLMEUTF_ORT_LIB_DIR})
  set(CMAKE_INSTALL_RPATH ${TELLMEUTF_ORT_LIB_DIR})

  add_library(tellmeutf_semantic STATIC semantic.cpp semantic_embedder.cpp)
  target_link_libraries(tellmeutf_semantic
    PUBLIC tellmeutf_semantic_base
    PRIVATE tellmeutf_ucd tellmeutf_onnxruntime tellmeutf_options)
else()
  add_library(tellmeutf_semantic STATIC semantic_disabled.cpp)
  target_link_libraries(tellmeutf_semantic PUBLIC tellmeutf_semantic_base PRIVATE tellmeutf_options)
endif()
