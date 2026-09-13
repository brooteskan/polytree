cmake_minimum_required(VERSION 3.24)

project(
    polytree
    VERSION 0.2.1
    DESCRIPTION "Mutable authoring and compact static polytree topology"
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
option(
    POLYTREE_BUILD_BENCHMARKS
    "Build standalone mutable-freeze benchmarks"
    OFF
)
set(_polytree_submodule_helper "${CMAKE_CURRENT_SOURCE_DIR}/external/algo/cmake/GitSubmodule.cmake")
if(NOT EXISTS "${_polytree_submodule_helper}")
    message(FATAL_ERROR "Missing algo submodule. Run git submodule update --init --recursive")
endif()
include("${_polytree_submodule_helper}")
set(ALGO_BUILD_TESTS OFF)
wz_add_git_submodule("${CMAKE_CURRENT_SOURCE_DIR}" external/algo algo::algo)

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

if(POLYTREE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
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
