# Inputs are pinned to O3DE 061180bf24f1666eb30315b35da292eb14f4659c.
# Only private build-directory files are modified; original license notices are retained.
function(tc_prepare_engine_overrides upstream_root output_root)
    find_package(Git REQUIRED)
    set(overrides
        "TerrainRenderer/TerrainMeshManager.h|19260ae06c587e2173df0ca5671a9f061d3b50a75a4e2c794b335885a1691f9b|fc3b4a128e471b47e7a9e7317210589d196dd557c81d3c96ca28330f784ccd99"
        "TerrainRenderer/TerrainDetailMaterialManager.cpp|23bc8e3381bcda365bd41982d2a8ff8b70c9a6b694ef7b63b5a9341c6c5a1e92|0fe7ba73787019a23e5bd1cfe3e1ca5961c4ae936cba9adb08bc330a9072395b"
        "TerrainRenderer/TerrainMeshManager.cpp|0cd52bf650755c4c479a842fc31c41645808ee0e9a9fc7253326b57d457b83eb|c62c6a83c008350c9a9e2a68395056538927363606f50d99e8fca69aea05df8c"
        "TerrainRenderer/TerrainFeatureProcessor.cpp|1d1ea74a882de2c48864179f56234c2c39dd9ca1e88f7902f5ccb27598fdaf5c|9b311c22c1a67c66cbb70257364e6c7230200acb002c25bb7356370a55c58d1c"
        "TerrainRaycast/TerrainRaycastContext.cpp|67bbd8e633136e9bbd2676bdcb19a76f735912559b85b6ebc3c173df881a2d92|ca0ac838425cbda646d9921df6077914a4a3c7ca71ac30364864499a1c7b5fc5"
        "Components/TerrainPhysicsColliderComponent.h|178852bddc0df6a499428bfded053475d9629b5a4b5448e7591e10e08a41ece5|a6f17d8784e66398b1c2cd4d7031222abb03ad6da01cf8a7f0ac04833e5614d3"
        "Components/TerrainPhysicsColliderComponent.cpp|58efa6213de6ee8545b810a0ac6f93d1e62156f2ee7fc064b0c62b6072ef1b05|c9337dd4a2f6580f00c17ac35f618f1de4c5642eb4f0ed3a0367518cec2ea725"
    )
    foreach(entry IN LISTS overrides)
        string(REPLACE "|" ";" fields "${entry}")
        list(GET fields 0 relative)
        list(GET fields 1 input_hash)
        list(GET fields 2 output_hash)
        set(source "${upstream_root}/${relative}")
        set(patch "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EnginePatches/${relative}.patch")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}" "${patch}")
        file(READ "${source}" content)
        string(REPLACE "\r\n" "\n" content "${content}")
        string(SHA256 actual "${content}")
        if(NOT actual STREQUAL input_hash)
            message(FATAL_ERROR "TerrainCompositor: pinned engine input changed: ${source}")
        endif()
        set(staging "${output_root}/staging")
        get_filename_component(directory "${staging}/${relative}" DIRECTORY)
        file(MAKE_DIRECTORY "${directory}")
        file(WRITE "${staging}/${relative}" "${content}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env --unset=GIT_DIR --unset=GIT_WORK_TREE
                "GIT_CEILING_DIRECTORIES=${output_root}"
                "${GIT_EXECUTABLE}" -c core.autocrlf=true apply --whitespace=nowarn "${patch}"
            WORKING_DIRECTORY "${staging}"
            RESULT_VARIABLE result ERROR_VARIABLE error)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "TerrainCompositor: cannot apply ${patch}: ${error}")
        endif()
        file(READ "${staging}/${relative}" content)
        string(REPLACE "\r\n" "\n" content "${content}")
        string(SHA256 actual "${content}")
        if(NOT actual STREQUAL output_hash)
            message(FATAL_ERROR "TerrainCompositor: generated override does not match its pinned output: ${relative}")
        endif()
        configure_file("${staging}/${relative}" "${output_root}/${relative}" COPYONLY)
    endforeach()
    # The retained-only renderer proof depends on QueryRegion returning original
    # grid XY (including float rounding), regardless of its ordinary sampler.
    set(query_source "${upstream_root}/TerrainSystem/TerrainSystem.cpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${query_source}")
    file(READ "${query_source}" query_content)
    string(REPLACE "\r\n" "\n" query_content "${query_content}")
    string(SHA256 query_hash "${query_content}")
    if(NOT query_hash STREQUAL "e4a2dd3b54921c68f270376c199a517c6b4af0d9de61b7faaf3f81d4891cce5e")
        message(FATAL_ERROR "TerrainCompositor: pinned terrain query input changed: ${query_source}")
    endif()
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    tc_prepare_engine_overrides("${TC_ENGINE_TERRAIN_ROOT}" "${TC_OUTPUT_ROOT}")
endif()
