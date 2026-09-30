# Stages the WCLAP bundle and its distributable archive.
#
#   DEST/            OB-Xd.wclap/{module.wasm, LICENSE}
#   ARCHIVE          OB-Xd.wclap.tar.gz, one top-level OB-Xd.wclap/ directory
#
# The archive is deterministic: sorted entries and a fixed mtime.

if(NOT DEFINED MODULE OR NOT EXISTS "${MODULE}")
  message(FATAL_ERROR "MODULE must name the built WCLAP module")
endif()
if(NOT DEFINED LICENSE OR NOT EXISTS "${LICENSE}")
  message(FATAL_ERROR "LICENSE must name the GPL license text")
endif()
if(NOT DEFINED DEST OR NOT DEFINED ARCHIVE)
  message(FATAL_ERROR "DEST and ARCHIVE must be set")
endif()

file(REMOVE_RECURSE "${DEST}")
get_filename_component(_archive_dir "${ARCHIVE}" DIRECTORY)
file(MAKE_DIRECTORY "${_archive_dir}")
file(MAKE_DIRECTORY "${DEST}")
file(COPY_FILE "${MODULE}" "${DEST}/module.wasm")
file(COPY_FILE "${LICENSE}" "${DEST}/LICENSE")

get_filename_component(_parent "${DEST}" DIRECTORY)
get_filename_component(_name "${DEST}" NAME)
file(REMOVE "${ARCHIVE}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar czf "${ARCHIVE}" --format=gnutar "--mtime=1980-01-01 00:00:00 UTC"
    "${_name}/LICENSE" "${_name}/module.wasm"
  WORKING_DIRECTORY "${_parent}"
  RESULT_VARIABLE _tar_result
)
if(NOT _tar_result EQUAL 0)
  message(FATAL_ERROR "Failed to write ${ARCHIVE}")
endif()
