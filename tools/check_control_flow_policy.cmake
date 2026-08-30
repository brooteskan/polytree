if(NOT DEFINED POLYTREE_SOURCE_DIR)
    message(FATAL_ERROR "POLYTREE_SOURCE_DIR is required")
endif()

file(
    GLOB_RECURSE _polytree_sources
    LIST_DIRECTORIES FALSE
    "${POLYTREE_SOURCE_DIR}/include/*.h"
    "${POLYTREE_SOURCE_DIR}/include/*.hh"
    "${POLYTREE_SOURCE_DIR}/include/*.hpp"
    "${POLYTREE_SOURCE_DIR}/tests/*.c"
    "${POLYTREE_SOURCE_DIR}/tests/*.cc"
    "${POLYTREE_SOURCE_DIR}/tests/*.cpp"
    "${POLYTREE_SOURCE_DIR}/tests/*.cxx"
    "${POLYTREE_SOURCE_DIR}/benchmarks/*.c"
    "${POLYTREE_SOURCE_DIR}/benchmarks/*.cc"
    "${POLYTREE_SOURCE_DIR}/benchmarks/*.cpp"
    "${POLYTREE_SOURCE_DIR}/benchmarks/*.cxx"
)

# static_dag.h is retained only as compatibility with the immutable extraction.
# The polytree implementation no longer includes it or depends on its traversal.
list(FILTER _polytree_sources EXCLUDE REGEX "[/\\\\]static_dag\\.h$")

set(_polytree_violations "")
foreach(_polytree_source IN LISTS _polytree_sources)
    file(STRINGS "${_polytree_source}" _polytree_lines)
    set(_polytree_line_number 0)
    foreach(_polytree_line IN LISTS _polytree_lines)
        math(EXPR _polytree_line_number "${_polytree_line_number} + 1")
        if(_polytree_line MATCHES "(^|[^A-Za-z0-9_])(for|while)[ \t]*\\(" OR
            _polytree_line MATCHES "(^|[^A-Za-z0-9_])do[ \t]*\\{")
            file(RELATIVE_PATH _polytree_relative "${POLYTREE_SOURCE_DIR}" "${_polytree_source}")
            list(
                APPEND
                _polytree_violations
                "${_polytree_relative}:${_polytree_line_number}: ${_polytree_line}"
            )
        endif()
    endforeach()
endforeach()

if(_polytree_violations)
    list(JOIN _polytree_violations "\n" _polytree_failure_text)
    message(FATAL_ERROR "Handwritten C++ loop statements are not permitted:\n${_polytree_failure_text}")
endif()

message(STATUS "No handwritten C++ loop statements found in polytree sources or tests.")
