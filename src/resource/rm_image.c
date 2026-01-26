//! rm_internal.h
#include "common.h"
#include "resource/resmanager.h"
#include "rm_internal.h"
#include <vulkan/vulkan_core.h>

// --- Private Prototypes ---
static void _create_view(M_GPU *gpu, RImage *image);

static VkComponentMapping _vk_component_mapping(void);

void rm_reset_image_sync(RImage *image) {
  image->sync = (SyncDef){
      .layout = VK_IMAGE_LAYOUT_UNDEFINED, .access = VK_ACCESS_2_NONE, .stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
}

void rm_create_image_full(M_Resource *rm, M_GPU *gpu, RImage *image) {
  (void)rm;
  rm_reset_image_sync(image);

  VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                          .imageType = VK_IMAGE_TYPE_2D,
                          .extent = {.width = image->extent.width, .height = image->extent.height, .depth = 1},
                          .mipLevels = 1,
                          .arrayLayers = 1,
                          .format = image->format,
                          .tiling = VK_IMAGE_TILING_OPTIMAL,
                          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                          .usage = image->usage,
                          .samples = VK_SAMPLE_COUNT_1_BIT};

  VmaAllocationCreateInfo ai = {0};
  ai.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

  vk_check(vmaCreateImage(gpu->allocator, &ci, &ai, &image->handle, &image->alloc, NULL));

  _create_view(gpu, image);
}

ResHandle rm_image_create(M_Resource *rm, M_GPU *gpu, RGImageInfo info) {
  RImage image = {0};
  rm_reset_image_sync(&image);

  assert(info.name);
  image.name = strdup(info.name);

  VkImageUsageFlags usage = info.usage;

  image.extent = (VkExtent2D){.width = info.width, .height = info.height};
  image.usage = info.usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  image.format = info.format;
  image.is_imported = false;

  // Decide binding/type based on usage
  bool is_sampled = (usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0;
  if (is_sampled) {
    image.binding = RES_B_SAMPLED_IMAGE;
    image.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  } else {
    image.binding = RES_B_STORAGE_IMAGE;
    image.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  }

  rm_create_image_full(rm, gpu, &image);

  u32 id = (u32)vec_len(&rm->resources[RES_TYPE_IMAGE]);
  vec_push(&rm->resources[RES_TYPE_IMAGE], &image);
  ResHandle h = {.id = id, .res_type = RES_TYPE_IMAGE};

  VkDescriptorImageInfo imageInfo = {
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
      .imageView = image.view,
      .sampler = (image.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ? rm->default_sampler : VK_NULL_HANDLE};

  rm_bindless_add(rm, h, &imageInfo, NULL);
  return h;
}

ResHandle rm_image_import(M_Resource *rm, RGImageInfo *info, VkImage img, VkImageView view) {
  RImage image = {0};
  M_GPU *dev = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  rm_reset_image_sync(&image);

  image.name = strdup(info->name);
  image.view = view;
  image.handle = img;
  image.extent = (VkExtent2D){.width = info->width, .height = info->height};
  image.usage = info->usage;
  image.format = info->format;
  image.is_imported = true;

  if (view == NULL) {
    _create_view(dev, &image);
  }

  // Decide binding/type based on usage
  bool is_sampled = (info->usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0;
  if (is_sampled) {
    image.binding = RES_B_SAMPLED_IMAGE;
    image.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  } else {
    image.binding = RES_B_STORAGE_IMAGE;
    image.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  }

  u32 id = (u32)vec_len(&rm->resources[RES_TYPE_IMAGE]);
  vec_push(&rm->resources[RES_TYPE_IMAGE], &image);
  ResHandle h = {.id = id, .res_type = RES_TYPE_IMAGE};

  // Update bindless for imported too (so shaders can use it)
  //   VkDescriptorImageInfo imageInfo = {
  //       .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
  //       .imageView = image.view,
  //       .sampler = (image.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ? rm->default_sampler : VK_NULL_HANDLE};
  //   rm_bindless_add(rm, h, &imageInfo, NULL);

  return h;
}

void rm_image_destroy_owned(M_Resource *rm, M_GPU *gpu, RImage *img) {
  (void)rm;
  if (!img)
    return;
  if (img->is_imported)
    return;

  if (img->view)
    vkDestroyImageView(gpu->device, img->view, NULL);
  img->view = VK_NULL_HANDLE;

  if (img->handle)
    vmaDestroyImage(gpu->allocator, img->handle, img->alloc);
  img->handle = VK_NULL_HANDLE;
  img->alloc = NULL;
}
// --- Private Functions ---

static void _create_view(M_GPU *gpu, RImage *image) {
  VkImageViewCreateInfo viewInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image->handle,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .components = _vk_component_mapping(),
      .format = image->format,
      .subresourceRange =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
  };
  vk_check(vkCreateImageView(gpu->device, &viewInfo, NULL, &image->view));
}

static VkComponentMapping _vk_component_mapping(void) {
  return (VkComponentMapping){
      .r = VK_COMPONENT_SWIZZLE_R,
      .g = VK_COMPONENT_SWIZZLE_G,
      .b = VK_COMPONENT_SWIZZLE_B,
      .a = VK_COMPONENT_SWIZZLE_A,
  };
}
