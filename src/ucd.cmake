# tellmeutf_ucd: lookup library over tables generated at build time.
set(TELLMEUTF_UCD_DATA_DIR ${PROJECT_SOURCE_DIR}/data/ucd)
set(TELLMEUTF_UCD_INPUTS
  ${TELLMEUTF_UCD_DATA_DIR}/UnicodeData.txt
  ${TELLMEUTF_UCD_DATA_DIR}/NameAliases.txt
  ${TELLMEUTF_UCD_DATA_DIR}/EastAsianWidth.txt)
# The vendored files must match data/ucd/SHA256SUMS ("<hash>  <name>" lines, the
# sha256sum format); a change to the checksum list re-runs configure.
set(TELLMEUTF_UCD_SUMS ${TELLMEUTF_UCD_DATA_DIR}/SHA256SUMS)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TELLMEUTF_UCD_SUMS} ${TELLMEUTF_UCD_INPUTS})
file(STRINGS ${TELLMEUTF_UCD_SUMS} TELLMEUTF_UCD_SUM_LINES)
foreach(ucd_input IN LISTS TELLMEUTF_UCD_INPUTS)
  get_filename_component(ucd_name ${ucd_input} NAME)
  set(ucd_expected "")
  foreach(ucd_line IN LISTS TELLMEUTF_UCD_SUM_LINES)
    if(ucd_line MATCHES "^([0-9a-fA-F]+)[ ]+[*]?${ucd_name}$")
      string(TOLOWER "${CMAKE_MATCH_1}" ucd_expected)
    endif()
  endforeach()
  if(ucd_expected STREQUAL "")
    message(FATAL_ERROR "data/ucd/SHA256SUMS has no checksum for ${ucd_name}; run scripts/update-ucd.sh <version> to regenerate it.")
  endif()
  file(SHA256 ${ucd_input} ucd_actual)
  if(NOT ucd_actual STREQUAL ucd_expected)
    message(FATAL_ERROR "data/ucd/${ucd_name} does not match data/ucd/SHA256SUMS (expected ${ucd_expected}, found ${ucd_actual}); restore the file or run scripts/update-ucd.sh <version>.")
  endif()
endforeach()

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
