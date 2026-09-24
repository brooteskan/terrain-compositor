cmake_minimum_required(VERSION 3.22)
project(TerrainCanvasParity LANGUAGES CXX)
if(NOT WIN32)
    message(FATAL_ERROR "This GPU parity check uses D3D11 on Windows")
endif()
enable_testing()
add_executable(TerrainCanvasParity TerrainTintParity.cpp)
target_compile_features(TerrainCanvasParity PRIVATE cxx_std_17)
target_compile_definitions(TerrainCanvasParity PRIVATE NOMINMAX
    CANVAS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    TERRAIN_SHADERS="${CMAKE_CURRENT_SOURCE_DIR}/../../o3de/Assets/Shaders/Terrain")
target_link_libraries(TerrainCanvasParity PRIVATE d3d11 d3dcompiler)
add_test(NAME TerrainCanvasParity COMMAND TerrainCanvasParity)

add_executable(TerrainSurfaceTests TerrainSurfaceTests.cpp)
target_compile_features(TerrainSurfaceTests PRIVATE cxx_std_17)
target_compile_definitions(TerrainSurfaceTests PRIVATE NOMINMAX
    CANVAS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    TERRAIN_SHADERS="${CMAKE_CURRENT_SOURCE_DIR}/../../o3de/Assets/Shaders/Terrain")
target_link_libraries(TerrainSurfaceTests PRIVATE d3d11 d3dcompiler)
add_test(NAME TerrainSurfaceTests COMMAND TerrainSurfaceTests)
set(TC_CANVAS_VALIDATION_ROOT "" CACHE PATH "Fresh native output root containing legacy and migrated fixtures")
if(TC_CANVAS_VALIDATION_ROOT)
    add_executable(TerrainGraphMigrationParity TerrainGraphMigrationParity.cpp)
    target_compile_features(TerrainGraphMigrationParity PRIVATE cxx_std_17)
    target_compile_definitions(TerrainGraphMigrationParity PRIVATE NOMINMAX
        CANVAS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
        TERRAIN_SHADERS="${CMAKE_CURRENT_SOURCE_DIR}/../../o3de/Assets/Shaders/Terrain")
    target_link_libraries(TerrainGraphMigrationParity PRIVATE d3d11 d3dcompiler)
    add_test(NAME TerrainGraphMigrationParity COMMAND TerrainGraphMigrationParity "${TC_CANVAS_VALIDATION_ROOT}")
endif()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME TerrainCanvasMigration COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/TestMigration.py")
    add_test(NAME TerrainCanvasReusableMigration COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/TestReusableMigration.py")
    add_test(NAME TerrainCanvasNodeLibrary COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/TestNodeLibrary.py")
    add_test(NAME TerrainCanvasCompilerContract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/TestCompilerContract.py")
    set(TC_CANVAS_ENGINE_ROOT "" CACHE PATH "Pinned O3DE checkout for compiler patch compatibility checks")
    if(TC_CANVAS_ENGINE_ROOT)
        add_test(NAME TerrainCanvasCompilerOverride COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/TestCompilerOverride.py" "${TC_CANVAS_ENGINE_ROOT}" "${CMAKE_COMMAND}")
    endif()
endif()
