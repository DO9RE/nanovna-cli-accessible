cmake_minimum_required(VERSION 3.10)
project(nanovna_cli_accessible LANGUAGES CXX)

# This project requires Windows
if(NOT WIN32)
    message(FATAL_ERROR "\n"
        "========================================================\n"
        "ERROR: This project is Windows-only!\n"
        "========================================================\n"
        "This application requires Windows-specific APIs:\n"
        "  - Windows API (windows.h)\n"
        "  - Windows Multimedia API (winmm)\n"
        "  - Console I/O (conio.h)\n"
        "  - SetupAPI for COM port enumeration\n"
        "\n"
        "Please build this project on Windows using:\n"
        "  - MSYS2 with MinGW-w64, or\n"
        "  - Visual Studio\n"
        "\n"
        "See README.md for build instructions.\n"
        "========================================================\n"
    )
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static linking for Windows to avoid MinGW runtime dependencies
if (WIN32 AND MINGW)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -static")
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

file(GLOB SOURCES "src/*.cpp")

add_executable(nanovna-cli ${SOURCES})

if (WIN32)
  target_link_libraries(nanovna-cli PRIVATE winmm)
  target_link_libraries(nanovna-cli PRIVATE setupapi)
  target_link_libraries(nanovna-cli PRIVATE winspool)
  target_link_libraries(nanovna-cli PRIVATE ws2_32)
endif()
