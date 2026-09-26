add_executable(astelio_core_tests
    character_rules_test.cpp
    composer_robustness_test.cpp
    composer_test.cpp
    converter_test.cpp
    dictionary_test.cpp
    emoji_test.cpp
    input_session_test.cpp
    kana_forms_test.cpp
    modifier_tap_tracker_test.cpp
    romaji_table_io_test.cpp
    special_candidates_test.cpp
    typo_candidates_test.cpp
    utf_test.cpp
    version_test.cpp
)

target_link_libraries(astelio_core_tests PRIVATE astelio_core GTest::gtest_main)
target_compile_definitions(astelio_core_tests PRIVATE ASTELIO_EXPECTED_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(astelio_core_tests PRIVATE /utf-8)
endif()

gtest_discover_tests(astelio_core_tests)
