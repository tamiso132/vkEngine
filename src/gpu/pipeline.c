#include "pipeline.h"
#include "filewatch.h"
#include "util.h"
#include "vector.h"

#include <string.h>

#include <glslang/Include/glslang_c_interface.h>

typedef struct Includes {
  const char **abs_paths;
  int count;
} Includes;

typedef struct {
  VkDevice device;
  GPUPipeline *target_pipeline;
  GpBuilder config;
  Includes ctx_include;
} PipReloadCtx;

// --- Private Prototypes ---
static VkShaderModule _create_shader_module(VkDevice device,
                                            const char *spirvPath);

static GPUPipeline _build_internal(VkDevice device, GpBuilder *b);

GpBuilder gp_init() {
  GpBuilder b = {0};
  b.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  b.cull_mode = VK_CULL_MODE_NONE;
  b.front_face = VK_FRONT_FACE_CLOCKWISE;
  b.depth_op = VK_COMPARE_OP_LESS_OR_EQUAL;
  return b;
}

void gp_set_shaders(GpBuilder *b, const char *vs, const char *fs) {
  b->vs_path = vs;
  b->fs_path = fs;
}

void gp_set_topology(GpBuilder *b, VkPrimitiveTopology topo) {
  b->topology = topo;
}
void gp_set_cull(GpBuilder *b, VkCullModeFlags mode, VkFrontFace front) {
  b->cull_mode = mode;
  b->front_face = front;
}

void gp_set_color_formats(GpBuilder *b, const VkFormat *formats,
                          uint32_t count) {
  if (count > 4)
    count = 4;
  b->color_count = count;
  memcpy(b->color_formats, formats, count * sizeof(VkFormat));
}

void gp_set_depth_format(GpBuilder *b, VkFormat format) {
  b->depth_format = format;
}

void gp_enable_depth(GpBuilder *b, bool write, VkCompareOp op) {
  b->depth_test = true;
  b->depth_write = write;
  b->depth_op = op;
}

void gp_enable_blend(GpBuilder *b) { b->blend_enable = true; }

void gp_set_layout(GpBuilder *b, VkDescriptorSetLayout bindless,
                   uint32_t push_size) {
  b->bindless_layout = bindless;
  b->push_const_size = push_size;
}

GPUPipeline gp_build(VkDevice device, GpBuilder *b) {
  return _build_internal(device, b);
}

void gp_destroy(VkDevice device, GPUPipeline *p) {
  vkDestroyPipeline(device, p->pipeline, NULL);
  vkDestroyPipelineLayout(device, p->layout, NULL);
  *p = (GPUPipeline){0};
}

// --- Private Functions ---

static VkShaderModule _create_shader_module(VkDevice device,
                                            const char *spirvPath) {
  Vector source_vec = file_read_binary(spirvPath);
  VkShaderModuleCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = vec_len(&source_vec),
      .pCode = source_vec.data,
  };
  vec_free(&source_vec);
  VkShaderModule module = {};
  vk_check(vkCreateShaderModule(device, &info, NULL, &module));

  return module;
}

// --- Internal: Core Build Logic ---
static GPUPipeline _build_internal(VkDevice device, GpBuilder *b) {
  GPUPipeline p = {0};

  // 1. Compile Shaders
  VkShaderModule fs = _create_shader_module(device, b->fs_path);
  VkShaderModule vs = _create_shader_module(device, b->vs_path);

  VkPipelineShaderStageCreateInfo stages[2];
  uint32_t stage_count = 0;

  stages[stage_count++] = (VkPipelineShaderStageCreateInfo){
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vs,
      .pName = "main"};

  stages[stage_count++] = (VkPipelineShaderStageCreateInfo){
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = fs,
      .pName = "main"};

  // 2. Vertex Input (Empty for Bindless/Pull)
  VkPipelineVertexInputStateCreateInfo vi = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

  // 3. Input Assembly
  VkPipelineInputAssemblyStateCreateInfo ia = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = b->topology};

  // 4. Viewport (Dynamic)
  VkPipelineViewportStateCreateInfo vp = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1};

  // 5. Rasterization
  VkPipelineRasterizationStateCreateInfo rs = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .lineWidth = 1.0f,
      .cullMode = b->cull_mode,
      .frontFace = b->front_face};

  // 6. Multisample
  VkPipelineMultisampleStateCreateInfo ms = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};

  // 7. Depth Stencil
  VkPipelineDepthStencilStateCreateInfo ds = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = b->depth_test,
      .depthWriteEnable = b->depth_write,
      .depthCompareOp = b->depth_op};

  // 8. Color Blend
  VkPipelineColorBlendAttachmentState att[4];
  for (uint32_t i = 0; i < b->color_count; i++) {
    att[i] = (VkPipelineColorBlendAttachmentState){
        .colorWriteMask = 0xF, .blendEnable = b->blend_enable};
    if (b->blend_enable) {
      att[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      att[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      att[i].colorBlendOp = VK_BLEND_OP_ADD;
      att[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      att[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
      att[i].alphaBlendOp = VK_BLEND_OP_ADD;
    }
  }

  VkPipelineColorBlendStateCreateInfo cb = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = b->color_count,
      .pAttachments = att};

  // 9. Dynamic State
  VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                 VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = 2,
      .pDynamicStates = dyn_states};

  // 10. Layout
  VkPushConstantRange push = {.stageFlags = VK_SHADER_STAGE_ALL,
                              .size = b->push_const_size};
  VkPipelineLayoutCreateInfo pl = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = b->bindless_layout ? 1 : 0,
      .pSetLayouts = b->bindless_layout ? &b->bindless_layout : NULL,
      .pushConstantRangeCount = b->push_const_size ? 1 : 0,
      .pPushConstantRanges = b->push_const_size ? &push : NULL};

  vk_check(vkCreatePipelineLayout(device, &pl, NULL, &p.layout));

  // 11. Dynamic Rendering Info
  VkPipelineRenderingCreateInfo dynamic_rendering = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = b->color_count,
      .pColorAttachmentFormats = b->color_formats,
      .depthAttachmentFormat = b->depth_format};

  // 12. Pipeline Creation
  VkGraphicsPipelineCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
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
      .layout = p.layout};

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, NULL,
                                &p.pipeline) != VK_SUCCESS) {
    LOG_ERROR("[Pipeline] Error: Failed to create graphics pipeline.\n");
  }

  // Cleanup Shaders
  vkDestroyShaderModule(device, vs, NULL);
  vkDestroyShaderModule(device, fs, NULL);

  return p;
}
