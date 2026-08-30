find_package(GTest CONFIG QUIET)

if(NOT TARGET GTest::gtest_main)
    include(FetchContent)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()

function(add_polytree_baseline_test target source test_name)
    add_executable(${target} ${source})
    target_link_libraries(
        ${target}
        PRIVATE
            polytree::polytree
            GTest::gtest_main
    )
    add_test(NAME ${test_name} COMMAND ${target})
    set_tests_properties(${test_name} PROPERTIES LABELS baseline)
endfunction()

add_polytree_baseline_test(
    polytree_test_core
    graph/static_polytree_tests_0.cpp
    polytree.baseline.core
)
add_polytree_baseline_test(
    polytree_test_algorithms
    graph/static_polytree_algo_tests_0.cpp
    polytree.baseline.algorithms
)
