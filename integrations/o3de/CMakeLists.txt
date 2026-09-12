o3de_gem_setup("TerrainCompositor")

include(EngineOverrides.cmake)
set(tc_terrain_source_root "${LY_ROOT_FOLDER}/Gems/Terrain/Code/Source")
set(tc_generated_overrides "${CMAKE_CURRENT_BINARY_DIR}/EngineOverrides")
tc_prepare_engine_overrides("${tc_terrain_source_root}" "${tc_generated_overrides}")

if(NOT TARGET Terrain.Static)
    message(FATAL_ERROR "TerrainCompositor requires the Terrain.Static target")
endif()
set(tc_override_sources
    TerrainRenderer/TerrainMeshManager.cpp
    TerrainRenderer/TerrainFeatureProcessor.cpp
    TerrainRaycast/TerrainRaycastContext.cpp
    Components/TerrainPhysicsColliderComponent.cpp)
get_target_property(tc_terrain_sources Terrain.Static SOURCES)
foreach(relative IN LISTS tc_override_sources)
    set(original "${tc_terrain_source_root}/${relative}")
    set(replacement "${tc_generated_overrides}/${relative}")
    set(found FALSE)
    foreach(source IN LISTS tc_terrain_sources)
        get_filename_component(absolute "${source}" ABSOLUTE BASE_DIR "${LY_ROOT_FOLDER}/Gems/Terrain/Code")
        if(absolute STREQUAL original)
            list(REMOVE_ITEM tc_terrain_sources "${source}")
            set(found TRUE)
        endif()
    endforeach()
    if(NOT found)
        message(FATAL_ERROR "Could not locate ${original} in Terrain.Static")
    endif()
    set_source_files_properties("${original}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON HEADER_FILE_ONLY ON)
    set_source_files_properties("${replacement}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    list(APPEND tc_terrain_sources "${replacement}")
endforeach()
set_property(TARGET Terrain.Static PROPERTY SOURCES "${tc_terrain_sources}")
target_include_directories(Terrain.Static BEFORE PUBLIC
    "${tc_generated_overrides}" "${CMAKE_CURRENT_SOURCE_DIR}/EngineOverrides" "${CMAKE_CURRENT_SOURCE_DIR}/Code/Include")

ly_add_target(
    NAME ${gem_name}.Static STATIC
    NAMESPACE Gem
    FILES_CMAKE terrain_compositor_files.cmake
    INCLUDE_DIRECTORIES
        PUBLIC Code/Include
        PRIVATE "${tc_generated_overrides}" EngineOverrides
        PUBLIC ${LY_ROOT_FOLDER}/Gems/Terrain/Code/Source
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
            PRIVATE Code/Include Code/Source "${tc_generated_overrides}" EngineOverrides ${LY_ROOT_FOLDER}/Gems/Terrain/Code/Source
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
                "${tc_generated_overrides}"
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
    set_source_files_properties(Code/Tests/TerrainNormalGpuTests.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    target_compile_definitions(
        ${gem_name}.Tests PRIVATE
        TERRAIN_COMPOSITOR_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Assets/Shaders/Terrain")
    if(PAL_PLATFORM_NAME STREQUAL "Windows")
        target_link_libraries(${gem_name}.Tests PRIVATE d3d11 d3dcompiler)
    endif()

    ly_add_googletest(NAME Gem::${gem_name}.Tests LABELS REQUIRES_tiaf)
    if(PAL_TRAIT_BUILD_HOST_TOOLS)
        ly_add_target(
            NAME ${gem_name}.Editor.Tests ${PAL_TRAIT_TEST_TARGET_TYPE}
            NAMESPACE Gem
            FILES_CMAKE Code/Tests/terrain_compositor_editor_tests_files.cmake
            INCLUDE_DIRECTORIES PRIVATE Code/Include Code/Source ${LY_ROOT_FOLDER}/Gems/Terrain/Code/Source
            BUILD_DEPENDENCIES PRIVATE AZ::AzTest AZ::AzToolsFramework Gem::${gem_name}.Static
        )
        ly_add_googletest(NAME Gem::${gem_name}.Editor.Tests LABELS REQUIRES_tiaf)
    endif()
    add_test(NAME Gem::${gem_name}.EngineOverrides
        COMMAND "${CMAKE_COMMAND}"
            "-DTC_ENGINE_TERRAIN_ROOT=${tc_terrain_source_root}"
            "-DTC_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/EngineOverrideTests"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Code/Tests/EngineOverridesTests.cmake")
endif()
