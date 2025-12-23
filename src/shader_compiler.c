#include <shaderc/shaderc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

// Helper to read text file
static char *read_txt(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc(size + 1);
    fread(data, 1, size, f);
    data[size] = 0;
    fclose(f);
    return data;
}

// Compile GLSL to SPIR-V
// kind: shaderc_vertex_shader, shaderc_fragment_shader, etc.
VkShaderModule compile_glsl(VkDevice dev, const char *path, shaderc_shader_kind kind)
{
    char *source = read_txt(path);
    if (!source)
    {
        printf("Failed to read shader: %s\n", path);
        return VK_NULL_HANDLE;
    }

    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compile_options_t options = shaderc_compile_options_initialize();

    // Optional: Optimization level
    // shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_performance);

    shaderc_compilation_result_t result = shaderc_compile_into_spv(
        compiler, source, strlen(source), kind, path, "main", options);

    VkShaderModule module = VK_NULL_HANDLE;

    if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success)
    {
        printf("Shader Error in %s:\n%s\n", path, shaderc_result_get_error_message(result));
    }
    else
    {
        // Create Vulkan Module
        const char *bytes = shaderc_result_get_bytes(result);
        size_t len = shaderc_result_get_length(result);

        VkShaderModuleCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = len,
            .pCode = (uint32_t *)bytes};
        vkCreateShaderModule(dev, &info, NULL, &module);
        printf("Recompiled: %s\n", path);
    }

    shaderc_result_release(result);
    shaderc_compile_options_release(options);
    shaderc_compiler_release(compiler);
    free(source);

    return module;
}