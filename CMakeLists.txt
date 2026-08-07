# CMakeLists.txt
#
# Builds MinimalPinyinIME.dll, a Windows 11 TSF text service (in-proc COM DLL).
#
# Target: Windows x64. This CANNOT be built on macOS/Linux; it requires the
# MSVC toolchain and the Windows SDK (msctf.h, ctffunc.h, etc.).
#
# Usage (from a "x64 Native Tools" prompt or with VS generators):
#   cmake -B build -A x64
#   cmake --build build --config Release

cmake_minimum_required(VERSION 3.20)
# CMP0091 lets us select the MSVC runtime via CMAKE_MSVC_RUNTIME_LIBRARY (below)
# instead of hand-patching /MD|/MT flags. Must be set before project().
cmake_policy(SET CMP0091 NEW)

project(MinimalPinyinIME LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static-link the C/C++ runtime so the DLL has NO dependency on the VC++
# redistributable. Without this the default is /MD (dynamic CRT), and on a
# fresh Windows 11 install regsvr32 fails with error 126 (ERROR_MOD_NOT_FOUND)
# because VCRUNTIME140.dll / MSVCP140.dll aren't present. /MT bakes them in.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT WIN32)
    message(WARNING
        "MinimalPinyinIME targets Windows only. Configuring on a non-Windows "
        "host will not produce a usable DLL.")
endif()

# The DLL. SHARED => produces MinimalPinyinIME.dll.
add_library(MinimalPinyinIME SHARED
    src/Globals.cpp
    src/Dictionary.cpp
    src/CandidateWindow.cpp
    src/TextService.cpp
    src/dllmain.cpp
)

# Force the output name (drop any "lib" prefix that some toolchains add).
set_target_properties(MinimalPinyinIME PROPERTIES
    OUTPUT_NAME "MinimalPinyinIME"
    PREFIX ""
)

# Unicode + lean Windows headers.
target_compile_definitions(MinimalPinyinIME PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Export the four standard COM entry points via the .def file.
if(MSVC)
    set_target_properties(MinimalPinyinIME PROPERTIES
        LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/exports.def\"")
    target_compile_options(MinimalPinyinIME PRIVATE /W4 /permissive-)
endif()

# System import libraries. TSF interface GUIDs and CLSIDs come from the SDK's
# uuid.lib; the interfaces themselves are header-only (msctf.h/ctffunc.h).
target_link_libraries(MinimalPinyinIME PRIVATE
    user32
    gdi32
    ole32
    oleaut32
    advapi32
    uuid
    shell32   # SHGetFolderPath / SHGFP
    imm32     # occasionally needed by TSF-adjacent code paths
)

# Copy the sample dictionary next to the built DLL for convenient local testing.
add_custom_command(TARGET MinimalPinyinIME POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/dictionary.txt"
        "$<TARGET_FILE_DIR:MinimalPinyinIME>/dictionary.txt"
)
