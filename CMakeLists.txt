cmake_minimum_required(VERSION 3.21)

# --- Toolchain (Must be before project) ---
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

project(MiniDaxa C CXX)

find_package(glslang CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(Vulkan REQUIRED)

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

include(cmake/engine.cmake)


add_subdirectory(thirdparty)
add_subdirectory(src)




get_property(SOURCES_FILES_GAME_ENGINE GLOBAL PROPERTY GLOBAL_SOURCES)
get_property(INCLUDE_DIRECTORIES_ENGINE GLOBAL PROPERTY GLOBAL_INCLUDES)

add_executable(${PROJECT_NAME} 
    src/main.c 
  ${SOURCES_FILES_GAME_ENGINE}
)

add_compile_definitions(PROJECT_ROOT="${CMAKE_SOURCE_DIR}/")

target_include_directories(${PROJECT_NAME} PRIVATE src shaders ${INCLUDE_DIRECTORIES_ENGINE} ${CMAKE_CURRENT_SOURCE_DIR} thirdparty/hashmap.c)



target_link_libraries(${PROJECT_NAME} PRIVATE 
    proxy_vk_mem  # Links your C++ library
    glfw 
    volk 
    cglm
    glslang::glslang 
    glslang::SPIRV 
    glslang::glslang-default-resource-limits

)

if(UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread m)
endif()



