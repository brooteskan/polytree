cmake_minimum_required(VERSION 3.24)

project(
    polytree
    VERSION 0.1.0
    DESCRIPTION "Compact static polytree with cached evaluation plans"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(_polytree_tests_default OFF)
if(PROJECT_IS_TOP_LEVEL)
    set(_polytree_tests_default ON)
endif()
option(
    POLYTREE_BUILD_TESTS
    "Build the Wozzits static-polytree baseline tests"
    ${_polytree_tests_default}
)
set(
    POLYTREE_ALGO_SOURCE_DIR
    ""
    CACHE PATH
    "Optional path to an algo source checkout"
)

if(NOT TARGET algo::algo AND POLYTREE_ALGO_SOURCE_DIR)
    add_subdirectory(
        "${POLYTREE_ALGO_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/_deps/algo-build"
        EXCLUDE_FROM_ALL
    )
endif()

if(NOT TARGET algo::algo)
    find_package(algo CONFIG QUIET)
endif()

if(NOT TARGET algo::algo)
    include(FetchContent)
    set(ALGO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        algo
        GIT_REPOSITORY https://github.com/brooteskan/algo.git
        GIT_TAG ea2e45f8c19ebe7deb44c85c91179e12fac57154
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(algo)
endif()

add_library(polytree INTERFACE)
add_library(polytree::polytree ALIAS polytree)
set_target_properties(polytree PROPERTIES EXPORT_NAME polytree)
target_compile_features(polytree INTERFACE cxx_std_20)
target_include_directories(
    polytree
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(
    polytree
    INTERFACE
        $<BUILD_INTERFACE:algo::algo>
        $<INSTALL_INTERFACE:algo::algo>
)

if(POLYTREE_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()

install(TARGETS polytree EXPORT polytree-targets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    EXPORT polytree-targets
    FILE polytree-targets.cmake
    NAMESPACE polytree::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/polytree
)

configure_package_config_file(
    cmake/polytree-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/polytree-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/polytree
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/polytree-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/polytree-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/polytree-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/polytree
)
