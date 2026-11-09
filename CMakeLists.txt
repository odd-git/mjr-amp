cmake_minimum_required(VERSION 3.24)
project(MJRAmp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

# Eigen (inside NAM) picks its SIMD at compile time: AVX2+FMA speeds up the WaveNet a lot but needs a 2013+ x86 CPU.
# Set for the whole project, so every file sees the same Eigen alignment.
option(MJR_AVX2 "x86-64 builds: compile with AVX2+FMA" ON)
if(MJR_AVX2 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64" AND NOT CMAKE_OSX_ARCHITECTURES)
    add_compile_options("$<IF:$<CXX_COMPILER_ID:MSVC>,/arch:AVX2,-mavx2;-mfma>")
endif()

add_subdirectory(libs/JUCE)

# --- NAM core (MIT): built straight from sources, its own CMake only builds tools
set(NAM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/NeuralAmpModelerCore)
file(GLOB NAM_SOURCES ${NAM_DIR}/NAM/*.cpp ${NAM_DIR}/NAM/*/*.cpp)
add_library(nam STATIC ${NAM_SOURCES})
target_include_directories(nam SYSTEM PUBLIC
    ${NAM_DIR} ${NAM_DIR}/NAM ${NAM_DIR}/Dependencies/eigen ${NAM_DIR}/Dependencies/nlohmann
    ${NAM_DIR}/Dependencies/AudioDSPTools)
target_compile_definitions(nam PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST
    $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>)
set_target_properties(nam PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Plugin
juce_add_plugin(MJRAmp
    COMPANY_NAME "mjr-amp"
    PRODUCT_NAME "MJR Amp"
    PLUGIN_MANUFACTURER_CODE Mjra
    PLUGIN_CODE Mjr1
    FORMATS VST3 AU Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

# The amp and cab photos are not ours to publish: used only if present locally, otherwise a drawn panel.
set(MJR_DATA data/captures.json)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets-local/amp.png)
    list(APPEND MJR_DATA assets-local/amp.png)
    set(MJR_HAS_PHOTO 1)
else()
    set(MJR_HAS_PHOTO 0)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets-local/cab.png)
    list(APPEND MJR_DATA assets-local/cab.png)
    set(MJR_HAS_CAB 1)
else()
    set(MJR_HAS_CAB 0)
endif()
juce_add_binary_data(MJRData SOURCES ${MJR_DATA})
set_target_properties(MJRData PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(MJRAmp PRIVATE src/Processor.cpp src/Editor.cpp)

# Airwindows effects (MIT): the LinuxVST sources compile unchanged against libs/airwindows/audioeffectx.h.
# Every plugin defines createEffectInstance(); rename it per plugin so they can live in one binary.
foreach(fx Dynamics3 TapeDelay2 StereoChorus Galactic3 Doublelay)
    set(fx_sources libs/airwindows/${fx}/${fx}.cpp libs/airwindows/${fx}/${fx}Proc.cpp)
    target_sources(MJRAmp PRIVATE ${fx_sources})
    set_source_files_properties(${fx_sources} PROPERTIES
        COMPILE_DEFINITIONS "createEffectInstance=mjr_create_${fx};_USE_MATH_DEFINES"
        COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")
endforeach()
target_include_directories(MJRAmp SYSTEM PRIVATE libs/airwindows)

target_compile_definitions(MJRAmp PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 $<$<PLATFORM_ID:Linux>:JUCE_JACK=1> JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0 MJR_HAS_PHOTO=${MJR_HAS_PHOTO} MJR_HAS_CAB=${MJR_HAS_CAB})

target_link_libraries(MJRAmp
    PRIVATE MJRData juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags
    # NAM registers its architectures from static initialisers: keep every object.
           "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam>")

# --- Headless tests: mjr-test <library dir> <DI wav>
add_executable(mjr-test tests/test.cpp)
target_link_libraries(mjr-test PRIVATE MJRAmp)
target_include_directories(mjr-test PRIVATE $<TARGET_PROPERTY:MJRAmp,INCLUDE_DIRECTORIES>)
target_compile_definitions(mjr-test PRIVATE $<TARGET_PROPERTY:MJRAmp,COMPILE_DEFINITIONS>)
