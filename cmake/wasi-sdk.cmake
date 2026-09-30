include_guard(GLOBAL)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
  message(FATAL_ERROR "The WCLAP product target requires the wasi-sdk WASI toolchain")
endif()

if(NOT DEFINED ENV{WASI_SDK_PATH} AND NOT DEFINED WASI_SDK_PATH)
  message(FATAL_ERROR "Set WASI_SDK_PATH to the extracted wasi-sdk-34 directory")
endif()

if(NOT WASI_SDK_PATH)
  set(WASI_SDK_PATH "$ENV{WASI_SDK_PATH}")
endif()

get_filename_component(WASI_SDK_PATH "${WASI_SDK_PATH}" ABSOLUTE)
if(NOT EXISTS "${WASI_SDK_PATH}/bin/clang++")
  message(FATAL_ERROR "Invalid WASI_SDK_PATH: ${WASI_SDK_PATH}")
endif()

set(_wasi_lock "${CMAKE_CURRENT_LIST_DIR}/../toolchains/wasi-sdk.lock")
if(NOT EXISTS "${_wasi_lock}")
  message(FATAL_ERROR "Missing wasi-sdk lock file: ${_wasi_lock}")
endif()

file(STRINGS "${_wasi_lock}" _wasi_lock_version REGEX "^version=wasi-sdk-[0-9]+$")
if(NOT _wasi_lock_version)
  message(FATAL_ERROR "Malformed wasi-sdk lock file: missing version=wasi-sdk-N")
endif()
list(GET _wasi_lock_version 0 _wasi_lock_version)
string(REGEX REPLACE "^version=wasi-sdk-([0-9]+)$" "\\1"
  _expected_major "${_wasi_lock_version}")

set(_wasi_version_file "${WASI_SDK_PATH}/VERSION")
if(NOT EXISTS "${_wasi_version_file}")
  message(FATAL_ERROR "Invalid wasi-sdk install: missing ${_wasi_version_file}")
endif()
file(STRINGS "${_wasi_version_file}" _wasi_installed_version LIMIT_COUNT 1)
if(NOT _wasi_installed_version MATCHES "^${_expected_major}(\\.|$)")
  message(FATAL_ERROR
    "wasi-sdk version mismatch: lock requires ${_expected_major}.x, "
    "install reports '${_wasi_installed_version}'")
endif()

# OB-Xd needs neither C++ exceptions nor threads, so the module is built
# without wasm exception handling: hosts need no exnref support to load it.
function(configure_wclap_reactor target)
  target_compile_options(${target} PRIVATE
    -fno-exceptions
    -fno-rtti
    "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=/pkg"
    "-ffile-prefix-map=${CMAKE_BINARY_DIR}=/build"
  )

  target_link_options(${target} PRIVATE
    -mexec-model=reactor
    "LINKER:--no-entry"
    "LINKER:--export=clap_entry"
    "LINKER:--export=malloc"
    "LINKER:--export=free"
    "LINKER:--export-table"
    "LINKER:--growable-table"
    "LINKER:--export-memory"
    "LINKER:--initial-memory=16777216"
    "LINKER:--max-memory=268435456"
    "LINKER:-z,stack-size=1048576"
  )
endfunction()
