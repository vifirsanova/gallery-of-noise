cmake_minimum_required(VERSION 3.16)
project(VoiceBoardBioModulation VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BIOMUS_ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(BIOMUS_ENABLE_ASAN)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)

# Optional ALSA detection
find_package(ALSA QUIET)
if(NOT ALSA_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(ALSA QUIET alsa)
endif()

# Optional RtMidi detection
if(PkgConfig_FOUND)
    pkg_check_modules(RTMIDI QUIET rtmidi)
endif()

# Optional SDL2 detection (Arch Linux windowing + Raspberry Pi KMSDRM)
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET sdl2)
endif()

# ============================================================================
# 1. Target: core (Portable C++17 Real-Time DSP, Synth, State & Serialization)
#    Strictly ZERO OS/hardware/UI dependencies.
# ============================================================================
add_library(core STATIC
    src/core/Types.cpp
    src/core/State.cpp
    src/core/Commands.cpp
    src/core/Effects.cpp
    src/core/BioSignals.cpp
    src/core/DroneSynth.cpp
    src/core/NoiseSynth.cpp
    src/core/Engine.cpp
    src/core/WavFile.cpp
    src/core/JsonSerializer.cpp
)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ============================================================================
# 2. Target: audio (Full-duplex ALSA 48kHz + Simulated RT Backend)
# ============================================================================
add_library(audio STATIC
    src/audio/AlsaBackend.cpp
    src/audio/SimulatedBackend.cpp
)
target_include_directories(audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(audio PUBLIC core Threads::Threads)

if(ALSA_FOUND)
    message(STATUS "[biomus] ALSA found — enabling full-duplex hardware backend")
    target_compile_definitions(audio PUBLIC BIOMUSIC_HAVE_ALSA=1)
    if(TARGET ALSA::ALSA)
        target_link_libraries(audio PUBLIC ALSA::ALSA)
    else()
        target_include_directories(audio PUBLIC ${ALSA_INCLUDE_DIRS})
        target_link_libraries(audio PUBLIC ${ALSA_LIBRARIES})
    endif()
else()
    message(STATUS "[biomus] ALSA dev headers not found — building with Simulated RT backend")
endif()

# ============================================================================
# 3. Target: ui (Autonomous 480x320 Embedded Small-Screen UI: SDL2 + /dev/fb0)
# ============================================================================
add_library(ui STATIC
    src/ui/Canvas.cpp
    src/ui/InstrumentUi.cpp
    src/ui/FbdevBackend.cpp
    src/ui/Sdl2Backend.cpp
)
target_include_directories(ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ui PUBLIC core)

if(SDL2_FOUND)
    message(STATUS "[biomus] SDL2 found — enabling native KMSDRM & desktop window UI")
    target_compile_definitions(ui PUBLIC BIOMUS_HAS_SDL2=1)
    if(TARGET SDL2::SDL2)
        target_link_libraries(ui PUBLIC SDL2::SDL2)
    else()
        target_include_directories(ui PUBLIC ${SDL2_INCLUDE_DIRS})
        target_link_libraries(ui PUBLIC ${SDL2_LIBRARIES})
    endif()
else()
    message(STATUS "[biomus] SDL2 not found — building UI with Linux fbdev (/dev/fb0) + software Canvas")
endif()

# ============================================================================
# 4. Target: app_modules (RtMidi Akai Controller + 128x64 I2C SSD1306 OLED)
# ============================================================================
add_library(app_modules STATIC
    src/app/MidiController.cpp
    src/app/OledDisplay.cpp
)
target_include_directories(app_modules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(app_modules PUBLIC core Threads::Threads)

if(RTMIDI_FOUND)
    message(STATUS "[biomus] RtMidi found — enabling hardware/virtual ALSA MIDI input")
    target_compile_definitions(app_modules PUBLIC BIOMUSIC_HAVE_RTMIDI=1)
    target_include_directories(app_modules PUBLIC ${RTMIDI_INCLUDE_DIRS})
    target_link_libraries(app_modules PUBLIC ${RTMIDI_LIBRARIES})
else()
    message(STATUS "[biomus] RtMidi not found — building programmatic MIDI handler")
endif()

# ============================================================================
# 5. Executables: app (Autonomous Instrument) & render_wav (Offline WAV CLI)
# ============================================================================
add_executable(app src/app/main.cpp)
target_link_libraries(app PRIVATE core audio ui app_modules Threads::Threads)

add_executable(render_wav src/app/render_wav.cpp)
target_link_libraries(render_wav PRIVATE core)

# ============================================================================
# 6. Automated Unit & Integration Tests (Catch2 v3 if installed, or built-in)
# ============================================================================
enable_testing()
find_package(Catch2 3 QUIET)

add_executable(tests tests/test_core.cpp)
target_link_libraries(tests PRIVATE core)

add_executable(integration_tests tests/test_integration.cpp)
target_link_libraries(integration_tests PRIVATE core audio ui app_modules Threads::Threads)

if(Catch2_FOUND)
    message(STATUS "[biomus] Catch2 v3 found — linking Catch2::Catch2WithMain")
    target_compile_definitions(tests PRIVATE BIOMUSIC_HAVE_CATCH2=1)
    target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)

    target_compile_definitions(integration_tests PRIVATE BIOMUSIC_HAVE_CATCH2=1)
    target_link_libraries(integration_tests PRIVATE Catch2::Catch2WithMain)
else()
    message(STATUS "[biomus] Catch2 v3 not found — using built-in Catch2-compatible runner")
endif()

add_test(NAME core_unit_tests COMMAND tests)
add_test(NAME system_integration_tests COMMAND integration_tests)
