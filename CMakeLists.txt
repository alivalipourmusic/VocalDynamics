cmake_minimum_required(VERSION 3.22)

project(VocalDynamics VERSION 0.1.0)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
)

FetchContent_MakeAvailable(JUCE)

juce_add_plugin(VocalDynamics
    COMPANY_NAME "Avlirecordlabel"
    BUNDLE_ID "com.avlirecordlabel.vocaldynamics"
    PLUGIN_MANUFACTURER_CODE Avlr
    PLUGIN_CODE Vdyn
    FORMATS VST3 Standalone
    VST3_CAN_REPLACE_VST2 TRUE
    PRODUCT_NAME "Vocal Dynamics"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(VocalDynamics)

target_sources(VocalDynamics
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
)

target_compile_features(VocalDynamics PRIVATE cxx_std_17)
target_compile_definitions(VocalDynamics
    PRIVATE
        JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
)

target_link_libraries(VocalDynamics
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Separate desktop editor for opening, previewing and playing vocal files.
juce_add_gui_app(VocalDynamicsEditor
    PRODUCT_NAME "Vocal Dynamics Editor"
    COMPANY_NAME "Avlirecordlabel"
    VERSION "0.1.0"
)

juce_generate_juce_header(VocalDynamicsEditor)

target_sources(VocalDynamicsEditor
    PRIVATE
        Source/StandaloneMain.cpp
        Source/StandaloneEditor.cpp
        Source/StandaloneEditor.h
)

target_compile_features(VocalDynamicsEditor PRIVATE cxx_std_17)
target_compile_definitions(VocalDynamicsEditor
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(VocalDynamicsEditor
    PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
