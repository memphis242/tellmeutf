# tellmeutf_ucd: lookup library over tables generated at build time.
set(TELLMEUTF_UCD_DATA_DIR ${PROJECT_SOURCE_DIR}/data/ucd)
set(TELLMEUTF_UCD_INPUTS
  ${TELLMEUTF_UCD_DATA_DIR}/UnicodeData.txt
  ${TELLMEUTF_UCD_DATA_DIR}/NameAliases.txt
  ${TELLMEUTF_UCD_DATA_DIR}/EastAsianWidth.txt)
set(TELLMEUTF_UCD_TABLES ${CMAKE_CURRENT_BINARY_DIR}/ucd_tables.cpp)

add_custom_command(
  OUTPUT ${TELLMEUTF_UCD_TABLES}
  COMMAND gen_ucd ${TELLMEUTF_UCD_INPUTS} -o ${TELLMEUTF_UCD_TABLES}
  DEPENDS gen_ucd ${TELLMEUTF_UCD_INPUTS}
  COMMENT "Generating Unicode tables"
  VERBATIM)

add_library(tellmeutf_ucd STATIC ucd.cpp ${TELLMEUTF_UCD_TABLES})
target_include_directories(tellmeutf_ucd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tellmeutf_ucd PRIVATE tellmeutf_options)
