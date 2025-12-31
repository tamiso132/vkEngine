#include "pipeline.h"
#include "common.h"
#include "resmanager.h"
#include "util.h"
#include "vector.h"

#include <string.h>

#include <glslang/Include/glslang_c_interface.h>

typedef struct M_Pipeline {

  VECTOR_TYPES(GPUPipeline)
  Vector pipelines;

  ResourceManager *res;
} M_Pipeline;

// --- Private Prototypes ---

static VkPipeline _build_internal(M_Pipeline *pm, GpBuilder *b);

M_Pipeline *pm_init(ResourceManager *rm) {
  M_Pipeline *pm = calloc(sizeof(M_Pipeline), 1);
  vec_init(&pm->pipelines, sizeof(GPUPipeline), NULL);
  pm->res = rm;

  return pm;
}

GPUPipeline *pm_get_pipeline(M_Pipeline *pm, PipelineHandle handle) {
  return VEC_AT(&pm->pipelines, handle, GPUPipeline);
}

ResourceManager *pm_get_rm(M_Pipeline *pm) { return pm->res; }

GpBuilder gp_init(ResourceManager *rm, const char *name) {
  GpBuilder b = {0};
  b.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  b.cull_mode = VK_CULL_MODE_NONE;
  b.front_face = VK_FRONT_FACE_CLOCKWISE;
  b.depth_op = VK_COMPARE_OP_LESS_OR_EQUAL;
  b.push_const_size = 128;
  b.name = name;
  return b;
}

GPUDevice *pm_get_gpu(M_Pipeline *pm) { return rm_get_gpu(pm->res); }

void gp_set_shaders(GpBuilder *b, VkShaderModule vs, VkShaderModule fs) {
  b->vs = vs;
  b->fs = fs;
}

void gp_set_topology(GpBuilder *b, VkPrimitiveTopology topo) { b->topology = topo; }
void gp_set_cull(GpBuilder *b, VkCullModeFlags mode, VkFrontFace front) {
  b->cull_mode = mode;
  b->front_face = front;
}

void gp_set_color_formats(GpBuilder *b, const VkFormat *formats, uint32_t count) {
  if (count > 4)
    count = 4;
  b->color_count = count;
  memcpy(b->color_formats, formats, count * sizeof(VkFormat));
}

void gp_set_depth_format(GpBuilder *b, VkFormat format) { b->depth_format = format; }

void gp_enable_depth(GpBuilder *b, bool write, VkCompareOp op) {
  b->depth_test = true;
  b->depth_write = write;
  b->depth_op = op;
}

void gp_enable_blend(GpBuilder *b) { b->blend_enable = true; }

void gp_set_layout(GpBuilder *b, VkDescriptorSetLayout bindless, uint32_t push_size) {}

PipelineHandle gp_build(M_Pipeline *pm, GpBuilder *b) {
  VkPipeline pipeline = _build_internal(pm, b);
  GPUPipeline p = {.vk_handle = pipeline, .config = *b};
  return vec_push(&pm->pipelines, &p);
}

void gp_rebuild(M_Pipeline *pm, GpBuilder *b, PipelineHandle handle) {
  GPUPipeline *p = VEC_AT(&pm->pipelines, handle, GPUPipeline);

  VkPipeline pipeline = _build_internal(pm, b);
  vkDestroyPipeline(rm_get_gpu(pm->res)->device, p->vk_handle, NULL);

  p->vk_handle = pipeline;
  p->config = *b;
}

void gp_destroy(VkDevice device, GPUPipeline *p) {
  vkDestroyPipeline(device, p->vk_handle, NULL);
  *p = (GPUPipeline){0};
}

// --- Private Functions ---

static VkPipeline _build_internal(M_Pipeline *pm, GpBuilder *b) {
  VkDevice device = rm_get_gpu(pm->res)->device;

  VkPipelineShaderStageCreateInfo stages[2];
  uint32_t stage_count = 0;

  stages[stage_count++] =
      (VkPipelineShaderStageCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                                        .module = b->vs,
                                        .pName = "main"};

  stages[stage_count++] =
      (VkPipelineShaderStageCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                        .module = b->fs,
                                        .pName = "main"};

  // 2. Vertex Input (Empty for Bindless/Pull)
  VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

  // 3. Input Assembly
  VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                               .topology = b->topology};

  // 4. Viewport (Dynamic)
  VkPipelineViewportStateCreateInfo vp = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};

  // 5. Rasterization
  VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                               .polygonMode = VK_POLYGON_MODE_FILL,
                                               .lineWidth = 1.0f,
                                               .cullMode = b->cull_mode,
                                               .frontFace = b->front_face};

  // 6. Multisample
  VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                             .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};

  // 7. Depth Stencil
  VkPipelineDepthStencilStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                                              .depthTestEnable = b->depth_test,
                                              .depthWriteEnable = b->depth_write,
                                              .depthCompareOp = b->depth_op};

  // 8. Color Blend
  VkPipelineColorBlendAttachmentState att[4];
  for (uint32_t i = 0; i < b->color_count; i++) {
    att[i] = (VkPipelineColorBlendAttachmentState){.colorWriteMask = 0xF, .blendEnable = b->blend_enable};
    if (b->blend_enable) {
      att[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      att[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      att[i].colorBlendOp = VK_BLEND_OP_ADD;
      att[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      att[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
      att[i].alphaBlendOp = VK_BLEND_OP_ADD;
    }
  }

  VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                            .attachmentCount = b->color_count,
                                            .pAttachments = att};

  // 9. Dynamic State
  VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                          .dynamicStateCount = 2,
                                          .pDynamicStates = dyn_states};

  // 11. Dynamic Rendering Info
  VkPipelineRenderingCreateInfo dynamic_rendering = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                                     .colorAttachmentCount = b->color_count,
                                                     .pColorAttachmentFormats = b->color_formats,
                                                     .depthAttachmentFormat = b->depth_format};

  // 12. Pipeline Creation
  VkGraphicsPipelineCreateInfo createInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                             .pNext = &dynamic_rendering,
                                             .stageCount = stage_count,
                                             .pStages = stages,
                                             .pVertexInputState = &vi,
                                             .pInputAssemblyState = &ia,
                                             .pViewportState = &vp,
                                             .pRasterizationState = &rs,
                                             .pMultisampleState = &ms,
                                             .pDepthStencilState = &ds,
                                             .pColorBlendState = &cb,
                                             .pDynamicState = &dyn,
                                             .layout = rm_get_pipeline_layout(pm->res)};

  VkPipeline pipeline = {};
  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, NULL, &pipeline) != VK_SUCCESS) {
    LOG_ERROR("[Pipeline] Error: Failed to create graphics pipeline.\n");
  }

  // Cleanup Shaders
  vkDestroyShaderModule(device, b->vs, NULL);
  vkDestroyShaderModule(device, b->fs, NULL);

  LOG_INFO("[Pipeline Created]:  %s", b->name);
  return pipeline;
}
