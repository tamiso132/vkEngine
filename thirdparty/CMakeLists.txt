# Compile vk_mem with cpp
add_library(proxy_vk_mem 
    ${vma_SOURCE_DIR}/src/VmaUsage.cpp
    ${vma_SOURCE_DIR}/include/vk_mem_alloc.h
)


target_compile_features(proxy_vk_mem PRIVATE cxx_std_17)
target_compile_options(proxy_vk_mem PRIVATE -w -g -fdiagnostics-color=always)


set_target_properties(proxy_vk_mem PROPERTIES LINKER_LANGUAGE CXX)
target_compile_definitions(proxy_vk_mem PUBLIC VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_VULKAN_VERSION=1003000)

target_link_libraries(proxy_vk_mem PUBLIC ${Vulkan_LIBRARY})

target_include_directories(proxy_vk_mem PUBLIC ${Vulkan_INCLUDE_DIRS}   ${vma_SOURCE_DIR}/include)


target_sources(proxy_vk_mem PUBLIC ${vma_SOURCE_DIR}/include)

# Add thirdparty source
add_sources(hashmap.c/hashmap.c)
