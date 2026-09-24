add_library(astelio_core STATIC
    src/character_rules.cpp
    src/composer.cpp
    src/dictionary.cpp
    src/dictionary_builder.cpp
    src/input_session.cpp
    src/modifier_tap_tracker.cpp
    src/romaji_table.cpp
    src/romaji_table_io.cpp
    src/utf.cpp
    src/version.cpp
)

target_include_directories(astelio_core PUBLIC include)
target_compile_definitions(astelio_core PRIVATE ASTELIO_CORE_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(astelio_core PRIVATE /W4 /WX /utf-8 /permissive-)
else()
    target_compile_options(astelio_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(ASTELIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(ASTELIO_BUILD_FUZZERS)
    add_executable(astelio_composer_fuzzer fuzz/composer_fuzzer.cpp)
    target_link_libraries(astelio_composer_fuzzer PRIVATE astelio_core)
    target_compile_options(astelio_composer_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(astelio_composer_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)

    add_executable(astelio_dictionary_fuzzer fuzz/dictionary_fuzzer.cpp)
    target_link_libraries(astelio_dictionary_fuzzer PRIVATE astelio_core)
    target_compile_options(astelio_dictionary_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(astelio_dictionary_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
