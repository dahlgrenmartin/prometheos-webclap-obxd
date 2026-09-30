include_guard(GLOBAL)

# Applies a local patch to a pinned submodule exactly once: forward if it
# applies, accepted if it is already applied, fatal otherwise.
function(obxd_apply_patch_once repo patch)
  execute_process(
    COMMAND git apply --check --verbose "${patch}"
    WORKING_DIRECTORY "${repo}"
    RESULT_VARIABLE _can_apply
    OUTPUT_VARIABLE _forward_output
    ERROR_VARIABLE _forward_error
  )
  if(_can_apply EQUAL 0)
    execute_process(
      COMMAND git apply "${patch}"
      WORKING_DIRECTORY "${repo}"
      RESULT_VARIABLE _apply_result
    )
    if(NOT _apply_result EQUAL 0)
      message(FATAL_ERROR "Failed to apply ${patch}")
    endif()
    return()
  endif()

  execute_process(
    COMMAND git apply --reverse --check --verbose "${patch}"
    WORKING_DIRECTORY "${repo}"
    RESULT_VARIABLE _already_applied
    OUTPUT_VARIABLE _reverse_output
    ERROR_VARIABLE _reverse_error
  )
  if(NOT _already_applied EQUAL 0)
    message(FATAL_ERROR
      "Patch neither applies nor is already applied: ${patch}\n"
      "forward git apply: ${_forward_output}${_forward_error}\n"
      "reverse git apply: ${_reverse_output}${_reverse_error}")
  endif()
endfunction()

set(OBXD_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../vendor/obxd" CACHE INTERNAL "")
set(OBXD_CLAP_DIR "${CMAKE_CURRENT_LIST_DIR}/../vendor/clap" CACHE INTERNAL "")
get_filename_component(OBXD_SOURCE_DIR "${OBXD_SOURCE_DIR}" ABSOLUTE)
get_filename_component(OBXD_CLAP_DIR "${OBXD_CLAP_DIR}" ABSOLUTE)

if(NOT EXISTS "${OBXD_SOURCE_DIR}/Source/Engine/SynthEngine.h")
  message(FATAL_ERROR "OB-Xd submodule missing; run git submodule update --init vendor/obxd")
endif()
if(NOT EXISTS "${OBXD_CLAP_DIR}/include/clap/clap.h")
  message(FATAL_ERROR "CLAP submodule missing; run git submodule update --init vendor/clap")
endif()

obxd_apply_patch_once("${OBXD_SOURCE_DIR}"
  "${CMAKE_CURRENT_LIST_DIR}/../patches/obxd/0001-engine-juce-free-include.patch")

# The editor's files, compiled into the module.
set(OBXD_WEBUI_DIR "${CMAKE_CURRENT_LIST_DIR}/../webui")
get_filename_component(OBXD_WEBUI_DIR "${OBXD_WEBUI_DIR}" ABSOLUTE)
file(GLOB_RECURSE OBXD_WEBUI_FILES CONFIGURE_DEPENDS "${OBXD_WEBUI_DIR}/*")
set(OBXD_WEBUI_SOURCE "${CMAKE_BINARY_DIR}/generated/ObxdWebUiResources.cpp")
add_custom_command(
  OUTPUT "${OBXD_WEBUI_SOURCE}"
  COMMAND "${CMAKE_COMMAND}"
    "-DWEBUI_DIR=${OBXD_WEBUI_DIR}"
    "-DOUTPUT=${OBXD_WEBUI_SOURCE}"
    -P "${CMAKE_CURRENT_LIST_DIR}/EmbedWebUi.cmake"
  DEPENDS ${OBXD_WEBUI_FILES} "${CMAKE_CURRENT_LIST_DIR}/EmbedWebUi.cmake"
  COMMENT "Embedding the OB-Xd web editor"
  VERBATIM
)

set(OBXD_PLUGIN_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/../src/ObxdClapPlugin.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/ObxdParameters.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/ObxdState.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/MtsEspUnavailable.cpp"
  "${OBXD_WEBUI_SOURCE}"
)

function(configure_obxd_target target)
  target_include_directories(${target} PUBLIC
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src"
    "${OBXD_CLAP_DIR}/include"
    "${OBXD_SOURCE_DIR}/Source"
  )
  # The pinned OB-Xd engine predates modern warning sets; keep our own code
  # strict and the vendored headers quiet.
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter
    -Wno-sign-compare -Wno-unused-variable -Wno-unused-but-set-variable
    -Wno-reorder -Wno-misleading-indentation -Wno-implicit-float-conversion
    -Wno-unknown-warning-option -Wno-unused-private-field)
endfunction()
