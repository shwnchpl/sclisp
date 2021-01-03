########################################################################
# Copyright 2020 Shawn M. Chapla
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
########################################################################

cmake_minimum_required(VERSION 3.1.3)

project(sclisp C)

option(BUILD_REPL "Build repl/ directory" OFF)
option(BUILD_SHARED_LIBS "Build shared library" ON)
option(BUILD_TESTS "Build tests/ directory" OFF)
option(BUILD_FMOD_SUPPORT "Build support for floating point modulo" ON)

set(LIB_MAJOR_VERSION 0)
set(LIB_MINOR_VERSION 2)
set(LIB_REVISION 2)
set(LIB_VERSION "${LIB_MAJOR_VERSION}.${LIB_MINOR_VERSION}.${LIB_REVISION}")
math(EXPR LIB_VERSION_NUMBER
    "${LIB_MAJOR_VERSION} * 1000000 + \
    ${LIB_MINOR_VERSION} * 1000 + \
    ${LIB_REVISION}"
)

set(BUILD_NEEDS_LIBM 0)
set(WILL_SUPPORT_FMOD 0)

if(BUILD_FMOD_SUPPORT)
    include(CheckFunctionExists)
    include(CheckLibraryExists)

    set(WILL_SUPPORT_FMOD 1)

    check_function_exists(fmod HAVE_FMOD)
    if(NOT HAVE_FMOD)
        check_library_exists(m fmod "" LIBM_AVAILABLE)
        if(NOT LIBM_AVAILABLE)
            message(WARNING
                "fmod not available; cannot support floating point modulo.")
            set(WILL_SUPPORT_FMOD 0)
        else()
            set(BUILD_NEEDS_LIBM 1)
        endif()
    endif()
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Defaulting to 'Release' build type.")
    set(CMAKE_BUILD_TYPE "Release" CACHE
        STRING "Build type." FORCE
    )

    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY
        STRINGS "Debug" "Release"
    )
endif()

add_library(sclisp
    src/sclisp.c
)

target_include_directories(sclisp
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

if(BUILD_NEEDS_LIBM)
    target_link_libraries(sclisp m)
endif()

# TODO: Consider moving these into some kind of generated
# config header?
target_compile_definitions(sclisp
    PRIVATE $<$<CONFIG:Debug>:SCLISP_DEBUG_BUILD>
    PRIVATE SCLISP_LIB_VERSION="${LIB_VERSION}"
    PRIVATE SCLISP_LIB_VERSION_NUMBER=${LIB_VERSION_NUMBER}
    PRIVATE SCLISP_FMOD_SUPPORT=${WILL_SUPPORT_FMOD}
)

if (MSVC)
    target_compile_options(sclisp PRIVATE /W4)
else()
    target_compile_options(sclisp PRIVATE -Wall -Wextra -pedantic)
endif()

set_target_properties(sclisp PROPERTIES
    OUTPUT_NAME "sclisp"
    VERSION ${LIB_VERSION}
    SOVERSION ${LIB_MAJOR_VERSION}
    C_STANDARD 90
)

if(BUILD_TESTS)
    add_executable(sclisp-tests
        tests/main.c
        tests/internal.c
        tests/external.c
    )

    # TODO: Consider moving these into some kind of generated
    # config header?
    target_compile_definitions(sclisp-tests
        PRIVATE $<$<CONFIG:Debug>:SCLISP_DEBUG_BUILD>
        PRIVATE SCLISP_LIB_VERSION="${LIB_VERSION}"
        PRIVATE SCLISP_LIB_VERSION_NUMBER=${LIB_VERSION_NUMBER}
        PRIVATE SCLISP_FMOD_SUPPORT=${WILL_SUPPORT_FMOD}
    )

    if (MSVC)
        target_compile_options(sclisp-tests PRIVATE /W4)
    else()
        target_compile_options(sclisp-tests PRIVATE -Wall -Wextra -pedantic)
    endif()

    target_include_directories(sclisp-tests
        PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/test>
    )

    if(BUILD_NEEDS_LIBM)
        target_link_libraries(sclisp-tests m)
    endif()

    set_target_properties(sclisp-tests PROPERTIES C_STANDARD 90)
endif()

if(BUILD_REPL)
    add_executable(sclisp-repl
        repl/sclisp-repl.c
    )

    if (MSVC)
        target_compile_options(sclisp-repl PRIVATE /W4)
    else()
        target_compile_options(sclisp-repl PRIVATE -Wall -Wextra -pedantic)
    endif()

    target_include_directories(sclisp-repl
        PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/repl>
    )
    target_link_libraries(sclisp-repl PRIVATE sclisp readline)

    set_target_properties(sclisp-repl PROPERTIES C_STANDARD 90)
endif()

# TODO: Add install logic.
