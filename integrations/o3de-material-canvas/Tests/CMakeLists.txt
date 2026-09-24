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
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME TerrainCanvasMigration COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/TestMigration.py")
endif()
