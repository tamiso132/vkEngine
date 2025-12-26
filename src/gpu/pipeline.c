#include "pipeline.h"
#include "filewatch.h" 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glslang/Include/glslang_c_interface.h>

// --- Globals ---
FileWatcher *g_filewatcher = NULL;

// --- Internal Helper Types ---
typedef struct {
  VkDevice device;
  GPUPipeline *target_pipeline;
  GpBuilder builder_state; 
} ReloadContext;


static glslang_resource_t InitResources()
{
  const glslang_resource_t default_resource = {
    .max_lights = 32,
    .max_clip_planes = 6,
    .max_texture_units = 32,
    .max_texture_coords = 32,
    .max_vertex_attribs = 64,
    .max_vertex_uniform_components = 4096,
    .max_varying_floats = 64,
    .max_vertex_texture_image_units = 32,
    .max_combined_texture_image_units = 80,
    .max_texture_image_units = 32,
    .max_fragment_uniform_components = 4096,
    .max_draw_buffers = 32,
    .max_vertex_uniform_vectors = 128,
    .max_varying_vectors = 8,
    .max_fragment_uniform_vectors = 16,
    .max_vertex_output_vectors = 16,
    .max_fragment_input_vectors = 15,
    .min_program_texel_offset = -8,
    .max_program_texel_offset = 7,
    .max_clip_distances = 8,
    .max_compute_work_group_count_x = 65535,
    .max_compute_work_group_count_y = 65535,
    .max_compute_work_group_count_z = 65535,
    .max_compute_work_group_size_x = 1024,
    .max_compute_work_group_size_y = 1024,
    .max_compute_work_group_size_z = 64,
    .max_compute_uniform_components = 1024,
    .max_compute_texture_image_units = 16,
    .max_compute_image_uniforms = 8,
    .max_compute_atomic_counters = 8,
    .max_compute_atomic_counter_buffers = 1,
    .max_varying_components = 60,
    .max_vertex_output_components = 64,
    .max_geometry_input_components = 64,
    .max_geometry_output_components = 128,
    .max_fragment_input_components = 128,
    .max_image_units = 8,
    .max_combined_image_units_and_fragment_outputs = 8,
    .max_combined_shader_output_resources = 8,
    .max_image_samples = 0,
    .max_vertex_image_uniforms = 0,
    .max_tess_control_image_uniforms = 0,
    .max_tess_evaluation_image_uniforms = 0,
    .max_geometry_image_uniforms = 0,
    .max_fragment_image_uniforms = 8,
    .max_combined_image_uniforms = 8,
    .max_geometry_texture_image_units = 16,
    .max_geometry_output_vertices = 256,
    .max_geometry_total_output_components = 1024,
    .max_geometry_uniform_components = 1024,
    .max_geometry_varying_components = 64,
    .max_tess_control_input_components = 128,
    .max_tess_control_output_components = 128,
    .max_tess_control_texture_image_units = 16,
    .max_tess_control_uniform_components = 1024,
    .max_tess_control_total_output_components = 4096,
    .max_tess_evaluation_input_components = 128,
    .max_tess_evaluation_output_components = 128,
    .max_tess_evaluation_texture_image_units = 16,
    .max_tess_evaluation_uniform_components = 1024,
    .max_tess_patch_components = 120,
    .max_patch_vertices = 32,
    .max_tess_gen_level = 64,
    .max_viewports = 16,
    .max_vertex_atomic_counters = 0,
    .max_tess_control_atomic_counters = 0,
    .max_tess_evaluation_atomic_counters = 0,
    .max_geometry_atomic_counters = 0,
    .max_fragment_atomic_counters = 8,
    .max_combined_atomic_counters = 8,
    .max_atomic_counter_bindings = 1,
    .max_vertex_atomic_counter_buffers = 0,
    .max_tess_control_atomic_counter_buffers = 0,
    .max_tess_evaluation_atomic_counter_buffers = 0,
    .max_geometry_atomic_counter_buffers = 0,
    .max_fragment_atomic_counter_buffers = 1,
    .max_combined_atomic_counter_buffers = 1,
    .max_atomic_counter_buffer_size = 16384,
    .max_transform_feedback_buffers = 4,
    .max_transform_feedback_interleaved_components = 64,
    .max_cull_distances = 8,
    .max_combined_clip_and_cull_distances = 8,
    .max_samples = 4,
    .max_mesh_output_vertices_nv = 256,
    .max_mesh_output_primitives_nv = 512,
    .max_mesh_work_group_size_x_nv = 32,
    .max_mesh_work_group_size_y_nv = 1,
    .max_mesh_work_group_size_z_nv = 1,
    .max_task_work_group_size_x_nv = 32,
    .max_task_work_group_size_y_nv = 1,
    .max_task_work_group_size_z_nv = 1,
    .max_mesh_view_count_nv = 4,
    
    // Limits
    .limits = {
        .non_inductive_for_loops = 1,
        .while_loops = 1,
        .do_while_loops = 1,
        .general_uniform_indexing = 1,
        .general_attribute_matrix_vector_indexing = 1,
        .general_varying_indexing = 1,
        .general_sampler_indexing = 1,
        .general_variable_indexing = 1,
        .general_constant_matrix_vector_indexing = 1
    }
};
return default_resource;
}

