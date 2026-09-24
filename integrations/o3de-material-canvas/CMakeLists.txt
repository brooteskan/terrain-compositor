o3de_gem_setup("TerrainCompositorCanvas")

ly_add_target(
    NAME ${gem_name}.Static STATIC NAMESPACE Gem
    FILES_CMAKE terrain_canvas_files.cmake
    INCLUDE_DIRECTORIES PUBLIC Code/Source
    BUILD_DEPENDENCIES PUBLIC AZ::AzFramework Gem::TerrainCompositor.Static)
ly_add_target(
    NAME ${gem_name} ${PAL_TRAIT_MONOLITHIC_DRIVEN_MODULE_TYPE} NAMESPACE Gem
    FILES_CMAKE terrain_canvas_module_files.cmake
    BUILD_DEPENDENCIES PRIVATE Gem::${gem_name}.Static)
if(PAL_TRAIT_BUILD_HOST_TOOLS)
    include(EngineOverrides.cmake)
    set(tc_canvas_source_root "${LY_ROOT_FOLDER}/Gems/Atom/Tools/MaterialCanvas/Code")
    set(tc_canvas_original "${tc_canvas_source_root}/Source/Document/MaterialGraphCompiler.cpp")
    set(tc_canvas_override "${CMAKE_CURRENT_BINARY_DIR}/EngineOverrides/MaterialGraphCompiler.cpp")
    tc_canvas_prepare_compiler("${tc_canvas_original}" "${tc_canvas_override}")
    get_target_property(tc_canvas_sources MaterialCanvas SOURCES)
    set(tc_canvas_found FALSE)
    foreach(source IN LISTS tc_canvas_sources)
        get_filename_component(absolute "${source}" ABSOLUTE BASE_DIR "${tc_canvas_source_root}")
        if(absolute STREQUAL tc_canvas_original)
            list(REMOVE_ITEM tc_canvas_sources "${source}")
            set(tc_canvas_found TRUE)
        endif()
    endforeach()
    if(NOT tc_canvas_found)
        message(FATAL_ERROR "Terrain Canvas: could not locate native MaterialGraphCompiler source")
    endif()
    set_source_files_properties("${tc_canvas_override}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    list(APPEND tc_canvas_sources "${tc_canvas_override}")
    set_property(TARGET MaterialCanvas PROPERTY SOURCES "${tc_canvas_sources}")
    target_include_directories(MaterialCanvas PRIVATE "${tc_canvas_source_root}/Source/Document")
    ly_add_target(
        NAME ${gem_name}.Editor GEM_MODULE NAMESPACE Gem
        FILES_CMAKE terrain_canvas_module_files.cmake
        COMPILE_DEFINITIONS PRIVATE TERRAIN_CANVAS_EDITOR
        BUILD_DEPENDENCIES PRIVATE Gem::${gem_name}.Static AZ::AzToolsFramework)
    ly_create_alias(NAME ${gem_name}.Tools NAMESPACE Gem TARGETS Gem::${gem_name}.Editor)
    ly_create_alias(NAME ${gem_name}.Builders NAMESPACE Gem TARGETS Gem::${gem_name}.Editor)
endif()
ly_create_alias(NAME ${gem_name}.Clients NAMESPACE Gem TARGETS Gem::${gem_name})
ly_create_alias(NAME ${gem_name}.Servers NAMESPACE Gem TARGETS Gem::${gem_name})
ly_create_alias(NAME ${gem_name}.Unified NAMESPACE Gem TARGETS Gem::${gem_name})
o3de_add_variant_dependencies_for_gem_dependencies(
    GEM_NAME ${gem_name} VARIANTS Builders Tools Clients Servers Unified)
