add_library(astelio_tip_keys STATIC
    src/key_translation.cpp
)
target_include_directories(astelio_tip_keys PUBLIC src)
target_link_libraries(astelio_tip_keys PUBLIC astelio_core)
target_compile_definitions(astelio_tip_keys PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(astelio_tip_keys PRIVATE /W4 /WX /utf-8 /permissive- /EHsc)

# The user dictionary file and its import/export, also linked into the tests (T-D03-3).
add_library(astelio_tip_store STATIC
    src/user_dictionary_store.cpp
)
target_include_directories(astelio_tip_store PUBLIC src)
target_link_libraries(astelio_tip_store PUBLIC astelio_core)
target_compile_definitions(astelio_tip_store PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(astelio_tip_store PRIVATE /W4 /WX /utf-8 /permissive- /EHsc)

add_library(astelio_tip SHARED
    src/astelio_tip.def
    src/astelio_tip.rc
    src/candidate_window.cpp
    src/dialog_keys.cpp
    src/dictionary_loader.cpp
    src/display_attributes.cpp
    src/dllmain.cpp
    src/emoji_history.cpp
    src/emoji_window.cpp
    src/lang_bar_button.cpp
    src/learning_manager.cpp
    src/learning_store.cpp
    src/mode_window.cpp
    src/popup_window.cpp
    src/registration.cpp
    src/text_service.cpp
    src/user_dictionary_manager.cpp
)

target_include_directories(astelio_tip PUBLIC include PRIVATE src)
target_link_libraries(astelio_tip PRIVATE astelio_tip_keys astelio_tip_store d2d1 dwrite dwmapi comctl32 comdlg32)
target_compile_options(astelio_tip PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /WX /utf-8 /permissive- /EHsc>)
# The version resource (astelio_tip.rc) takes the project version.
configure_file(src/astelio_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/astelio_version.h @ONLY)
target_include_directories(astelio_tip PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

if(ASTELIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
