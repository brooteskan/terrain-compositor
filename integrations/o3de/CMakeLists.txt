o3de_gem_setup("TerrainCompositor")

# These integration copies are based on O3DE revision
# 061180bf24f1666eb30315b35da292eb14f4659c. They keep render topology,
# per-scene gap resources, terrain raycasts, and heightfield collision aligned
# without requiring edits in the external engine checkout.
set(tc_terrain_mesh_manager_original "${LY_ROOT_FOLDER}/Gems/Terrain/Code/Source/TerrainRenderer/TerrainMeshManager.cpp")
set(tc_terrain_mesh_manager_override "${CMAKE_CURRENT_SOURCE_DIR}/EngineOverrides/TerrainRenderer/TerrainMeshManager.cpp")
set(tc_terrain_feature_processor_original "${LY_ROOT_FOLDER}/Gems/Terrain/Code/Source/TerrainRenderer/TerrainFeatureProcessor.cpp")
set(tc_terrain_feature_processor_override "${CMAKE_CURRENT_SOURCE_DIR}/EngineOverrides/TerrainRenderer/TerrainFeatureProcessor.cpp")
set(tc_terrain_raycast_original "${LY_ROOT_FOLDER}/Gems/Terrain/Code/Source/TerrainRaycast/TerrainRaycastContext.cpp")
set(tc_terrain_raycast_override "${CMAKE_CURRENT_SOURCE_DIR}/EngineOverrides/TerrainRaycast/TerrainRaycastContext.cpp")
set(tc_terrain_physics_collider_original "${LY_ROOT_FOLDER}/Gems/Terrain/Code/Source/Components/TerrainPhysicsColliderComponent.cpp")
set(tc_terrain_physics_collider_override "${CMAKE_CURRENT_SOURCE_DIR}/EngineOverrides/Components/TerrainPhysicsColliderComponent.cpp")

