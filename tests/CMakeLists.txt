find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(stream-bits-tests
        server_test.cpp
)

target_link_libraries(stream-bits-tests PRIVATE
        stream-bits::stream-bits
        GTest::gtest_main
)

# PRE_TEST because on Windows the exe can only run once the DLLs are next to it,
# and that copy happens in another post-build step that may run after discovery.
gtest_discover_tests(stream-bits-tests DISCOVERY_MODE PRE_TEST)
