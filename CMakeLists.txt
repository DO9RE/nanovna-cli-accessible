cmake_minimum_required(VERSION 3.10)
project(nanovna_cli_accessible LANGUAGES CXX)

# ============ VERSION MANAGEMENT (Two-Repo Safe) ============
# Read version from VERSION file (always present, synced via push script)
file(READ "${CMAKE_SOURCE_DIR}/VERSION" VERSION_STRING)
string(STRIP "${VERSION_STRING}" VERSION_STRING)

# Parse version components
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[a-zA-Z0-9._-]+)?" _ ${VERSION_STRING})
set(VERSION_MAJOR ${CMAKE_MATCH_1})
set(VERSION_MINOR ${CMAKE_MATCH_2})
set(VERSION_PATCH ${CMAKE_MATCH_3})
set(VERSION_SUFFIX ${CMAKE_MATCH_4})

# Try to get Git info (works in both repos independently)
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    # Detect which repo we're in
    execute_process(
        COMMAND ${GIT_EXECUTABLE} config --get remote.origin.url
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_REMOTE_URL
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GIT_REMOTE_URL MATCHES "nanovna-cli-accessible-cpa")
        set(REPO_TYPE "public")
    else()
        set(REPO_TYPE "dev")
    endif()
else()
    set(GIT_COMMIT_HASH "no-git")
    set(REPO_TYPE "unknown")
endif()

# Build timestamp
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S UTC" UTC)

# Build type indicator
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(BUILD_TYPE_STR "Release")
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(BUILD_TYPE_STR "Debug")
else()
    set(BUILD_TYPE_STR "Dev")
endif()

# Configure version header
configure_file(
    "${CMAKE_SOURCE_DIR}/include/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/version.h"
    @ONLY
)

include_directories(${CMAKE_BINARY_DIR}/generated)

message(STATUS "Building ${REPO_TYPE} version: ${VERSION_STRING} (${GIT_COMMIT_HASH})")
# ============================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option for bundled PortAudio on macOS
option(USE_BUNDLED_PORTAUDIO "Use bundled static PortAudio library (macOS only)" OFF)

# Ham Spirit Easter Egg is always enabled
set(WITH_HAM_SPIRIT ON)

# Detect platform
if(WIN32)
    set(PLATFORM_NAME "Windows")
    add_definitions(-DPLATFORM_WINDOWS)
elseif(APPLE)
    set(PLATFORM_NAME "macOS")
    add_definitions(-DPLATFORM_MACOS)
elseif(UNIX)
    set(PLATFORM_NAME "Linux")
    add_definitions(-DPLATFORM_LINUX)
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

message(STATUS "Building for platform: ${PLATFORM_NAME}")

# Ham Spirit Easter Egg is always enabled
add_definitions(-DWITH_HAM_SPIRIT)
message(STATUS "Ham Spirit Easter Egg: ALWAYS ENABLED")

# Optional: enable verbose TTS diagnostics on macOS (prints spawn/exit info to stderr)
# Pass -DTTS_MACOS_DEBUG=ON to cmake to activate.
option(TTS_MACOS_DEBUG "Enable verbose TTS spawn/exit logging on macOS" OFF)
if(TTS_MACOS_DEBUG)
    add_definitions(-DTTS_MACOS_DEBUG)
    message(STATUS "macOS TTS debug logging: ENABLED")
endif()

# Static linking for Windows to avoid MinGW runtime dependencies
if (WIN32 AND MINGW)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -static")
endif()

# MSVC-specific settings (Visual Studio)
if (MSVC)
  # Prevent windows.h from defining min/max macros that conflict with std::min/std::max
  add_definitions(-DNOMINMAX)
  # Enable C++ exception handling
  add_compile_options(/EHsc)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src/platform/interface)

# Collect core source files
file(GLOB CORE_SOURCES "src/*.cpp")

# Exclude platform-specific TTS implementations from core sources
list(FILTER CORE_SOURCES EXCLUDE REGEX "tts_engine_.*\\.cpp$")

# Ham Spirit game is always included (no conditional exclusion needed)

