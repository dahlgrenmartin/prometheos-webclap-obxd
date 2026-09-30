cmake_minimum_required(VERSION 3.28)

project(prometheos_webclap_obxd VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_NATIVE_TESTS "Build native OB-Xd/CLAP unit tests" OFF)
enable_testing()

include(cmake/ObxdEngine.cmake)

add_test(NAME source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/source_contract_test.cmake"
)

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  include(cmake/wasi-sdk.cmake)

  add_executable(ObxdWclap ${OBXD_PLUGIN_SOURCES})
  configure_obxd_target(ObxdWclap)
  set_target_properties(ObxdWclap PROPERTIES
    OUTPUT_NAME module
    SUFFIX ".wasm"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/OB-Xd.wclap"
  )
  configure_wclap_reactor(ObxdWclap)

  set(OBXD_WCLAP_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist" CACHE PATH
      "Directory receiving the packaged WCLAP")
  set(_dist_bundle "${OBXD_WCLAP_DIST_DIR}/OB-Xd.wclap")
  add_custom_target(package_wclap
    COMMAND "${CMAKE_COMMAND}"
      "-DMODULE=$<TARGET_FILE:ObxdWclap>"
      "-DLICENSE=${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
      "-DDEST=${_dist_bundle}"
      "-DARCHIVE=${OBXD_WCLAP_DIST_DIR}/OB-Xd.wclap.tar.gz"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageWclap.cmake"
    COMMAND "${CMAKE_COMMAND}"
      "-DBUNDLE=${_dist_bundle}"
      "-DOUTPUT=${OBXD_WCLAP_DIST_DIR}/OB-Xd.wclap.sha256"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteManifest.cmake"
    DEPENDS ObxdWclap
    VERBATIM
  )

  add_test(NAME wasm_contract
    COMMAND node "${CMAKE_CURRENT_SOURCE_DIR}/tests/wasm_contract.test.mjs"
      "${_dist_bundle}/module.wasm"
  )
endif()

if(BUILD_NATIVE_TESTS)
  add_library(ObxdClapCore STATIC ${OBXD_PLUGIN_SOURCES})
  configure_obxd_target(ObxdClapCore)

  foreach(_test plugin_lifecycle params_state audio_process webview)
    add_executable(${_test}_test tests/${_test}_test.cpp)
    target_include_directories(${_test}_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(${_test}_test PRIVATE ObxdClapCore)
    add_test(NAME ${_test} COMMAND ${_test}_test)
  endforeach()
endif()
