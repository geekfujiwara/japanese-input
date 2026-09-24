cmake_minimum_required(VERSION 3.25)

project(AstelioIME VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ASTELIO_BUILD_TESTS "Build unit tests" ON)
option(ASTELIO_BUILD_FUZZERS "Build libFuzzer targets (clang only)" OFF)

if(ASTELIO_BUILD_FUZZERS)
    # Sanitizers must cover the library too, not only the fuzzer entry point.
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if(ASTELIO_BUILD_TESTS)
    enable_testing()
    include(FetchContent)
    # Test-only dependency (BSD-3-Clause); not shipped with the IME.
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/releases/download/v1.18.0/googletest-1.18.0.tar.gz
        URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    include(GoogleTest)
endif()

add_subdirectory(core)