# Collect platform-specific source files
if(WIN32)
    file(GLOB PLATFORM_SOURCES "src/platform/windows/*.cpp")
    # Add Windows-specific TTS implementation
    list(APPEND PLATFORM_SOURCES "${CMAKE_SOURCE_DIR}/src/platform/windows/tts_engine_win.cpp")
    # Add Windows-specific gamepad implementation (always included)
    list(APPEND PLATFORM_SOURCES "${CMAKE_SOURCE_DIR}/src/platform/windows/gamepad_win.cpp")
elseif(APPLE)
    # Use POSIX implementation for macOS
    file(GLOB PLATFORM_SOURCES "src/platform/posix/*.cpp")
    file(GLOB MACOS_SPECIFIC_SOURCES "src/platform/macos/*.cpp")
    # Exclude console_input_mac.cpp as it's replaced by console_input_posix.cpp
    list(FILTER MACOS_SPECIFIC_SOURCES EXCLUDE REGEX "console_input_mac\\.cpp$")
    list(APPEND PLATFORM_SOURCES ${MACOS_SPECIFIC_SOURCES})
    # Add macOS-specific TTS implementation (Objective-C++ for NSSpeechSynthesizer)
    list(APPEND PLATFORM_SOURCES "${CMAKE_SOURCE_DIR}/src/platform/macos/tts_engine_macos.mm")
    # Add macOS-specific gamepad implementation (always included)
    list(APPEND PLATFORM_SOURCES "${CMAKE_SOURCE_DIR}/src/platform/macos/gamepad_macos.cpp")
    # Add native macOS GUI for Ham Spirit (Objective-C++, AppKit/Cocoa)
    list(APPEND PLATFORM_SOURCES "${CMAKE_SOURCE_DIR}/src/platform/macos/hamspirit_gui_macos.mm")
elseif(UNIX)
    # Use POSIX implementation for Linux
    file(GLOB PLATFORM_SOURCES "src/platform/posix/*.cpp")
    file(GLOB LINUX_SPECIFIC_SOURCES "src/platform/linux/*.cpp")
    # Exclude console_input_linux.cpp as it's replaced by console_input_posix.cpp
    list(FILTER LINUX_SPECIFIC_SOURCES EXCLUDE REGEX "console_input_linux\\.cpp$")
    list(APPEND PLATFORM_SOURCES ${LINUX_SPECIFIC_SOURCES})
    # Add Linux-specific TTS implementation
    list(APPEND PLATFORM_SOURCES "${CMAKE_SOURCE_DIR}/src/platform/linux/tts_engine_linux.cpp")
    # Add Linux-specific gamepad implementation (always included)
    list(APPEND PLATFORM_SOURCES "${CMAKE_SOURCE_DIR}/src/platform/linux/gamepad_linux.cpp")
endif()

set(ALL_SOURCES ${CORE_SOURCES} ${PLATFORM_SOURCES})

add_executable(nanovna-cli ${ALL_SOURCES})

# Platform-specific libraries
if (WIN32)
    target_link_libraries(nanovna-cli PRIVATE winmm)
    target_link_libraries(nanovna-cli PRIVATE setupapi)
    target_link_libraries(nanovna-cli PRIVATE winspool)
    target_link_libraries(nanovna-cli PRIVATE ws2_32)
    target_link_libraries(nanovna-cli PRIVATE ole32)
    target_link_libraries(nanovna-cli PRIVATE urlmon)  # For URLDownloadToFile (NVDA controller client DLL download fallback)
    target_link_libraries(nanovna-cli PRIVATE gdiplus)  # GDI+ for Ham Spirit wallpaper image rendering
    target_link_libraries(nanovna-cli PRIVATE hid)     # HID API for PS4/PS5 controller support
    # Media Foundation libraries for MIDI to WAV rendering
    target_link_libraries(nanovna-cli PRIVATE mf)
    target_link_libraries(nanovna-cli PRIVATE mfplat)
    target_link_libraries(nanovna-cli PRIVATE mfreadwrite)
    target_link_libraries(nanovna-cli PRIVATE mfuuid)
