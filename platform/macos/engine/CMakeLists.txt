# The platform-independent part of the macOS input method (key codes, the shared engine, the session and what it
# shows). Built and tested on every POSIX host; the Xcode project (platform/macos/project.yml) compiles the same
# sources into the input method.
add_library(astelio_mac_engine STATIC
    src/engine.cpp
    src/key_translation.cpp
    src/mac_session.cpp
)

target_include_directories(astelio_mac_engine PUBLIC include)
target_link_libraries(astelio_mac_engine PUBLIC astelio_core)
target_compile_options(astelio_mac_engine PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(ASTELIO_BUILD_TESTS)
    add_executable(astelio_mac_engine_tests
        tests/engine_test.cpp
        tests/key_translation_test.cpp
        tests/mac_session_test.cpp
    )
    target_link_libraries(astelio_mac_engine_tests PRIVATE astelio_mac_engine GTest::gtest_main)
    gtest_discover_tests(astelio_mac_engine_tests)
endif()
