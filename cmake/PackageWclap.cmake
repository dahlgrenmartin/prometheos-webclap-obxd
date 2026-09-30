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
# GNU tar + gzip -n so the archive depends only on the bundle's bytes: no
# builder uid, umask, file times or gzip header timestamp leak into it.
find_program(_tar tar REQUIRED)
find_program(_gzip gzip REQUIRED)
execute_process(
  COMMAND "${_tar}" --sort=name --owner=0 --group=0 --numeric-owner
    --mode=u=rw,go=r --mtime=@315532800 --format=gnu
    -cf - "${_name}/LICENSE" "${_name}/module.wasm"
  COMMAND "${_gzip}" -n -9
  WORKING_DIRECTORY "${_parent}"
  OUTPUT_FILE "${ARCHIVE}"
  RESULTS_VARIABLE _tar_results
)
set(_tar_result 0)
foreach(_result IN LISTS _tar_results)
  if(NOT _result EQUAL 0)
    set(_tar_result "${_result}")
  endif()
endforeach()
if(NOT _tar_result EQUAL 0)
  message(FATAL_ERROR "Failed to write ${ARCHIVE}")
endif()
