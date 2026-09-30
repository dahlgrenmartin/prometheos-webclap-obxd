if(NOT DEFINED BUNDLE OR NOT IS_DIRECTORY "${BUNDLE}")
  message(FATAL_ERROR "BUNDLE must point to a staged .wclap directory")
endif()
if(NOT DEFINED OUTPUT)
  message(FATAL_ERROR "OUTPUT must name the checksum manifest")
endif()

file(GLOB_RECURSE _files
  LIST_DIRECTORIES FALSE
  RELATIVE "${BUNDLE}"
  "${BUNDLE}/*")
list(SORT _files)

set(_manifest "")
foreach(_relative IN LISTS _files)
  file(SHA256 "${BUNDLE}/${_relative}" _sha)
  string(APPEND _manifest "${_sha}  ${_relative}\n")
endforeach()

file(WRITE "${OUTPUT}" "${_manifest}")
message(STATUS "Wrote deterministic manifest: ${OUTPUT}")