// --- Internal: File IO ---
static char* _read_file(const char* path, int *length) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
   
    uint32_t* buffer = malloc(len + 1);
    fread(buffer, 1, len, f);
    buffer[len] = 0;
    *length = len;

    fclose(f);
    return (char*)buffer;
}

bool write_file_binary(const char* path, const void* data, size_t size) {
    if (!path || !data) return false;

    FILE* f = fopen(path, "wb"); // 'wb' is critical for binary data
    if (!f) {
        printf("Error: Failed to open file for writing: %s\n", path);
        return false;
    }

    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    if (written != size) {
        printf("Error: Failed to write all bytes to %s (wrote %zu of %zu)\n", path, written, size);
        return false;
    }

    return true;
}

// --- Helper: Compile with Glslang ---
static VkShaderModule _compile_glsl_glslang(VkDevice dev, const char* path, glslang_stage_t stage) {
    // 1. Read File
    int code_len = 0;
    char* code = _read_file(path, &code_len);
    printf("%s\n", code);

 

    const glslang_resource_t default_resource = InitResources();

    const glslang_input_t input = {
        .language = GLSLANG_SOURCE_GLSL,
        .stage = stage,
        .client = GLSLANG_CLIENT_VULKAN,
        .client_version = GLSLANG_TARGET_VULKAN_1_0,      // Target Vulkan 1.0
        .target_language_version = GLSLANG_TARGET_SPV_1_0, // Target SPIR-V 1.0
        .target_language = GLSLANG_TARGET_SPV,
        .code = code, // <--- CORRECTED: Use loaded file code
        .default_version = 450,
        .default_profile = GLSLANG_NO_PROFILE,
        .force_default_version_and_profile = 0,
        .forward_compatible = 0,
        .messages = GLSLANG_MSG_DEFAULT_BIT,
        .resource = &default_resource,
    };

    glslang_shader_t* shader = glslang_shader_create(&input);
    if (!glslang_shader_preprocess(shader, &input) || !glslang_shader_parse(shader, &input)) {
        printf("[Glslang] Parse/Preprocess Error %s:\n%s\n", path, glslang_shader_get_info_log(shader));
        glslang_shader_delete(shader);
        free(code);
        return VK_NULL_HANDLE;
    }

    glslang_program_t* program = glslang_program_create();
    glslang_program_add_shader(program, shader);

    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        printf("[Glslang] Link Error %s:\n%s\n", path, glslang_program_get_info_log(program));
        glslang_program_delete(program);
        glslang_shader_delete(shader);
        free(code);
        return VK_NULL_HANDLE;
    }

    glslang_program_SPIRV_generate(program, stage);

    if ( glslang_program_SPIRV_get_messages(program) )
    {
        printf("%s", glslang_program_SPIRV_get_messages(program));
    }

    // 5. Get SPIR-V Data
    size_t word_count = glslang_program_SPIRV_get_size(program);
    unsigned int* words = glslang_program_SPIRV_get_ptr(program);

    // <--- CRITICAL FIX: Check if generation actually produced data before passing to Vulkan
    if (!words || word_count == 0) {
        printf("[Glslang] Error: SPIR-V generation produced empty code for %s. Messages:\n%s\n", 
            path, glslang_program_SPIRV_get_messages(program));
        glslang_program_delete(program);
        glslang_shader_delete(shader);
        free(code);
        return VK_NULL_HANDLE;
    }

    // 6. Create Vulkan Module
      const VkShaderModuleCreateInfo info =
    {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = glslang_program_SPIRV_get_size(program) * sizeof(unsigned int),
        .pCode    = glslang_program_SPIRV_get_ptr(program)
    };
 

    VkShaderModule module;
    if (vkCreateShaderModule(dev, &info, NULL, &module) != VK_SUCCESS) {
        printf("[Glslang] Failed to create VkShaderModule for %s\n", path);
    } else {
        printf("[Glslang] Compiled successfully: %s\n", path);
    }

    // Cleanup
    glslang_program_delete(program);
    glslang_shader_delete(shader);
    free(code);

    return module;
}

