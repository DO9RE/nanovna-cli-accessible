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

# Static linking for Windows to avoid MinGW runtime dependencies
if (WIN32 AND MINGW)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -static")
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src/platform/interface)

# Collect core source files
file(GLOB CORE_SOURCES "src/*.cpp")

# Collect platform-specific source files
if(WIN32)
    file(GLOB PLATFORM_SOURCES "src/platform/windows/*.cpp")
elseif(APPLE)
    # Use POSIX implementation for macOS
    file(GLOB PLATFORM_SOURCES "src/platform/posix/*.cpp")
    file(GLOB MACOS_SPECIFIC_SOURCES "src/platform/macos/*.cpp")
    # Exclude console_input_mac.cpp as it's replaced by console_input_posix.cpp
    list(FILTER MACOS_SPECIFIC_SOURCES EXCLUDE REGEX "console_input_mac\\.cpp$")
    list(APPEND PLATFORM_SOURCES ${MACOS_SPECIFIC_SOURCES})
elseif(UNIX)
    # Use POSIX implementation for Linux
    file(GLOB PLATFORM_SOURCES "src/platform/posix/*.cpp")
    file(GLOB LINUX_SPECIFIC_SOURCES "src/platform/linux/*.cpp")
    # Exclude console_input_linux.cpp as it's replaced by console_input_posix.cpp
    list(FILTER LINUX_SPECIFIC_SOURCES EXCLUDE REGEX "console_input_linux\\.cpp$")
    list(APPEND PLATFORM_SOURCES ${LINUX_SPECIFIC_SOURCES})
endif()

set(ALL_SOURCES ${CORE_SOURCES} ${PLATFORM_SOURCES})

add_executable(nanovna-cli ${ALL_SOURCES})

# Platform-specific libraries
if (WIN32)
    target_link_libraries(nanovna-cli PRIVATE winmm)
    target_link_libraries(nanovna-cli PRIVATE setupapi)
    target_link_libraries(nanovna-cli PRIVATE winspool)
    target_link_libraries(nanovna-cli PRIVATE ws2_32)
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
