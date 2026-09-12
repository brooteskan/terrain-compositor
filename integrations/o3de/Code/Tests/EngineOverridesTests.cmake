set(integration "${CMAKE_CURRENT_LIST_DIR}/../..")
set(generator "${integration}/EngineOverrides.cmake")
set(first "TerrainRenderer/TerrainMeshManager.h")
file(MAKE_DIRECTORY "${TC_TEST_ROOT}/wrong-input/TerrainRenderer")
file(COPY "${TC_ENGINE_TERRAIN_ROOT}/${first}" DESTINATION "${TC_TEST_ROOT}/wrong-input/TerrainRenderer")
file(APPEND "${TC_TEST_ROOT}/wrong-input/${first}" "\n// Incompatible engine input.\n")

file(READ "${generator}" altered_generator)
string(REPLACE
    "04fda8c2520579594d8031ebbf54c5b388128ea59155174653e257929a3aaf02"
    "0000000000000000000000000000000000000000000000000000000000000000"
    altered_generator "${altered_generator}")
file(MAKE_DIRECTORY "${TC_TEST_ROOT}/wrong-output")
file(WRITE "${TC_TEST_ROOT}/wrong-output/EngineOverrides.cmake" "${altered_generator}")
file(COPY "${integration}/EnginePatches" DESTINATION "${TC_TEST_ROOT}/wrong-output")

foreach(case IN ITEMS valid repeat wrong-input wrong-output)
    set(script "${generator}")
    set(upstream "${TC_ENGINE_TERRAIN_ROOT}")
    if(case STREQUAL "wrong-input")
        set(upstream "${TC_TEST_ROOT}/wrong-input")
        set(expected_error "pinned engine input changed")
    elseif(case STREQUAL "wrong-output")
        set(script "${TC_TEST_ROOT}/wrong-output/EngineOverrides.cmake")
        set(expected_error "generated override does not match")
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
