cmake_minimum_required(VERSION 3.22)

project(ToneDeck VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
endif()

option(TONEDECK_BUILD_CLAP "Also build a CLAP plugin (needs libs/clap-juce-extensions)" ON)

# ---------------------------------------------------------------------------
# Dependencies (git submodules or plain clones inside libs/)
# ---------------------------------------------------------------------------
add_subdirectory(libs/JUCE)

set(NAM_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/libs/NeuralAmpModelerCore)
set(RTNEURAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/RTNeural)
set(EIGEN_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/libs/eigen)   # one Eigen copy shared by NAM + RTNeural

# --- NAM core (supports standard WaveNet, LSTM, ConvNet and the new A2 architectures)
file(GLOB NAM_SOURCES_TOP ${NAM_DIR}/NAM/*.cpp)
file(GLOB NAM_SOURCES_SUB ${NAM_DIR}/NAM/*/*.cpp)
# OBJECT (not STATIC) so the self-registering architecture parsers are not dropped by the linker
add_library(nam_core OBJECT ${NAM_SOURCES_TOP} ${NAM_SOURCES_SUB})
target_include_directories(nam_core SYSTEM PUBLIC ${NAM_DIR} ${NAM_DIR}/Dependencies/nlohmann ${EIGEN_DIR})
target_compile_definitions(nam_core PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
if(WIN32)
    target_compile_definitions(nam_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    target_compile_options(nam_core PRIVATE /bigobj /O2)
endif()

# --- RTNeural (AIDA-X / .json keras models)
add_library(rtneural_lib INTERFACE)
target_include_directories(rtneural_lib SYSTEM INTERFACE ${RTNEURAL_DIR} ${EIGEN_DIR})
target_compile_definitions(rtneural_lib INTERFACE RTNEURAL_USE_EIGEN=1 RTNEURAL_DEFAULT_ALIGNMENT=16)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
set(TONEDECK_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND TONEDECK_FORMATS AU)
endif()
if(UNIX AND NOT APPLE)
    list(APPEND TONEDECK_FORMATS LV2)
endif()

juce_add_plugin(ToneDeck
    COMPANY_NAME "ToneDeck Audio"
    BUNDLE_ID com.tonedeck.tonedeck
    PLUGIN_MANUFACTURER_CODE Tdck
    PLUGIN_CODE Tdk1
    FORMATS ${TONEDECK_FORMATS}
    PRODUCT_NAME "ToneDeck"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect
    LV2URI "https://github.com/tonedeck/tonedeck"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "ToneDeck needs audio input to process your guitar.")

target_sources(ToneDeck PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/dsp/NamEngine.cpp
    Source/dsp/AidaEngine.cpp
    Source/ui/LcdDisplay.cpp
    Source/ui/PedalLookAndFeel.cpp
    Source/online/Tone3000Client.cpp)

target_include_directories(ToneDeck PRIVATE Source)

# TONE3000 publishable key baked in at build time (optional; users can also paste it in the plugin)
set(TONEDECK_T3K_CLIENT_ID "" CACHE STRING "TONE3000 publishable key (t3k_pub_...)")

# HTTPS: WinINet on Windows, NSURLSession on macOS, libcurl on Linux
if(UNIX AND NOT APPLE)
    set(TONEDECK_USE_CURL 1)
else()
    set(TONEDECK_USE_CURL 0)
endif()

target_compile_definitions(ToneDeck PUBLIC
    TONEDECK_T3K_CLIENT_ID="${TONEDECK_T3K_CLIENT_ID}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=${TONEDECK_USE_CURL}
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ToneDeck
    PRIVATE
        nam_core
        rtneural_lib
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(ToneDeck PRIVATE /bigobj)
endif()

if(TONEDECK_USE_CURL)
    find_package(CURL REQUIRED)
    target_link_libraries(ToneDeck PRIVATE CURL::libcurl)
endif()

# --- CLAP (Bitwig, Reaper, FL Studio 2024+, etc.)
if(TONEDECK_BUILD_CLAP AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/libs/clap-juce-extensions/CMakeLists.txt)
    add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)
    clap_juce_extensions_plugin(TARGET ToneDeck
        CLAP_ID "com.tonedeck.tonedeck"
        CLAP_FEATURES audio-effect distortion mono stereo)
endif()

# ---------------------------------------------------------------------------
# Offline tests (render audio through the full chain + UI snapshot)
# ---------------------------------------------------------------------------
option(TONEDECK_BUILD_TESTS "Build the offline test runner" OFF)
if(TONEDECK_BUILD_TESTS)
    juce_add_console_app(ToneDeckTests PRODUCT_NAME "ToneDeckTests")
    target_sources(ToneDeckTests PRIVATE
        Tests/OfflineTest.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/dsp/NamEngine.cpp
        Source/dsp/AidaEngine.cpp
        Source/ui/LcdDisplay.cpp
        Source/ui/PedalLookAndFeel.cpp
        Source/online/Tone3000Client.cpp)
    target_include_directories(ToneDeckTests PRIVATE Source)
    target_compile_definitions(ToneDeckTests PRIVATE
        JucePlugin_Name="ToneDeck" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=${TONEDECK_USE_CURL} JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ToneDeckTests PRIVATE nam_core rtneural_lib
        juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography juce::juce_recommended_config_flags)
    if(TONEDECK_USE_CURL)
        target_link_libraries(ToneDeckTests PRIVATE CURL::libcurl)
    endif()
endif()
