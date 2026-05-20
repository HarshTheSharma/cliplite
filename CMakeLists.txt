cmake_minimum_required(VERSION 3.20)
project(cliplite VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX STRICT
    _WIN32_WINNT=0x0A00 WINVER=0x0A00
    VC_EXTRA_LEAN
    _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
)

# ── nlohmann/json ──────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# ── WTL (header-only) ─────────────────────────────────────────────────────────
# Download WTL 10 from https://sourceforge.net/projects/wtl/ and extract so
# that atlapp.h lives at  third_party/wtl/Include/atlapp.h,
# OR pass  -DWTL_INCLUDE_DIR=<path>  to CMake.
if(NOT DEFINED WTL_INCLUDE_DIR)
    set(WTL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/wtl/Include"
        CACHE PATH "Path to WTL Include/ directory")
endif()
if(NOT EXISTS "${WTL_INCLUDE_DIR}/atlapp.h")
    message(FATAL_ERROR
        "WTL not found at ${WTL_INCLUDE_DIR}.\n"
        "Download WTL 10 from https://sourceforge.net/projects/wtl/ and extract\n"
        "to ${CMAKE_SOURCE_DIR}/third_party/wtl/, or pass -DWTL_INCLUDE_DIR=<path>.")
endif()

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/App.cpp
    src/Config.cpp
    src/TexturePool.cpp
    src/CaptureEngine.cpp
    src/VideoCapture.cpp
    src/AudioLoopback.cpp
    src/AudioMic.cpp
    src/EncoderThread.cpp
    src/TrayIcon.cpp
    src/HotkeyDialog.cpp
    resources/app.rc
)

add_executable(cliplite WIN32 ${SOURCES})

target_include_directories(cliplite PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${WTL_INCLUDE_DIR}
)

target_link_libraries(cliplite PRIVATE
    nlohmann_json::nlohmann_json
    d3d11 dxgi
    mf mfplat mfuuid mfreadwrite
    evr strmiids
    ole32 oleaut32
    shell32 user32 gdi32 comctl32
    avrt uxtheme comdlg32 shlwapi advapi32
    winmm
)


target_link_options(cliplite PRIVATE
    /SUBSYSTEM:WINDOWS
    /ENTRY:wWinMainCRTStartup
)

# Let the RC compiler find src/resource.h
set_source_files_properties(resources/app.rc PROPERTIES
    COMPILE_FLAGS "/I \"${CMAKE_SOURCE_DIR}/src\"")
