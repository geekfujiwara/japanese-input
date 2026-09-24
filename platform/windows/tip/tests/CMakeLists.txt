add_executable(astelio_tip_tests
    tip_activation_test.cpp
)

target_link_libraries(astelio_tip_tests PRIVATE GTest::gtest_main)
target_include_directories(astelio_tip_tests PRIVATE ../include)
target_compile_definitions(astelio_tip_tests PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    ASTELIO_TIP_PATH="$<TARGET_FILE:astelio_tip>"
)
target_compile_options(astelio_tip_tests PRIVATE /utf-8)
add_dependencies(astelio_tip_tests astelio_tip)

gtest_discover_tests(astelio_tip_tests)
