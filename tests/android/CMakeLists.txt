cmake_minimum_required(VERSION 3.15)
project(a6check VERSION 0.1 LANGUAGES CXX)

# A6: compile the bundled-UI extractor against a desktop JUCE and the real
# crosspoint-ui.zip, then assert the unpack + stamp behaviour on the host
# (the Android extractor itself cannot run here).
set(CMAKE_CXX_STANDARD 17)

add_subdirectory(../../juce juce EXCLUDE_FROM_ALL)

juce_add_console_app(a6check PRODUCT_NAME "a6check")
juce_add_binary_data(a6data SOURCES ../../sonobus/Resources/crosspoint-ui.zip)

target_sources(a6check PRIVATE a6-ui-extract.cpp)
target_link_libraries(a6check PRIVATE a6data)
target_include_directories(a6check PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/juce_binarydata_a6data/JuceLibraryCode")

target_compile_definitions(a6check PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(a6check PRIVATE
    juce::juce_core
    juce::juce_cryptography
    juce::juce_events
    juce::juce_gui_basics
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
