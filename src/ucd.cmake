# tellmeutf_ucd: lookup library over tables generated at build time.

# Verifies vendored data files against the SHA256SUMS file of their directory
# ("<hash>  <path relative to the directory>" lines, the sha256sum format) and
# re-runs configure when the checksum list or a file changes.
#   verify_vendored_data(<data directory> <update command> <relative file>...)
function(verify_vendored_data data_dir update_command)
  cmake_path(RELATIVE_PATH data_dir BASE_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE data_label)
  set(sums_file ${data_dir}/SHA256SUMS)
  set(inputs "")
  foreach(name IN LISTS ARGN)
    list(APPEND inputs ${data_dir}/${name})
  endforeach()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${sums_file} ${inputs})
  file(STRINGS ${sums_file} sum_lines)
  foreach(name IN LISTS ARGN)
    set(expected "")
    foreach(sum_line IN LISTS sum_lines)
      if(sum_line MATCHES "^([0-9a-fA-F]+)[ ]+[*]?${name}$")
        string(TOLOWER "${CMAKE_MATCH_1}" expected)
      endif()
    endforeach()
    if(expected STREQUAL "")
      message(FATAL_ERROR "${data_label}/SHA256SUMS has no checksum for ${name}; run ${update_command} to regenerate it.")
    endif()
    file(SHA256 ${data_dir}/${name} actual)
    if(NOT actual STREQUAL expected)
      message(FATAL_ERROR "${data_label}/${name} does not match ${data_label}/SHA256SUMS (expected ${expected}, found ${actual}); restore the file or run ${update_command}.")
    endif()
  endforeach()
endfunction()

set(TELLMEUTF_UCD_DATA_DIR ${PROJECT_SOURCE_DIR}/data/ucd)
set(TELLMEUTF_CLDR_DATA_DIR ${PROJECT_SOURCE_DIR}/data/cldr)
verify_vendored_data(${TELLMEUTF_UCD_DATA_DIR} "scripts/update-ucd.sh <version>"
  UnicodeData.txt NameAliases.txt EastAsianWidth.txt)
verify_vendored_data(${TELLMEUTF_CLDR_DATA_DIR} "scripts/update-cldr.sh <release-tag>"
  annotations/en.xml)
set(TELLMEUTF_UCD_INPUTS
  ${TELLMEUTF_UCD_DATA_DIR}/UnicodeData.txt
  ${TELLMEUTF_UCD_DATA_DIR}/NameAliases.txt
  ${TELLMEUTF_UCD_DATA_DIR}/EastAsianWidth.txt
  ${TELLMEUTF_CLDR_DATA_DIR}/annotations/en.xml)

set(TELLMEUTF_UCD_TABLES ${CMAKE_CURRENT_BINARY_DIR}/ucd_tables.cpp)

add_custom_command(
  OUTPUT ${TELLMEUTF_UCD_TABLES}
  COMMAND gen_ucd ${TELLMEUTF_UCD_INPUTS} -o ${TELLMEUTF_UCD_TABLES}
  DEPENDS gen_ucd ${TELLMEUTF_UCD_INPUTS}
  COMMENT "Generating Unicode tables"
  VERBATIM)

add_library(tellmeutf_ucd STATIC ucd.cpp fuzzy.cpp ${TELLMEUTF_UCD_TABLES})
target_include_directories(tellmeutf_ucd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tellmeutf_ucd PRIVATE tellmeutf_options)
