add_library(astelio_tip SHARED
    src/astelio_tip.def
    src/dllmain.cpp
    src/registration.cpp
    src/text_service.cpp
)

target_include_directories(astelio_tip PUBLIC include PRIVATE src)
target_compile_definitions(astelio_tip PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(astelio_tip PRIVATE /W4 /WX /utf-8 /permissive- /EHsc)

if(ASTELIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
