cmake_minimum_required(VERSION 3.1.3)

project(sclisp C)

option(BUILD_TESTS "Build test/ directory" OFF)
option(BUILD_SHARED_LIBS "Build shared library" ON)

SET(LIB_MAJOR_VERSION 0)
SET(LIB_MINOR_VERSION 0)
SET(LIB_REVISION 1)
SET(LIB_VERSION "${LIB_MAJOR_VERSION}.${LIB_MINOR_VERSION}.${LIB_REVISION}")

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

target_compile_definitions(sclisp
    PRIVATE $<$<CONFIG:Debug>:SCLISP_DEBUG_BUILD>
)

set_target_properties(sclisp PROPERTIES
    OUTPUT_NAME "sclisp"
    VERSION ${LIB_VERSION}
    SOVERSION ${LIB_MAJOR_VERSION}
    C_STANDARD 90
)

if(BUILD_TESTS)
    add_executable(sclisp-tests
        test/sclisp-test.c
    )

    if (MSVC)
        target_compile_options(sclisp-tests PRIVATE /W4)
    else()
        target_compile_options(sclisp-tests PRIVATE -Wall -Wextra -pedantic)
    endif()

    target_include_directories(sclisp-tests
        PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/test>
    )
    target_link_libraries(sclisp-tests PRIVATE sclisp)

    set_target_properties(sclisp-tests PROPERTIES C_STANDARD 90)
endif()

# TODO: Add install logic.
