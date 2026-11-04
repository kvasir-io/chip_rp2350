cmake_minimum_required(VERSION 3.28)

# Host tests of the picobin block builder (src/chip/picobin.hpp). Standalone, not part of any firmware tree:
#
# cmake -S tests/picobin -B tests/picobin/build -G Ninja && cmake --build tests/picobin/build && ctest --test-dir
# tests/picobin/build
#
# * picobin_test.cpp: the encodings against the RP2350 datasheet (5.9) and picotool's writer, and the block size limits.
# * reader_test.cpp: PicobinReader.hpp, the boot ROM's walk read back (data/README.md: the picotool-made inputs). Most
#   checks are static_asserts, so building is the test; the program checks the same words at run time and prints what it
#   compared.
# * seal_test.cpp: PicobinSeal.hpp, images sealed as A/B updates (picotool's sealed ones again among them) and walked.
# * bad/*_fail.cpp: blocks that must NOT compile, each with the message it must fail with (first line `// expect: ...`).
#   The `_fail.cpp` suffix keeps them out of clang-tidy (Kvasir_SDK tools/tidy_keep.toml [must_fail]).
#
# picobin.hpp needs only the standard library, so a lone checkout of this repository (CI) builds the tests.
#
# clang++ unless the caller names a compiler (CXX / -DCMAKE_CXX_COMPILER): the firmware's compiler.
if(NOT CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
    find_program(_clangxx clang++)
    if(_clangxx)
        set(CMAKE_CXX_COMPILER "${_clangxx}")
    endif()
endif()
project(chip_rp2350_picobin_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

get_filename_component(_chip_src "${CMAKE_CURRENT_LIST_DIR}/../../src" ABSOLUTE)

set(_warnings -Wall -Wextra -Wpedantic -Werror)
# clang: the firmware trees' set (as chip_rp_common/tests), so a warning the header would give in a firmware build shows
# here first
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(
        APPEND
        _warnings
        -Weverything
        -Wno-switch-default
        -Wno-c++98-compat
        -Wno-c++98-compat-pedantic
        -Wno-c++20-compat
        -Wno-pre-c2x-compat
        -Wno-pre-c11-compat
        -Wno-padded
        -Wno-covered-switch-default
        -Wno-switch-enum
        -Wno-unknown-warning-option
        -Wno-reserved-identifier
        -Wno-unsafe-buffer-usage
        -Wno-nrvo
        -Wno-lifetime-safety-intra-tu-suggestions
        -Wno-lifetime-safety-intra-tu-constructor-suggestions)
endif()

add_executable(picobin_test picobin_test.cpp)
target_include_directories(picobin_test PRIVATE ${_chip_src})
target_compile_options(picobin_test PRIVATE ${_warnings})
add_test(NAME picobin_test COMMAND picobin_test)

# chip/PicobinReader.hpp: the boot ROM's block walk, against picotool's tables and a picotool-sealed image (data/) and a
# fuzz of Walker fed in pieces; under ASan + UBSan, its reads into the ROM's buffer are what it is about
add_executable(reader_test reader_test.cpp)
target_include_directories(reader_test PRIVATE ${_chip_src})
target_compile_options(reader_test PRIVATE ${_warnings} -fsanitize=address,undefined -fno-sanitize-recover=all)
target_link_options(reader_test PRIVATE -fsanitize=address,undefined)
target_compile_definitions(reader_test PRIVATE PICOBIN_TEST_DATA="${CMAKE_CURRENT_LIST_DIR}/data")
add_test(NAME reader_test COMMAND reader_test)

# chip/PicobinSeal.hpp: images sealed as A/B updates and walked back (the reader judges them), picotool's sealed images
# among them; the compile-time seal is a static_assert
add_executable(seal_test seal_test.cpp)
target_include_directories(seal_test PRIVATE ${_chip_src})
target_compile_options(seal_test PRIVATE ${_warnings} -fsanitize=address,undefined -fno-sanitize-recover=all)
target_link_options(seal_test PRIVATE -fsanitize=address,undefined)
target_compile_definitions(seal_test PRIVATE PICOBIN_TEST_DATA="${CMAKE_CURRENT_LIST_DIR}/data")
add_test(NAME seal_test COMMAND seal_test)

# ---- blocks that must not compile
file(GLOB _bad CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/bad/*_fail.cpp)
foreach(_file IN LISTS _bad)
    get_filename_component(_name ${_file} NAME_WE)
    # the expected message is read here: a change to it has to re-run cmake
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS ${_file})
    file(
        STRINGS ${_file} _expect
        REGEX "^// expect: "
        LIMIT_COUNT 1)
    string(REPLACE "// expect: " "" _expect "${_expect}")
    if(_expect STREQUAL "")
        message(FATAL_ERROR "${_file} has no '// expect: <regex>' line")
    endif()
    set(_target bad_${_name})
    add_library(${_target} OBJECT EXCLUDE_FROM_ALL ${_file})
    target_include_directories(${_target} PRIVATE ${_chip_src})
    add_test(NAME ${_target} COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${_target})
    # the build must fail, and for the reason named: a plain WILL_FAIL would pass a failure for any reason. Each case
    # runs a build of this very tree: two at once (ctest -j) corrupt its .ninja_deps.
    set_tests_properties(${_target} PROPERTIES PASS_REGULAR_EXPRESSION "${_expect}" RESOURCE_LOCK build_tree)
endforeach()
