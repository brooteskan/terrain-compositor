# TG's consumer artifacts are maintained in the submodule. A submodule update
# triggers normal CMake reconfiguration and refreshes these managed copies.
function(tc_sync_tg_projects)
    set(bundle "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    file(GLOB_RECURSE inputs CONFIGURE_DEPENDS "${bundle}/project/*")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${inputs} "${bundle}/manifest.json" "${bundle}/SyncProject.py")
    foreach(project IN LISTS LY_PROJECTS)
        file(REAL_PATH "${project}" absolute BASE_DIRECTORY "${LY_ROOT_FOLDER}")
        if(EXISTS "${absolute}/Gem/Source/Editor/VueLiveLink/terrain_normals.py")
            find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
            execute_process(COMMAND "${Python3_EXECUTABLE}" "${bundle}/SyncProject.py" --project "${absolute}"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
            if(NOT result EQUAL 0)
                message(FATAL_ERROR "TerrainCompositor: TG consumer sync failed: ${output}${error}")
            endif()
            message(STATUS "TerrainCompositor: TG consumer files synchronized from the submodule")
        endif()
    endforeach()
endfunction()

# O3DE project editor targets do not automatically build dynamically loaded Gems.
# Resolve targets after all Gems/projects have been declared.
function(tc_wire_tg_editor_build)
    if(TARGET TGProject.Editor AND TARGET TerrainCompositor.Editor)
        add_dependencies(TGProject.Editor TerrainCompositor.Editor)
        # Terrain.Editor owns the renderer feature processor. Engine override
        # changes must also rebuild its embedded Terrain.Static implementation.
        if(TARGET Terrain.Editor)
            add_dependencies(TGProject.Editor Terrain.Editor)
        endif()
        if(TARGET TerrainCompositorCanvas.Editor)
            add_dependencies(TGProject.Editor TerrainCompositorCanvas.Editor)
        endif()
    endif()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL tc_wire_tg_editor_build)