elseif(APPLE)
    # macOS-specific libraries
    find_package(Threads REQUIRED)
    target_link_libraries(nanovna-cli PRIVATE Threads::Threads)
    
    # PortAudio for cross-platform audio
    if(USE_BUNDLED_PORTAUDIO)
        # Use bundled static PortAudio
        message(STATUS "Using bundled static PortAudio")
        
        if(DEFINED PORTAUDIO_ROOT)
            set(PORTAUDIO_INCLUDE_DIR "${PORTAUDIO_ROOT}/include")
            set(PORTAUDIO_LIBRARY "${PORTAUDIO_ROOT}/lib/libportaudio.a")
        else()
            message(FATAL_ERROR "PORTAUDIO_ROOT must be set when USE_BUNDLED_PORTAUDIO is ON")
        endif()
        
        if(EXISTS "${PORTAUDIO_LIBRARY}" AND EXISTS "${PORTAUDIO_INCLUDE_DIR}/portaudio.h")
            message(STATUS "Found bundled PortAudio: ${PORTAUDIO_LIBRARY}")
            target_include_directories(nanovna-cli PRIVATE ${PORTAUDIO_INCLUDE_DIR})
            target_link_libraries(nanovna-cli PRIVATE ${PORTAUDIO_LIBRARY})
        else()
            message(FATAL_ERROR "Bundled PortAudio not found at ${PORTAUDIO_LIBRARY}")
        endif()
    else()
        # Use system/Homebrew PortAudio
        message(STATUS "Using system/Homebrew PortAudio")
        find_library(PORTAUDIO_LIBRARY portaudio PATHS /usr/local/lib /opt/homebrew/lib)
        find_path(PORTAUDIO_INCLUDE_DIR portaudio.h PATHS /usr/local/include /opt/homebrew/include)
        
        if(PORTAUDIO_LIBRARY AND PORTAUDIO_INCLUDE_DIR)
            message(STATUS "Found PortAudio: ${PORTAUDIO_LIBRARY}")
            target_include_directories(nanovna-cli PRIVATE ${PORTAUDIO_INCLUDE_DIR})
            target_link_libraries(nanovna-cli PRIVATE ${PORTAUDIO_LIBRARY})
        else()
            message(WARNING "PortAudio not found. Audio functionality will be limited.")
            message(STATUS "Install PortAudio using: brew install portaudio")
        endif()
    endif()
    
    # macOS frameworks (always needed)
    target_link_libraries(nanovna-cli PRIVATE "-framework CoreFoundation")
    target_link_libraries(nanovna-cli PRIVATE "-framework IOKit")
    target_link_libraries(nanovna-cli PRIVATE "-framework CoreAudio")
    target_link_libraries(nanovna-cli PRIVATE "-framework AudioToolbox")
    target_link_libraries(nanovna-cli PRIVATE "-framework CoreMIDI")
    # AppKit for native Ham Spirit GUI window (NSWindow + Core Graphics rendering)
    target_link_libraries(nanovna-cli PRIVATE "-framework AppKit")
    target_compile_definitions(nanovna-cli PRIVATE HAVE_NATIVE_GUI)
    message(STATUS "Ham Spirit GUI: Native macOS (AppKit/Cocoa) — no third-party dependencies")
    
    # Additional frameworks needed for bundled static PortAudio
    if(USE_BUNDLED_PORTAUDIO)
        target_link_libraries(nanovna-cli PRIVATE "-framework AudioUnit")
        target_link_libraries(nanovna-cli PRIVATE "-framework CoreServices")
    endif()
elseif(UNIX)
    # Linux-specific libraries
    find_package(Threads REQUIRED)
    target_link_libraries(nanovna-cli PRIVATE Threads::Threads)
    
    # PortAudio for cross-platform audio
    find_library(PORTAUDIO_LIBRARY portaudio)
    find_path(PORTAUDIO_INCLUDE_DIR portaudio.h)
    
    if(PORTAUDIO_LIBRARY AND PORTAUDIO_INCLUDE_DIR)
        message(STATUS "Found PortAudio: ${PORTAUDIO_LIBRARY}")
        target_include_directories(nanovna-cli PRIVATE ${PORTAUDIO_INCLUDE_DIR})
        target_link_libraries(nanovna-cli PRIVATE ${PORTAUDIO_LIBRARY})
    else()
        message(WARNING "PortAudio not found. Audio functionality will be limited.")
        message(STATUS "Install PortAudio using: sudo apt-get install portaudio19-dev (Debian/Ubuntu)")
    endif()
    
    # Task 2.1: ALSA support for MIDI on Linux
    find_package(ALSA)
    if(ALSA_FOUND)
        message(STATUS "Found ALSA: ${ALSA_LIBRARIES}")
        target_include_directories(nanovna-cli PRIVATE ${ALSA_INCLUDE_DIRS})
        target_link_libraries(nanovna-cli PRIVATE ${ALSA_LIBRARIES})
        add_definitions(-DHAVE_ALSA)
    else()
        message(WARNING "ALSA not found. MIDI functionality will be unavailable on Linux.")
        message(STATUS "Install ALSA using: sudo apt-get install libasound2-dev (Debian/Ubuntu)")
    endif()
