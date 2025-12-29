#include "shader_compiler.h"
#include <glslang/Include/glslang_c_interface.h>
#include <stdio.h>
#include <stdlib.h>

// --- Private Prototypes ---
static char *read_file_text(const char *path, size_t *out_len);
static glsl_include_result_t *on_local_func_include(void *ctx,
                                                    const char *header_name,
                                                    const char *includer_name,
                                                    size_t include_depth);
static glsl_include_result_t *on_system_func_include(void *ctx,
                                                     const char *header_name,
                                                     const char *includer_name,
                                                     size_t include_depth);
static int on_free_include_result(void *ctx, glsl_include_result_t *result);
static glslang_resource_t get_default_resources();

VkShaderModule compile_glsl_to_spirv(VkDevice device, const char *path,
                                     ShaderStage stage) {
  size_t code_len;
  char *code = read_file_text(path, &code_len);
  if (!code)
    return VK_NULL_HANDLE;

  glslang_stage_t glsl_stage = (stage == SHADER_STAGE_VERTEX)
                                   ? GLSLANG_STAGE_VERTEX
                                   : GLSLANG_STAGE_FRAGMENT;
  const glslang_resource_t res = get_default_resources();

  const glslang_input_t input = {.language = GLSLANG_SOURCE_GLSL,
                                 .stage = glsl_stage,
                                 .client = GLSLANG_CLIENT_VULKAN,
                                 .client_version = GLSLANG_TARGET_VULKAN_1_2,
                                 .target_language_version =
                                     GLSLANG_TARGET_SPV_1_5,
                                 .code = code,
                                 .default_version = 450,
                                 .resource = &res,
                                 .messages = GLSLANG_MSG_DEFAULT_BIT};

  glslang_shader_t *shader = glslang_shader_create(&input);
  if (!glslang_shader_preprocess(shader, &input) ||
      !glslang_shader_parse(shader, &input)) {
    printf("[Shader] Compile Error in %s:\n%s\n", path,
           glslang_shader_get_info_log(shader));
    glslang_shader_delete(shader);
    free(code);
    return VK_NULL_HANDLE;
  }

  glslang_program_t *program = glslang_program_create();
  glslang_program_add_shader(program, shader);
  glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT |
                                    GLSLANG_MSG_VULKAN_RULES_BIT);
  glslang_program_SPIRV_generate(program, glsl_stage);

  VkShaderModuleCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize =
          glslang_program_SPIRV_get_size(program) * sizeof(unsigned int),
      .pCode = glslang_program_SPIRV_get_ptr(program)};

  VkShaderModule module;
  if (vkCreateShaderModule(device, &info, NULL, &module) != VK_SUCCESS)
    module = VK_NULL_HANDLE;

  glslang_program_delete(program);
  glslang_shader_delete(shader);
  free(code);
  return module;
}

// --- Private Functions ---
static char *read_file_text(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  size_t len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buffer = malloc(len + 1);
  fread(buffer, 1, len, f);
  buffer[len] = '\0';
  if (out_len)
    *out_len = len;
  fclose(f);
  return buffer;
}

static glsl_include_result_t *on_local_func_include(void *ctx,
                                                    const char *header_name,
                                                    const char *includer_name,
                                                    size_t include_depth) {
  glsl_include_result_t d;
  d.header_name = header_name;
}

/* Callback for system file inclusion */
static glsl_include_result_t *on_system_func_include(void *ctx,
                                                     const char *header_name,
                                                     const char *includer_name,
                                                     size_t include_depth) {}

/* Callback for include result destruction */
static int on_free_include_result(void *ctx, glsl_include_result_t *result) {}

// Minimal resursdefinition (kan expanderas vid behov)
static glslang_resource_t get_default_resources() {
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
      .limits = {.non_inductive_for_loops = 1,
                 .while_loops = 1,
                 .do_while_loops = 1,
                 .general_uniform_indexing = 1,
                 .general_attribute_matrix_vector_indexing = 1,
                 .general_varying_indexing = 1,
                 .general_sampler_indexing = 1,
                 .general_variable_indexing = 1,
                 .general_constant_matrix_vector_indexing = 1}};
  return default_resource;
}
