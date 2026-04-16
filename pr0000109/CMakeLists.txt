cmake_minimum_required(VERSION 3.15)
project(tharavu VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Source files ─────────────────────────────────────────────────────────────

set(ENGINE_SOURCES
    src/data_engine.c
    src/platform.c
)

# ── Static library (used by the CLI tools) ────────────────────────────────────

add_library(tharavu_static STATIC ${ENGINE_SOURCES})
target_include_directories(tharavu_static PUBLIC include)
set_target_properties(tharavu_static PROPERTIES OUTPUT_NAME tharavu)

# ── Shared library / DLL ─────────────────────────────────────────────────────
#
#   Windows:  tharavu.dll  +  tharavu.lib  (import library)
#   Linux:    libtharavu.so
#   macOS:    libtharavu.dylib
#
# Consumers link against this target and include tharavu_dll.h ONLY.
# The internal data_engine.h is never installed.

add_library(tharavu SHARED ${ENGINE_SOURCES} src/tharavu_dll.c)
target_include_directories(tharavu PUBLIC include)
target_compile_definitions(tharavu PRIVATE THARAVU_EXPORTS)

# Restrict exported symbols to those marked THARAVU_API on non-Windows.
# On Windows, __declspec(dllexport/import) handles this automatically.
if(NOT WIN32)
    target_compile_options(tharavu PRIVATE -fvisibility=hidden)
endif()

# On Windows with MSVC generate a .lib import library automatically.
# MinGW produces libtharavu.dll.a via --out-implib (handled by CMake).
set_target_properties(tharavu PROPERTIES
    OUTPUT_NAME   tharavu
    WINDOWS_EXPORT_ALL_SYMBOLS OFF   # we use explicit __declspec
)

# ── CLI Tools ────────────────────────────────────────────────────────────────

foreach(TOOL de_dump de_import de_crud)
    if(EXISTS "${CMAKE_SOURCE_DIR}/tools/${TOOL}.c")
        add_executable(${TOOL} tools/${TOOL}.c)
        target_include_directories(${TOOL} PRIVATE include)
        target_link_libraries(${TOOL} PRIVATE tharavu_static)
    endif()
endforeach()

# ── Tests ────────────────────────────────────────────────────────────────────

foreach(TEST test_engine test_logical)
    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/${TEST}.c")
        add_executable(${TEST} tests/${TEST}.c)
        target_include_directories(${TEST} PRIVATE include)
        target_link_libraries(${TEST} PRIVATE tharavu_static)
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/tools/${TEST}.c")
        add_executable(${TEST} tools/${TEST}.c)
        target_include_directories(${TEST} PRIVATE include)
        target_link_libraries(${TEST} PRIVATE tharavu_static)
    endif()
endforeach()

# ── Install rules ─────────────────────────────────────────────────────────────
#   cmake --install . --prefix /usr/local

install(TARGETS tharavu
    RUNTIME DESTINATION bin          # .dll on Windows
    LIBRARY DESTINATION lib          # .so / .dylib on POSIX
    ARCHIVE DESTINATION lib          # .lib import lib on Windows
)

# Expose only the public header — not the internal engine headers
install(FILES include/tharavu_dll.h DESTINATION include)

# ── Summary ───────────────────────────────────────────────────────────────────

message(STATUS "Tharavu DataEngine ${PROJECT_VERSION}")
message(STATUS "  DLL/SO target : tharavu")
message(STATUS "  Static target : tharavu_static")
message(STATUS "  Public header : include/tharavu_dll.h")
message(STATUS "")
message(STATUS "  Build DLL only:  cmake --build . --target tharavu")
message(STATUS "  Install:         cmake --install . --prefix <dir>")