endif()

# SDL2 + SDL2_ttf for Linux GUI window (optional)
# macOS uses native AppKit GUI (no third-party deps needed).
# On Linux, provides a graphical window if SDL2 is installed.
# Falls back to terminal-based ANSI GUI if not available.
if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    set(SDL2_FOUND_COMBINED FALSE)
    
    # Try pkg-config first
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET sdl2)
        pkg_check_modules(SDL2_TTF QUIET SDL2_ttf)
    endif()
    
    # Fallback: manual library search
    if(NOT SDL2_FOUND)
        find_library(SDL2_LIBRARY SDL2 PATHS /usr/local/lib)
        find_path(SDL2_INCLUDE_DIR SDL2/SDL.h PATHS /usr/local/include /usr/include)
        if(SDL2_LIBRARY AND SDL2_INCLUDE_DIR)
            set(SDL2_FOUND TRUE)
            set(SDL2_LIBRARIES ${SDL2_LIBRARY})
            set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
        endif()
    endif()
    
    if(NOT SDL2_TTF_FOUND)
        find_library(SDL2_TTF_LIBRARY SDL2_ttf PATHS /usr/local/lib)
        find_path(SDL2_TTF_INCLUDE_DIR SDL2/SDL_ttf.h PATHS /usr/local/include /usr/include)
        if(SDL2_TTF_LIBRARY AND SDL2_TTF_INCLUDE_DIR)
            set(SDL2_TTF_FOUND TRUE)
            set(SDL2_TTF_LIBRARIES ${SDL2_TTF_LIBRARY})
            set(SDL2_TTF_INCLUDE_DIRS ${SDL2_TTF_INCLUDE_DIR})
        endif()
    endif()
    
    if(SDL2_FOUND AND SDL2_TTF_FOUND)
        message(STATUS "Found SDL2: ${SDL2_LIBRARIES}")
        message(STATUS "Found SDL2_ttf: ${SDL2_TTF_LIBRARIES}")
        target_include_directories(nanovna-cli PRIVATE ${SDL2_INCLUDE_DIRS} ${SDL2_TTF_INCLUDE_DIRS})
        target_link_libraries(nanovna-cli PRIVATE ${SDL2_LIBRARIES} ${SDL2_TTF_LIBRARIES})
        target_compile_definitions(nanovna-cli PRIVATE HAVE_SDL2)
        set(SDL2_FOUND_COMBINED TRUE)
        message(STATUS "Ham Spirit GUI: SDL2 windowed mode (Linux)")
    else()
        message(STATUS "SDL2 or SDL2_ttf not found — Ham Spirit will use terminal-based GUI on Linux.")
        message(STATUS "  Install via apt: sudo apt-get install libsdl2-dev libsdl2-ttf-dev")
    endif()
endif()

# Copy midi preset directory to build output
file(GLOB MIDI_PRESETS "${CMAKE_SOURCE_DIR}/midi/*.cfg")
if(MIDI_PRESETS)
    file(COPY ${MIDI_PRESETS} DESTINATION "${CMAKE_BINARY_DIR}/midi")
    message(STATUS "Copied MIDI preset files to build directory")
endif()

# Copy Ham Spirit wallpaper images to build output
file(GLOB HAMSPIRIT_IMAGES "${CMAKE_SOURCE_DIR}/img/*.PNG" "${CMAKE_SOURCE_DIR}/img/*.png")
if(HAMSPIRIT_IMAGES)
    file(COPY ${HAMSPIRIT_IMAGES} DESTINATION "${CMAKE_BINARY_DIR}/img")
    message(STATUS "Copied Ham Spirit image files to build directory")
endif()
