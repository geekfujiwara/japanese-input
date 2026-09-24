add_executable(astelio_core_tests
    version_test.cpp
)

target_link_libraries(astelio_core_tests PRIVATE astelio_core GTest::gtest_main)
target_compile_definitions(astelio_core_tests PRIVATE ASTELIO_EXPECTED_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(astelio_core_tests PRIVATE /utf-8)
endif()

gtest_discover_tests(astelio_core_tests)
