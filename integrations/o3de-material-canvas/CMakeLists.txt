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
