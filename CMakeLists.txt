cmake_minimum_required(VERSION 3.22)
project(BeadsPlugin VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  # Native Apple silicon by default; uncomment for a universal binary.
  # set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Plugin formats: AU + VST3 (+ Standalone for quick testing).
set(FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND FORMATS AU)
endif()

juce_add_plugin(Beads
  COMPANY_NAME "particledots"
  PLUGIN_MANUFACTURER_CODE Ptdt
  PLUGIN_CODE Bead
  FORMATS ${FORMATS}
  PRODUCT_NAME "Unofficial Beads"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicEffect   # lets Logic route MIDI to it as a "MIDI-controlled effect"
  COPY_PLUGIN_AFTER_BUILD TRUE)

# --- DSP core: vendored Beads/stmlib code + our JUCE-free wrapper, built with plain flags ---
add_library(beads_core STATIC
  src/BeadsEngine.cpp
  src/WavetableLoader.cpp
  third_party/beads/dsp/processor.cc
  third_party/beads/dsp/synth.cc
  third_party/beads/dsp/mu_law.cc
  third_party/beads/dsp/delay_clock.cc
  third_party/beads/dsp/delay_controller.cc
  third_party/beads/dsp/trigger_generator.cc
  third_party/beads/resources.cc
  third_party/stmlib/utils/random.cc
  third_party/stmlib/dsp/units.cc)
target_include_directories(beads_core PUBLIC src third_party)
target_compile_definitions(beads_core PRIVATE TEST=1)
set_target_properties(beads_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Some upstream headers rely on <cstdio>/<cstdlib>/<cmath> being pulled in transitively.
if(MSVC)
  target_compile_options(beads_core PRIVATE /FIcstdio /FIcstdlib /FIcmath)
else()
  target_compile_options(beads_core PRIVATE "SHELL:-include cstdio" "SHELL:-include cstdlib" "SHELL:-include cmath" -Wno-unused-local-typedef)
endif()

# --- The plugin ---
target_sources(Beads PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp)
target_compile_definitions(Beads PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Beads PRIVATE
  beads_core
  juce::juce_audio_utils
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

# --- Headless test of the DSP wrapper:  cmake --build build --target engine_test && build/engine_test ---
add_executable(engine_test EXCLUDE_FROM_ALL src/engine_test.cpp)
target_link_libraries(engine_test PRIVATE beads_core)
target_compile_definitions(engine_test PRIVATE BEADS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
