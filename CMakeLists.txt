cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")  # universal: Live 10 is Intel-only
project(ElukkaSynth VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: link the C++ runtime statically so the plugin does not depend on a VC++ redistributable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Copy the built plugin into the system VST3 folder. Default on for local macOS builds (as before);
# off elsewhere, since on Windows the folder needs admin rights and CI builds only package the result.
if(APPLE)
    set(ELUKKA_COPY_PLUGIN_DEFAULT ON)
else()
    set(ELUKKA_COPY_PLUGIN_DEFAULT OFF)
endif()
option(ELUKKA_COPY_PLUGIN_AFTER_BUILD "Copy the VST3 into the user's plugin folder after building" ${ELUKKA_COPY_PLUGIN_DEFAULT})

add_subdirectory(JUCE)

juce_add_plugin(ElukkaSynth
    COMPANY_NAME "Elukka"
    PRODUCT_NAME "ElukkaSynth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${ELUKKA_COPY_PLUGIN_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Elkk
    PLUGIN_CODE Elks
    VST3_CATEGORIES Instrument Synth
    FORMATS VST3)

juce_generate_juce_header(ElukkaSynth)

juce_add_binary_data(ElukkaSynthData SOURCES Resources/logo.jpg)

set(ELUKKA_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Presets.cpp
    Source/DSP/SpectralOsc.cpp
    Source/DSP/Resynth.cpp
    Source/DSP/ResynthAnalysis.cpp
    Source/DSP/PartialBank.cpp
    Source/DSP/Voice.cpp
    Source/DSP/Sequencer.cpp
    Source/UI/Pages.cpp)
if(APPLE)
    list(APPEND ELUKKA_SOURCES Source/UI/HostWindow.mm)
endif()

target_sources(ElukkaSynth PRIVATE ${ELUKKA_SOURCES})

target_compile_definitions(ElukkaSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
if(UNIX AND NOT APPLE)
    # macOS and Windows decode MP3 through OS codecs; Linux has none, so use JUCE's built-in decoder.
    target_compile_definitions(ElukkaSynth PUBLIC JUCE_USE_MP3AUDIOFORMAT=1)
endif()

target_link_libraries(ElukkaSynth
    PRIVATE
        ElukkaSynthData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Developer-only test harness: renders every factory preset offline and checks the output.
option(ELUKKA_BUILD_TESTS "Build the offline render test" ON)
if(ELUKKA_BUILD_TESTS)
    juce_add_console_app(ElukkaTest PRODUCT_NAME "ElukkaTest")
    juce_generate_juce_header(ElukkaTest)
    target_sources(ElukkaTest PRIVATE Tests/RenderTest.cpp ${ELUKKA_SOURCES})
    target_compile_definitions(ElukkaTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="ElukkaSynth"
        JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0
        JucePlugin_IsMidiEffect=0 JucePlugin_EditorRequiresKeyboardFocus=0)
    target_link_libraries(ElukkaTest PRIVATE
        ElukkaSynthData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
    if(MSVC)
        # The test's main() keeps many large buffers on the stack; Windows' default 1 MB main-thread
        # stack is too small for it (the frame is ~7 MB with clang; macOS and Linux give 8 MB).
        target_link_options(ElukkaTest PRIVATE /STACK:67108864)
    endif()
endif()