// --- Internal: Core Build Logic ---
static GPUPipeline _build_internal(VkDevice device, GpBuilder *b) {
  GPUPipeline p = {0};

  // 1. Compile Shaders
  VkShaderModule fs = _compile_glsl_glslang(device, b->fs_path, GLSLANG_STAGE_FRAGMENT);
  VkShaderModule vs = _compile_glsl_glslang(device, b->vs_path, GLSLANG_STAGE_VERTEX);
  // If VS failed, we can't continue.
  if (vs == VK_NULL_HANDLE) {
    printf("[Pipeline] Failed to compile Vertex Shader. Aborting build.\n");
    if (fs) vkDestroyShaderModule(device, fs, NULL); // Cleanup if FS somehow succeeded
    return p;
  }
  
  if (fs == VK_NULL_HANDLE) {
     printf("[Pipeline] Failed to compile Fragment Shader. Aborting build.\n");
     vkDestroyShaderModule(device, vs, NULL);
     return p;
  }

  // ... (Rest of your build logic remains exactly the same) ...

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

  if (vkCreatePipelineLayout(device, &pl, NULL, &p.layout) != VK_SUCCESS) {
    printf("[Pipeline] Error: Failed to create layout.\n");
    if (vs) vkDestroyShaderModule(device, vs, NULL);
    if (fs) vkDestroyShaderModule(device, fs, NULL);
    return p;
  }

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

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, NULL, &p.pipeline) != VK_SUCCESS) {
    printf("[Pipeline] Error: Failed to create graphics pipeline.\n");
  }

  // Cleanup Shaders
  if (vs) vkDestroyShaderModule(device, vs, NULL);
  if (fs) vkDestroyShaderModule(device, fs, NULL);

  return p;
}

// --- Internal: Hot Reload Callback ---
static void _on_shader_changed(const char *path, void *user_data) {
  ReloadContext *ctx = (ReloadContext *)user_data;
  printf("[HotReload] Detected change in %s. Rebuilding pipeline...\n", path);
  vkDeviceWaitIdle(ctx->device);
  GPUPipeline new_pipe = _build_internal(ctx->device, &ctx->builder_state);
  if (new_pipe.pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(ctx->device, ctx->target_pipeline->pipeline, NULL);
    vkDestroyPipelineLayout(ctx->device, ctx->target_pipeline->layout, NULL);
    *ctx->target_pipeline = new_pipe;
    printf("[HotReload] Success!\n");
  } else {
    printf("[HotReload] Rebuild failed. Keeping old pipeline.\n");
  }
}

// --- Public API Implementation ---
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

void gp_set_color_formats(GpBuilder *b, const VkFormat *formats, uint32_t count) {
  if (count > 4) count = 4;
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

void gp_set_layout(GpBuilder *b, VkDescriptorSetLayout bindless, uint32_t push_size) {
  b->bindless_layout = bindless;
  b->push_const_size = push_size;
}

GPUPipeline gp_build(VkDevice device, GpBuilder *b) {
  return _build_internal(device, b);
}

void gp_register_hotreload(VkDevice device, GPUPipeline *target, GpBuilder *b) {
  if (!g_filewatcher) return;
  ReloadContext *ctx = malloc(sizeof(ReloadContext));
  ctx->device = device;
  ctx->target_pipeline = target;
  ctx->builder_state = *b;
  if (b->vs_path) fw_add_watch(g_filewatcher, b->vs_path, _on_shader_changed, ctx);
  if (b->fs_path) fw_add_watch(g_filewatcher, b->fs_path, _on_shader_changed, ctx);
}

void gp_destroy(VkDevice device, GPUPipeline *p) {
  if (p->pipeline) vkDestroyPipeline(device, p->pipeline, NULL);
  if (p->layout) vkDestroyPipelineLayout(device, p->layout, NULL);
  *p = (GPUPipeline){0};
}
