# The product is OB-Xd's engine behind the CLAP C ABI and nothing else: no
# JUCE, no CLAP helper framework, no Emscripten, no WebVST layer.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR must be set")
endif()

file(GLOB _sources "${SOURCE_DIR}/src/*.h" "${SOURCE_DIR}/src/*.cpp")
set(_forbidden
  "JuceHeader.h" "juce_audio" "PluginProcessor.h" "PluginEditor.h"
  "clap-helpers" "helpers/plugin.hh" "emscripten" "webvst" "WebVst")
foreach(_file IN LISTS _sources)
  file(READ "${_file}" _text)
  foreach(_token IN LISTS _forbidden)
    string(FIND "${_text}" "${_token}" _at)
    if(NOT _at EQUAL -1)
      message(FATAL_ERROR "${_file} references forbidden surface '${_token}'")
    endif()
  endforeach()
endforeach()

# The engine reaches JUCE through exactly one include, which the one local
# patch replaces with the shim.
file(GLOB _patches "${SOURCE_DIR}/patches/obxd/*.patch")
list(LENGTH _patches _patch_count)
if(NOT _patch_count EQUAL 1)
  message(FATAL_ERROR "Expected exactly one OB-Xd patch, found ${_patch_count}")
endif()
file(READ "${SOURCE_DIR}/patches/obxd/0001-engine-juce-free-include.patch" _patch)
string(FIND "${_patch}" "+#include <ObxdJuceShim.h>" _shim)
if(_shim EQUAL -1)
  message(FATAL_ERROR "The engine patch no longer routes SynthEngine.h through the shim")
endif()

# No engine header other than SynthEngine.h/midiMap.h pulls in JUCE directly.
file(GLOB _engine "${SOURCE_DIR}/vendor/obxd/Source/Engine/*.h")
foreach(_file IN LISTS _engine)
  get_filename_component(_name "${_file}" NAME)
  if(_name STREQUAL "midiMap.h")
    continue()
  endif()
  file(READ "${_file}" _text)
  string(FIND "${_text}" "JuceHeader.h" _juce)
  string(FIND "${_text}" "PluginProcessor.h" _processor)
  if(NOT _juce EQUAL -1 OR NOT _processor EQUAL -1)
    message(FATAL_ERROR "${_name} includes JUCE; the engine patch is not applied")
  endif()
endforeach()
message(STATUS "Source contract holds")
