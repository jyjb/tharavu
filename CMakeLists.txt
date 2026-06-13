cmake_minimum_required(VERSION 3.15)
project(tharavu VERSION 1.2 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Source files ─────────────────────────────────────────────────────────────

set(ENGINE_SOURCES
    src/data_engine.c
    src/platform.c
    src/ogph.c
    src/ogph_builder.c
)

# ── Static library (used by the CLI tools) ────────────────────────────────────

add_library(tharavu_static STATIC ${ENGINE_SOURCES})
target_include_directories(tharavu_static PUBLIC src/include)
target_compile_definitions(tharavu_static PRIVATE THARAVU_EXPORTS)
set_target_properties(tharavu_static PROPERTIES OUTPUT_NAME tharavu)

# ── Shared library / DLL ─────────────────────────────────────────────────────
#
#   Windows:  tharavu.dll  +  tharavu.lib  (import library)
#   Linux:    libtharavu.so
#   macOS:    libtharavu.dylib
#
# Consumers link against this target and include tharavu_dll.h ONLY.
# The internal data_engine.h is never installed.
# THARAVU has zero external dependencies (sLispManager removed 2026-05-29).

add_library(tharavu SHARED ${ENGINE_SOURCES} src/tharavu_dll.c)
target_include_directories(tharavu PUBLIC src/include)
target_compile_definitions(tharavu PRIVATE THARAVU_EXPORTS)

if(NOT WIN32)
    target_compile_options(tharavu PRIVATE -fvisibility=hidden)
endif()

set_target_properties(tharavu PROPERTIES
    OUTPUT_NAME   tharavu
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
)

if(WIN32)
    set_target_properties(tharavu PROPERTIES
        LINK_FLAGS "${CMAKE_SOURCE_DIR}/src/tharavu.def"
    )
endif()

# ── CLI Tools ────────────────────────────────────────────────────────────────

foreach(TOOL de_dump de_import de_crud)
    if(EXISTS "${CMAKE_SOURCE_DIR}/src/tools/${TOOL}.c")
        add_executable(${TOOL} src/tools/${TOOL}.c)
        set_target_properties(${TOOL} PROPERTIES OUTPUT_NAME ${TOOL}_CLI)
        target_include_directories(${TOOL} PRIVATE src/include)
        target_link_libraries(${TOOL} PRIVATE tharavu_static)
    endif()
endforeach()

# ── Tests (auto-discovered via CTest) ────────────────────────────────────────

enable_testing()

file(GLOB TEST_SOURCES "test/*.c")

foreach(TEST_FILE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)

    # Windows-specific tests (filename contains "win32") are skipped on non-Windows hosts
    if(TEST_NAME MATCHES "win32" AND NOT WIN32)
        message(STATUS "  Skipping ${TEST_NAME} (Windows-only, non-Windows host)")
        continue()
    endif()

    add_executable(${TEST_NAME} ${TEST_FILE})
    set_target_properties(${TEST_NAME} PROPERTIES OUTPUT_NAME ${TEST_NAME}_CLI)
    target_include_directories(${TEST_NAME} PRIVATE src/include)
    target_link_libraries(${TEST_NAME} PRIVATE tharavu_static)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

# ── Sample / Examples ─────────────────────────────────────────────────────────

if(EXISTS "${CMAKE_SOURCE_DIR}/docs/sample/create_sample_db.c")
    add_executable(create_sample_db docs/sample/create_sample_db.c)
    set_target_properties(create_sample_db PROPERTIES OUTPUT_NAME create_sample_db_CLI)
    target_include_directories(create_sample_db PRIVATE src/include)
    target_link_libraries(create_sample_db PRIVATE tharavu_static)
endif()

# ── Install rules ─────────────────────────────────────────────────────────────
#   cmake --install . --prefix /usr/local

install(TARGETS tharavu
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES src/include/tharavu_dll.h DESTINATION include)

# ── Summary ───────────────────────────────────────────────────────────────────

message(STATUS "Tharavu DataEngine ${PROJECT_VERSION} (zero external dependencies)")
message(STATUS "  DLL/SO target : tharavu")
message(STATUS "  Static target : tharavu_static")
message(STATUS "  Public header : src/include/tharavu_dll.h")
message(STATUS "")
message(STATUS "  Build:           cmake -B build && cmake --build build")
message(STATUS "  Install:         cmake --install build --prefix <dir>")
