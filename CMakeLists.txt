cmake_minimum_required(VERSION 3.21)

project(stream-bits
        VERSION     0.1.0
        DESCRIPTION "A C++ WebServer"
        LANGUAGES   CXX
)

# ---------------------------------------------------------------------------
# Options and project-wide settings (only when stream-bits is the top-level project,
# so that add_subdirectory()/FetchContent consumers keep control of their own build)
# ---------------------------------------------------------------------------
option(STREAM_BITS_BUILD_EXAMPLES "Build the programs in examples/" ${PROJECT_IS_TOP_LEVEL})

if (PROJECT_IS_TOP_LEVEL)
    option(BUILD_SHARED_LIBS "Build stream-bits as a shared library" ON)

    # Every executable (and, on Windows, every DLL) is placed in <repo>/bin.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
endif ()

# ---------------------------------------------------------------------------
# Library: sources are listed by hand, so you decide exactly what gets compiled
# ---------------------------------------------------------------------------
add_library(stream-bits
        src/stream_bits.cpp
        include/api/server_api.hpp
        include/api/server_api.hpp
        src/api/server_api.cpp
        src/api/server_api.cpp
        include/net/stream_bits_tcp.hpp
        src/net/stream_bits_tcp.cpp
        include/net/http.hpp
        src/net/http.cpp
)
add_library(stream-bits::stream-bits ALIAS stream-bits)

# Platform-specific sources
if (WIN32)
    target_sources(stream-bits PRIVATE
            src/net/io_handler_win32.cpp
    )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(stream-bits PRIVATE
            src/net/connection_linux.cpp
    )
endif ()

target_include_directories(stream-bits PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_compile_features(stream-bits PUBLIC cxx_std_23)

set_target_properties(stream-bits PROPERTIES
        CXX_EXTENSIONS             OFF
        WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_compile_options(stream-bits PRIVATE
        $<$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>:/W4>
        $<$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>:/permissive->
        $<$<NOT:$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>:-Wall>
        $<$<NOT:$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>:-Wextra>
        $<$<NOT:$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>:-Wpedantic>
)

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if (STREAM_BITS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()

# ---------------------------------------------------------------------------
# Install + CMake package (find_package(stream-bits))
# ---------------------------------------------------------------------------
if (PROJECT_IS_TOP_LEVEL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(STREAM_BITS_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/stream-bits")

    install(TARGETS stream-bits EXPORT stream-bits-targets)
    install(DIRECTORY include/ TYPE INCLUDE)

    install(EXPORT stream-bits-targets
            NAMESPACE   stream-bits::
            DESTINATION "${STREAM_BITS_CMAKE_DIR}"
    )

    write_basic_package_version_file(
            "${PROJECT_BINARY_DIR}/stream-bits-config-version.cmake"
            COMPATIBILITY SameMajorVersion
    )
    install(FILES
            "${PROJECT_SOURCE_DIR}/cmake/stream-bits-config.cmake"
            "${PROJECT_BINARY_DIR}/stream-bits-config-version.cmake"
            DESTINATION "${STREAM_BITS_CMAKE_DIR}"
    )
endif ()
