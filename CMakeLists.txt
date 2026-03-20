cmake_minimum_required(VERSION 3.18)
project(MonteCarloPathTracer LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_ARCHITECTURES 75 80 86)

find_package(OpenMP REQUIRED)
find_package(CUDAToolkit REQUIRED)

# GLM (header-only)
find_path(GLM_INCLUDE_DIR glm/glm.hpp HINTS /usr/include /usr/local/include)
if(NOT GLM_INCLUDE_DIR)
    message(STATUS "GLM not found, downloading via FetchContent...")
    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        0.9.9.8
    )
    FetchContent_MakeAvailable(glm)
    set(GLM_INCLUDE_DIR ${glm_SOURCE_DIR})
endif()

set(SOURCES
    src/main.cpp
    src/core/scene.cpp
    src/camera/camera.cpp
    src/renderer/cpu_renderer.cpp
    src/renderer/gpu_renderer.cu
    src/renderer/path_tracer.cu
    src/renderer/stream_compaction.cu
    src/geometry/sah_bvh.cpp
    src/geometry/lbvh.cu
    src/utils/image.cpp
    src/utils/timer.cpp
)

add_executable(pathtracer ${SOURCES})

target_include_directories(pathtracer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GLM_INCLUDE_DIR}
)

target_link_libraries(pathtracer PRIVATE
    OpenMP::OpenMP_CXX
    CUDA::cudart
)

set_target_properties(pathtracer PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_RESOLVE_DEVICE_SYMBOLS ON
)