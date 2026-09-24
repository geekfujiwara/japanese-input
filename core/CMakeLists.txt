add_library(astelio_core STATIC
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