if(TARGET Terrain.Static)
    get_target_property(tc_terrain_static_sources Terrain.Static SOURCES)
    set(tc_terrain_static_filtered_sources)
    set(tc_terrain_mesh_manager_removed FALSE)
    set(tc_terrain_feature_processor_removed FALSE)
    set(tc_terrain_raycast_removed FALSE)
    set(tc_terrain_physics_collider_removed FALSE)
    foreach(tc_terrain_source IN LISTS tc_terrain_static_sources)
        get_filename_component(
            tc_terrain_source_absolute "${tc_terrain_source}" ABSOLUTE
            BASE_DIR "${LY_ROOT_FOLDER}/Gems/Terrain/Code")
        if(tc_terrain_source_absolute STREQUAL tc_terrain_mesh_manager_original)
            set(tc_terrain_mesh_manager_removed TRUE)
        elseif(tc_terrain_source_absolute STREQUAL tc_terrain_feature_processor_original)
            set(tc_terrain_feature_processor_removed TRUE)
        elseif(tc_terrain_source_absolute STREQUAL tc_terrain_raycast_original)
            set(tc_terrain_raycast_removed TRUE)
        elseif(tc_terrain_source_absolute STREQUAL tc_terrain_physics_collider_original)
            set(tc_terrain_physics_collider_removed TRUE)
        else()
            list(APPEND tc_terrain_static_filtered_sources "${tc_terrain_source}")
        endif()
    endforeach()
    if(NOT tc_terrain_mesh_manager_removed OR NOT tc_terrain_feature_processor_removed OR
       NOT tc_terrain_raycast_removed OR NOT tc_terrain_physics_collider_removed)
        message(FATAL_ERROR "Could not locate all required upstream terrain integration sources in Terrain.Static")
    endif()

    set_property(TARGET Terrain.Static PROPERTY SOURCES "${tc_terrain_static_filtered_sources}")
    set_source_files_properties("${tc_terrain_mesh_manager_original}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON HEADER_FILE_ONLY ON)
    set_source_files_properties("${tc_terrain_mesh_manager_override}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    target_sources(Terrain.Static PRIVATE "${tc_terrain_mesh_manager_override}")
    set_source_files_properties("${tc_terrain_feature_processor_original}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON HEADER_FILE_ONLY ON)
    set_source_files_properties("${tc_terrain_feature_processor_override}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    target_sources(Terrain.Static PRIVATE "${tc_terrain_feature_processor_override}")
    set_source_files_properties("${tc_terrain_raycast_original}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON HEADER_FILE_ONLY ON)
    set_source_files_properties("${tc_terrain_raycast_override}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    target_sources(Terrain.Static PRIVATE "${tc_terrain_raycast_override}")
    set_source_files_properties("${tc_terrain_physics_collider_original}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON HEADER_FILE_ONLY ON)
    set_source_files_properties("${tc_terrain_physics_collider_override}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    target_sources(Terrain.Static PRIVATE "${tc_terrain_physics_collider_override}")
    target_include_directories(
        Terrain.Static BEFORE PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/EngineOverrides"
        "${CMAKE_CURRENT_SOURCE_DIR}/Code/Include")
else()
    message(FATAL_ERROR "TerrainCompositor requires the Terrain.Static target before applying its render-query integration")
endif()

ly_add_target(
    NAME ${gem_name}.Static STATIC
    NAMESPACE Gem
    FILES_CMAKE terrain_compositor_files.cmake
    INCLUDE_DIRECTORIES
        PUBLIC Code/Include ${LY_ROOT_FOLDER}/Gems/Terrain/Code/Source
        PRIVATE EngineOverrides
    BUILD_DEPENDENCIES
        PRIVATE
            AZ::AzGameFramework
            Gem::Atom_AtomBridge.Static
        PUBLIC
            AZ::AzFramework
            Gem::Atom_RPI.Public
            Gem::Atom_Feature_Common.Public
            Gem::CommonFeaturesAtom.Public
            Gem::GradientSignal.Static
            Gem::LmbrCentral.API
            Gem::Terrain.Static
)

ly_add_target(
    NAME ${gem_name} ${PAL_TRAIT_MONOLITHIC_DRIVEN_MODULE_TYPE}
    NAMESPACE Gem
    FILES_CMAKE terrain_compositor_shared_files.cmake
    INCLUDE_DIRECTORIES PRIVATE Code/Include Code/Source
    BUILD_DEPENDENCIES PRIVATE Gem::${gem_name}.Static AZ::AzCore
)

ly_add_source_properties(
    SOURCES Code/Source/TerrainCompositorModule.cpp
    PROPERTY COMPILE_DEFINITIONS
    VALUES O3DE_GEM_NAME=${gem_name} O3DE_GEM_VERSION=${gem_version}
)

if(PAL_TRAIT_BUILD_HOST_TOOLS)
    ly_add_target(
        NAME ${gem_name}.Editor GEM_MODULE
        NAMESPACE Gem
        FILES_CMAKE terrain_compositor_editor_files.cmake
        INCLUDE_DIRECTORIES
            PRIVATE Code/Include Code/Source EngineOverrides ${LY_ROOT_FOLDER}/Gems/Terrain/Code/Source
        BUILD_DEPENDENCIES PRIVATE Gem::${gem_name}.Static AZ::AzToolsFramework
    )
    ly_add_source_properties(
        SOURCES Code/Source/Editor/TerrainCompositorEditorModule.cpp
        PROPERTY COMPILE_DEFINITIONS
        VALUES O3DE_GEM_NAME=${gem_name} O3DE_GEM_VERSION=${gem_version}
    )
    ly_create_alias(NAME ${gem_name}.Builders NAMESPACE Gem TARGETS Gem::${gem_name}.Editor)
    ly_create_alias(NAME ${gem_name}.Tools NAMESPACE Gem TARGETS Gem::${gem_name}.Editor)
endif()

ly_create_alias(NAME ${gem_name}.Clients NAMESPACE Gem TARGETS Gem::${gem_name})
ly_create_alias(NAME ${gem_name}.Servers NAMESPACE Gem TARGETS Gem::${gem_name})
ly_create_alias(NAME ${gem_name}.Unified NAMESPACE Gem TARGETS Gem::${gem_name})

o3de_add_variant_dependencies_for_gem_dependencies(
    GEM_NAME ${gem_name}
    VARIANTS Builders Tools Clients Servers Unified
)

if(PAL_TRAIT_BUILD_TESTS_SUPPORTED)
    ly_add_target(
        NAME ${gem_name}.Tests ${PAL_TRAIT_TEST_TARGET_TYPE}
        NAMESPACE Gem
        FILES_CMAKE Code/Tests/terrain_compositor_tests_files.cmake
        INCLUDE_DIRECTORIES
            PRIVATE
                Code/Include
                Code/Source
                EngineOverrides
                ${LY_ROOT_FOLDER}/Gems/Terrain/Code/Source
                ${LY_ROOT_FOLDER}/Gems/Terrain/Code/Mocks
                ${LY_ROOT_FOLDER}/Gems/LmbrCentral/Code/Mocks
        BUILD_DEPENDENCIES
            PRIVATE
                AZ::AzTest
                AZ::AzCore
                AZ::AzFrameworkTestShared
                Gem::GradientSignal.Static
                Gem::${gem_name}.Static
    )

    set_source_files_properties(Code/Tests/TerrainMeshHeightGapGpuTests.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    target_compile_definitions(
        ${gem_name}.Tests PRIVATE
        TERRAIN_COMPOSITOR_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Assets/Shaders/Terrain")
    if(PAL_PLATFORM_NAME STREQUAL "Windows")
        target_link_libraries(${gem_name}.Tests PRIVATE d3d11 d3dcompiler)
    endif()

    ly_add_googletest(NAME Gem::${gem_name}.Tests LABELS REQUIRES_tiaf)
endif()
