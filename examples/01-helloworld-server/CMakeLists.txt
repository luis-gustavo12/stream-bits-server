cmake_minimum_required(VERSION 3.21)

project(01-helloworld-server LANGUAGES CXX)

add_executable(helloworld-server main.cpp)

if (TARGET stream-bits::stream-bits)
    # Built from the stream-bits tree: the target already exists, and the executable and the
    # DLL both land in <repo>/bin, so nothing else is needed.
    target_link_libraries(helloworld-server PRIVATE stream-bits::stream-bits)
else ()
    # Built standalone, it behaves like any third-party consumer and finds the installed package.
    find_package(stream-bits 0.1 REQUIRED CONFIG)
    target_link_libraries(helloworld-server PRIVATE stream-bits::stream-bits)

    # Windows has no rpath: a shared stream-bits DLL must sit next to the executable to be loaded.
    get_target_property(stream_bits_type stream-bits::stream-bits TYPE)
    if (WIN32 AND stream_bits_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET helloworld-server POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:helloworld-server> $<TARGET_FILE_DIR:helloworld-server>
                COMMAND_EXPAND_LISTS
        )
    endif ()
endif ()
