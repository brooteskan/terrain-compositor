set(integration "${CMAKE_CURRENT_LIST_DIR}/../..")
set(generator "${integration}/EngineOverrides.cmake")
set(first "TerrainRenderer/TerrainMeshManager.h")
file(MAKE_DIRECTORY "${TC_TEST_ROOT}/wrong-input/TerrainRenderer")
file(COPY "${TC_ENGINE_TERRAIN_ROOT}/${first}" DESTINATION "${TC_TEST_ROOT}/wrong-input/TerrainRenderer")
file(APPEND "${TC_TEST_ROOT}/wrong-input/${first}" "\n// Incompatible engine input.\n")

file(READ "${generator}" altered_generator)
string(REGEX MATCH "TerrainRenderer/TerrainMeshManager.h\\|[a-f0-9]+\\|([a-f0-9]+)" first_override "${altered_generator}")
if(NOT first_override)
    message(FATAL_ERROR "Cannot locate the generated manager-header hash for the rejection test")
endif()
string(REPLACE
    "${CMAKE_MATCH_1}"
    "0000000000000000000000000000000000000000000000000000000000000000"
    altered_generator "${altered_generator}")
file(MAKE_DIRECTORY "${TC_TEST_ROOT}/wrong-output")
file(WRITE "${TC_TEST_ROOT}/wrong-output/EngineOverrides.cmake" "${altered_generator}")
file(COPY "${integration}/EnginePatches" DESTINATION "${TC_TEST_ROOT}/wrong-output")

file(MAKE_DIRECTORY "${TC_TEST_ROOT}/wrong-query/TerrainSystem")
foreach(relative IN ITEMS TerrainRenderer/TerrainFeatureProcessor.h TerrainRenderer/TerrainDetailMaterialManager.h
    TerrainRenderer/TerrainMeshManager.h TerrainRenderer/TerrainMeshManager.cpp
    TerrainRenderer/TerrainDetailMaterialManager.cpp
    TerrainRenderer/TerrainFeatureProcessor.cpp TerrainRaycast/TerrainRaycastContext.cpp
    Components/TerrainPhysicsColliderComponent.h Components/TerrainPhysicsColliderComponent.cpp
    TerrainSystem/TerrainSystem.cpp)
    get_filename_component(directory "${TC_TEST_ROOT}/wrong-query/${relative}" DIRECTORY)
    file(MAKE_DIRECTORY "${directory}")
    configure_file("${TC_ENGINE_TERRAIN_ROOT}/${relative}" "${TC_TEST_ROOT}/wrong-query/${relative}" COPYONLY)
endforeach()
file(APPEND "${TC_TEST_ROOT}/wrong-query/TerrainSystem/TerrainSystem.cpp" "\n// Changed grid/sampler contract.\n")

foreach(case IN ITEMS valid repeat wrong-input wrong-output wrong-query)
    set(script "${generator}")
    set(upstream "${TC_ENGINE_TERRAIN_ROOT}")
    if(case STREQUAL "wrong-input")
        set(upstream "${TC_TEST_ROOT}/wrong-input")
        set(expected_error "pinned engine input changed")
    elseif(case STREQUAL "wrong-output")
        set(script "${TC_TEST_ROOT}/wrong-output/EngineOverrides.cmake")
        set(expected_error "generated override does not match")
    elseif(case STREQUAL "wrong-query")
        set(upstream "${TC_TEST_ROOT}/wrong-query")
        set(expected_error "pinned terrain query input changed")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DTC_ENGINE_TERRAIN_ROOT=${upstream}"
            "-DTC_OUTPUT_ROOT=${TC_TEST_ROOT}/generated" -P "${script}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(case MATCHES "^(valid|repeat)$")
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "${case}: ${output}\n${error}")
        endif()
        file(TIMESTAMP "${TC_TEST_ROOT}/generated/${first}" timestamp "%Y-%m-%dT%H:%M:%S.%f")
        if(case STREQUAL "repeat" AND NOT timestamp STREQUAL first_timestamp)
            message(FATAL_ERROR "Unchanged output was rewritten")
        endif()
        set(first_timestamp "${timestamp}")
    elseif(result EQUAL 0 OR NOT error MATCHES "${expected_error}")
        message(FATAL_ERROR "${case} did not reject the mismatched hash: ${output}\n${error}")
    endif()
endforeach()
