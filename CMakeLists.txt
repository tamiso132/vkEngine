cmake_minimum_required(VERSION 3.21)

# --- Toolchain (Must be before project) ---
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

project(MiniDaxa C CXX)

# --- Standards ---
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies ---
include(FetchContent)
set(FETCHCONTENT_BASE_DIR ${CMAKE_SOURCE_DIR}/external)

# 1. GLFW
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.3.8)
# 2. Volk
FetchContent_Declare(volk GIT_REPOSITORY https://github.com/zeux/volk.git GIT_TAG master)
# 3. VMA
FetchContent_Declare(vma GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git GIT_TAG master)
# 4. cglm
FetchContent_Declare(cglm GIT_REPOSITORY https://github.com/recp/cglm.git GIT_TAG master)

FetchContent_MakeAvailable(glfw volk vma cglm)

add_subdirectory(external)




# --- 2. C Executable (Main App) ---
# Compiles your code with C23
add_executable(${PROJECT_NAME} 
    src/main.c 
    src/gpu/gpu.c
    src/rendergraph.c 
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    proxy_vk_mem  # Links your C++ library
    glfw 
    volk 
    cglm
)



