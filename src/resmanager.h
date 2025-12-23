#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include "gpu/gpu.h"
#include <stdbool.h>

// --- Types ---

// Opaque Handle
typedef struct RGHandle
{
    uint32_t id;
} RGHandle;

typedef enum RGResourceType
{
    RES_TYPE_BUFFER,
    RES_TYPE_IMAGE
} RGResourceType;

// Internal Resource Representation
typedef struct RGResource
{
    char name[64];
    RGResourceType type;
    bool is_imported; // True = Do not destroy GPU resource on cleanup

    union
    {
        GPUBuffer buf;
        struct
        {
            GPUImage img;
            // We only need to store the current layout to bridge frames.
            // (e.g., if a previous frame left the image in PRESENT_SRC, we need to know that).
            VkImageLayout current_layout;
        } img;
    };
} RGResource;

typedef struct
{
    GPUDevice *gpu;
    RGResource *resources; // Stretchy Buffer (Vector)

    // --- Bindless Context ---
    VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout bindless_layout;
    VkDescriptorSet bindless_set; // Global Set 0
    VkSampler default_sampler;
} ResourceManager;

// --- API ---

// Init & Destroy
void rm_init(ResourceManager *rm, GPUDevice *gpu);
void rm_destroy(ResourceManager *rm);

// Creators (Creates resource + Adds to Bindless Heap)
RGHandle rm_create_buffer(ResourceManager *rm, const char *name, uint64_t size, VkBufferUsageFlags usage);
RGHandle rm_create_image(ResourceManager *rm, const char *name, uint32_t w, uint32_t h, VkFormat fmt);

// Import (For swapchain images or external textures)
RGHandle rm_import_image(ResourceManager *rm, const char *name, VkImage img, VkImageView view, VkImageLayout cur_layout);

// Getters (Raw Vulkan handles)
VkBuffer rm_get_buffer(ResourceManager *rm, RGHandle handle);
VkImage rm_get_image(ResourceManager *rm, RGHandle handle);
VkImageView rm_get_image_view(ResourceManager *rm, RGHandle handle);

// Bindless Getters (For Pipeline Creation / Rendering)
VkDescriptorSetLayout rm_get_bindless_layout(ResourceManager *rm);
VkDescriptorSet rm_get_bindless_set(ResourceManager *rm);

#endif // RESOURCE_MANAGER_H