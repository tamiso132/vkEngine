//! rm_internal.h
#include "common.h"
#include "resource/resmanager.h"
#include "rm_internal.h"
#include "vector.h"
#include <vulkan/vulkan_core.h>

static VkComponentMapping _vk_component_mapping(void) {
  return (VkComponentMapping){
      .r = VK_COMPONENT_SWIZZLE_R,
      .g = VK_COMPONENT_SWIZZLE_G,
      .b = VK_COMPONENT_SWIZZLE_B,
      .a = VK_COMPONENT_SWIZZLE_A,
  };
}

void rm_bindless_init(M_Resource *rm, M_GPU *gpu) {
  VkDescriptorPoolSize sizes[] = {
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RM_MAX_RESOURCES},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RM_MAX_RESOURCES},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RM_MAX_RESOURCES},
  };

  VkDescriptorPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                   .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
                                   .maxSets = 1,
                                   .poolSizeCount = 3,
                                   .pPoolSizes = sizes};
  vk_check(vkCreateDescriptorPool(gpu->device, &pi, NULL, &rm->descriptor_pool));

  VkDescriptorSetLayoutBinding bindings[] = {
      {RES_B_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RM_MAX_RESOURCES, VK_SHADER_STAGE_ALL, NULL},
      {RES_B_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RM_MAX_RESOURCES, VK_SHADER_STAGE_ALL, NULL},
      {RES_B_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RM_MAX_RESOURCES, VK_SHADER_STAGE_ALL, NULL},
  };

  VkDescriptorBindingFlags f = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
  VkDescriptorBindingFlags bindFlags[] = {f, f, f};

  VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      .bindingCount = 3,
      .pBindingFlags = bindFlags};

  VkDescriptorSetLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                                        .pNext = &flagsInfo,
                                        .bindingCount = 3,
                                        .pBindings = bindings};
  vk_check(vkCreateDescriptorSetLayout(gpu->device, &li, NULL, &rm->bindless_layout));

  VkDescriptorSetAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                    .descriptorPool = rm->descriptor_pool,
                                    .descriptorSetCount = 1,
                                    .pSetLayouts = &rm->bindless_layout};
  vk_check(vkAllocateDescriptorSets(gpu->device, &ai, &rm->bindless_set));

  VkPushConstantRange push = {.stageFlags = SHADER_STAGES, .size = 128};
  VkDescriptorSetLayout layout = rm->bindless_layout;

  VkPipelineLayoutCreateInfo pl = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                   .setLayoutCount = 1,
                                   .pSetLayouts = &layout,
                                   .pushConstantRangeCount = 1,
                                   .pPushConstantRanges = &push};
  vk_check(vkCreatePipelineLayout(gpu->device, &pl, NULL, &rm->pip_layout));

  VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                            .magFilter = VK_FILTER_LINEAR,
                            .minFilter = VK_FILTER_LINEAR,
                            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                            .maxAnisotropy = 1.0f,
                            .maxLod = VK_LOD_CLAMP_NONE};
  vk_check(vkCreateSampler(gpu->device, &si, NULL, &rm->default_sampler));

  (void)_vk_component_mapping; // keep if you later need it here
}

void rm_bindless_shutdown(M_Resource *rm, M_GPU *gpu) {
  if (rm->default_sampler)
    vkDestroySampler(gpu->device, rm->default_sampler, NULL);
  if (rm->pip_layout)
    vkDestroyPipelineLayout(gpu->device, rm->pip_layout, NULL);
  if (rm->bindless_layout)
    vkDestroyDescriptorSetLayout(gpu->device, rm->bindless_layout, NULL);
  if (rm->descriptor_pool)
    vkDestroyDescriptorPool(gpu->device, rm->descriptor_pool, NULL);

  rm->default_sampler = VK_NULL_HANDLE;
  rm->pip_layout = VK_NULL_HANDLE;
  rm->bindless_layout = VK_NULL_HANDLE;
  rm->descriptor_pool = VK_NULL_HANDLE;
  rm->bindless_set = VK_NULL_HANDLE;
}

void rm_bindless_add(M_Resource *rm, ResHandle handle, VkDescriptorImageInfo *imageInfo,
                     VkDescriptorBufferInfo *bufferInfo) {
  void *res = vec_at(&rm->resources[handle.res_type], handle.id);

  if (handle.res_type == RES_TYPE_IMAGE) {
    RImage *image = (RImage *)res;
    image->bindlessIndex = rm->b_counter[image->binding]++;
  } else {
    RBuffer *buffer = (RBuffer *)res;
    buffer->bindlessIndex = rm->b_counter[buffer->binding]++;
  }

  rm_bindless_update(rm, handle, imageInfo, bufferInfo);
}

void rm_bindless_update(M_Resource *rm, ResHandle handle, VkDescriptorImageInfo *imageInfo,
                        VkDescriptorBufferInfo *bufferInfo) {
  M_GPU *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);

  VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstSet = rm->bindless_set,
                                .descriptorCount = 1,
                                .pImageInfo = imageInfo,
                                .pBufferInfo = bufferInfo};

  void *res = vec_at(&rm->resources[handle.res_type], handle.id);

  if (handle.res_type == RES_TYPE_BUFFER) {
    RBuffer *buffer = (RBuffer *)res;
    write.dstBinding = RES_B_STORAGE_BUFFER;
    write.dstArrayElement = buffer->bindlessIndex;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  } else {
    RImage *image = (RImage *)res;
    write.dstBinding = image->binding;
    write.dstArrayElement = image->bindlessIndex;
    write.descriptorType = image->type;
    if (imageInfo) {
      imageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL; // TODO: refine later
      if (image->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
        imageInfo->sampler = rm->default_sampler;
      }
    }
  }

  vkUpdateDescriptorSets(gpu->device, 1, &write, 0, NULL);
}
