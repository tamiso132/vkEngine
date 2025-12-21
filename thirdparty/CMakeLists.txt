# Set compiler flags for the vk_mem target
add_library(proxy_vk_mem 
    ${vma_SOURCE_DIR}/src/VmaUsage.cpp
    ${vma_SOURCE_DIR}/include/vk_mem_alloc.h
)

# Set the C++ standard and ensure any specific options
target_compile_features(proxy_vk_mem PRIVATE cxx_std_17)
target_compile_options(proxy_vk_mem PRIVATE -w -g -fdiagnostics-color=always)

# Set the C++ language for the vk_mem library
set_target_properties(proxy_vk_mem PROPERTIES LINKER_LANGUAGE CXX)

target_compile_definitions(proxy_vk_mem PUBLIC VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_VULKAN_VERSION=1003000)
# Locate Vulkan package
find_package(Vulkan REQUIRED)

# Link Vulkan library
target_link_libraries(proxy_vk_mem PUBLIC ${Vulkan_LIBRARY})

# Include Vulkan headers
target_include_directories(proxy_vk_mem PUBLIC ${Vulkan_INCLUDE_DIRS}   ${vma_SOURCE_DIR}/include)

# Set the library to be header-only by including its header in the interface
target_sources(proxy_vk_mem PUBLIC ${vma_SOURCE_DIR}/include)