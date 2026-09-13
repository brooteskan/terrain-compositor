include_guard(GLOBAL)

function(terrain_compositor_require_dependencies)
    get_filename_component(repository "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(helper "${repository}/external/polytree/external/algo/cmake/GitSubmodule.cmake")
    if(NOT EXISTS "${helper}")
        message(FATAL_ERROR "Missing dependency submodules. Run git submodule update --init --recursive")
    endif()
    include("${helper}")
    set(ALGO_BUILD_TESTS OFF)
    set(POLYTREE_BUILD_TESTS OFF)
    set(POLYTREE_BUILD_BENCHMARKS OFF)
    wz_add_git_submodule("${repository}" external/polytree polytree::polytree)
    # Also validate the transitive algo pin when polytree was already provided
    # by another consumer, so target reuse cannot hide a conflicting gitlink.
    wz_add_git_submodule("${repository}/external/polytree" external/algo algo::algo)
endfunction()
