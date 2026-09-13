set(gtest_force_shared_crt ON)
set(BUILD_GMOCK OFF)
set(INSTALL_GTEST OFF)
wz_add_git_submodule("${PROJECT_SOURCE_DIR}" external/googletest GTest::gtest_main)

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

add_executable(
    polytree_test_traversal
    graph/static_polytree_traversal_tests.cpp
)
target_link_libraries(
    polytree_test_traversal
    PRIVATE
        polytree::polytree
        GTest::gtest_main
)
add_test(NAME polytree.traversal COMMAND polytree_test_traversal)
set_tests_properties(polytree.traversal PROPERTIES LABELS current)

add_executable(
    polytree_test_mutable
    graph/mutable_polytree_tests.cpp
)
target_link_libraries(
    polytree_test_mutable
    PRIVATE
        polytree::polytree
        GTest::gtest_main
)
add_test(NAME polytree.mutable COMMAND polytree_test_mutable)
set_tests_properties(polytree.mutable PROPERTIES LABELS current)

add_test(
    NAME polytree.no_handwritten_loops
    COMMAND
        ${CMAKE_COMMAND}
        -DPOLYTREE_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P
        ${PROJECT_SOURCE_DIR}/tools/check_control_flow_policy.cmake
)
set_tests_properties(polytree.no_handwritten_loops PROPERTIES LABELS policy)

add_polytree_baseline_test(polytree_test_ancestor_closure graph/ancestor_closure_tests.cpp polytree.ancestor_closure)

set_tests_properties(polytree.ancestor_closure PROPERTIES LABELS current)
