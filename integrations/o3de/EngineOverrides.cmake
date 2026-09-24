# Inputs are pinned to O3DE 061180bf24f1666eb30315b35da292eb14f4659c.
# Only private build-directory files are modified; original license notices are retained.
function(tc_prepare_engine_overrides upstream_root output_root)
    find_package(Git REQUIRED)
    set(overrides
        "EditorComponents/EditorTerrainSystemComponent.cpp|75e8501b2f407422ca243c3c1b967f1e6b6953d4f8ea11cdcb0b2eaae4fb4366|b9bd9bd969f6fe944e12e6558923e0fad1a461657d7375bb07743512f6762819"
        "EditorTerrainModule.cpp|51919513a47fc30fa11b15f80253f95689c3c98fa5addd7394bd25f4d414a539|6be981d52180d714b7868a0a2fdde40bc463f3481a4cc67ab0524d2ee338b84c"
        "TerrainModule.cpp|02abeb6c06ba4b37527c6e46147489d6206670da6dc010d6f44b903e6eabc333|ff2544377aa3e70f9843ab35e2069b4daeb5e3d31ee6c6d6459e8c2f32aacfbd"
        "Components/TerrainSystemComponent.cpp|7587424ba64ebe1d11e9fec68bd461d0208236ea86cbb8d3ee05842b7ec2f799|917c5bfdbe395fdd7141ee9b2ab6a9e3564a202e06fb76df5f08d70fb17c6890"
        "Components/TerrainWorldRendererComponent.h|8d41ba6bc477c0fcab30de48827f63415ae98c00da4af4ebb99861f5c0bfb215|542414af135c5f5f062e6966e2b0f5a917cfc1b53c82c598323462db34fc64c9"
        "Components/TerrainWorldRendererComponent.cpp|67aea67fef794d70013221e6f7747d3af81fc2fdf5865a5978c15756e041ad57|6101bf0259890c751a71c84fc707b1dbc7c66b76ea0a0ebe3719b2fb8f2da6b5"
        "TerrainRenderer/TerrainMeshManager.h|19260ae06c587e2173df0ca5671a9f061d3b50a75a4e2c794b335885a1691f9b|00c6157791e3eccf6a565ad517ab38639e9cf6089b37b8e2b66f13fbc4545d20"
        "TerrainRenderer/TerrainFeatureProcessor.h|a5a5b130e036d85706fa20b0cea5168d0e6db8b4229db0a57bd99b1330bed672|ab04c28e4221d19bb2ef9de25a8d6576387ba50d1559601a2cf6b0de6012b3ba"
        "TerrainRenderer/TerrainMeshManager.cpp|0cd52bf650755c4c479a842fc31c41645808ee0e9a9fc7253326b57d457b83eb|a5abfc06ae98572b700a0b568052994501e061027e5ce66017e4c07b9574e8eb"
        "TerrainRenderer/TerrainFeatureProcessor.cpp|1d1ea74a882de2c48864179f56234c2c39dd9ca1e88f7902f5ccb27598fdaf5c|5c56bcb4b5b3192a6cc3307ccf90a3d378b056cceb3b05e9f8a8b14e0262996b"
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
