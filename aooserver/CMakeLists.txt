# Cross-platform build for the headless AOO connection server.
#
# Mirrors Builds/LinuxMakefile (same sources, same defines) so the server can be
# built on macOS without Xcode, and on Linux without the Projucer makefile.
# Uses this directory's own vendored AOO (deps/aoo) and JUCE modules
# (JuceLibraryCode/modules) on purpose -- see the top-level README.
#
#   cmake -S aooserver -B build/aooserver -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/aooserver

cmake_minimum_required(VERSION 3.15)
project(aooserver VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (APPLE)
    enable_language(OBJCXX)
    set(JUCE_EXT mm)
else()
    set(JUCE_EXT cpp)
endif()

add_executable(aooserver
    deps/aoo/lib/src/client.cpp
    deps/aoo/lib/src/codec_pcm.cpp
    deps/aoo/lib/src/common.cpp
    deps/aoo/lib/src/net_utils.cpp
    deps/aoo/lib/src/server.cpp
    deps/aoo/lib/src/sink.cpp
    deps/aoo/lib/src/source.cpp
    deps/aoo/lib/src/sync.cpp
    deps/aoo/lib/src/time.cpp
    deps/aoo/deps/md5/md5.c
    deps/aoo/deps/oscpack/osc/OscOutboundPacketStream.cpp
    deps/aoo/deps/oscpack/osc/OscPrintReceivedElements.cpp
    deps/aoo/deps/oscpack/osc/OscReceivedElements.cpp
    deps/aoo/deps/oscpack/osc/OscTypes.cpp
    Source/Main.cpp
    JuceLibraryCode/include_juce_audio_basics.${JUCE_EXT}
    JuceLibraryCode/include_juce_audio_formats.${JUCE_EXT}
    JuceLibraryCode/include_juce_core.${JUCE_EXT}
    JuceLibraryCode/include_juce_cryptography.${JUCE_EXT}
    JuceLibraryCode/include_juce_data_structures.${JUCE_EXT}
    JuceLibraryCode/include_juce_events.${JUCE_EXT}
)

target_include_directories(aooserver PRIVATE
    JuceLibraryCode
    JuceLibraryCode/modules
    deps/aoo/lib
    deps/aoo/deps
)

target_compile_definitions(aooserver PRIVATE
    USE_CODEC_OPUS=0
    JUCE_APP_VERSION=1.0.0
    JUCE_APP_VERSION_HEX=0x10000
    JucePlugin_Build_VST=0
    JucePlugin_Build_VST3=0
    JucePlugin_Build_AU=0
    JucePlugin_Build_AUv3=0
    JucePlugin_Build_AAX=0
    JucePlugin_Build_Standalone=0
    JucePlugin_Build_Unity=0
    JucePlugin_Build_LV2=0
    $<IF:$<CONFIG:Debug>,DEBUG=1;_DEBUG=1;LOGLEVEL=3,NDEBUG=1>
)

find_package(Threads REQUIRED)
target_link_libraries(aooserver PRIVATE Threads::Threads)

if (APPLE)
    target_link_libraries(aooserver PRIVATE
        "-framework Accelerate"
        "-framework AppKit"
        "-framework AudioToolbox"
        "-framework CoreAudio"
        "-framework CoreFoundation"
        "-framework Foundation"
        "-framework IOKit"
        "-framework Security"
    )
elseif (UNIX)
    target_compile_definitions(aooserver PRIVATE LINUX=1)
    find_package(CURL REQUIRED)
    target_link_libraries(aooserver PRIVATE CURL::libcurl ${CMAKE_DL_LIBS} rt)
endif()
