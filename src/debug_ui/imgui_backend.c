#include "imgui_backend.h"
#include <cimgui.h>
#include <stdio.h>

// Helper to upload the font atlas to your bindless system
static void _upload_font_atlas(ImGuiContext_Vk* ctx, CmdBuffer cmd) {
    ImGuiIO* io = igGetIO_ContextPtr();
    
    unsigned char* pixels;
    int width, height;
    // 1. Get raw RGBA pixels from ImGui
    ImFontAtlas(io->Fonts, &pixels, &width, &height, NULL);
    // 2. Create Image via Resource Manager
    RGImageInfo info = {
        .name = "ImGui_Font_Atlas",
        .width = width,
        .height = height,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preset = RG_IMAGETYPE_TEXTURE
    };
    ctx->font_texture = rm_create_image(ctx->rm, info);

    // 3. Upload to GPU using your engine's staging buffer
    size_t upload_size = width * height * 4;
    RmStageSlice slice = rm_get_stage_buffer(ctx->rm, pixels, upload_size, 4);

    // Barrier: Transfer Write
    cmd_sync_image(cmd, ctx->rm, ctx->font_texture, STATE_TRANSFER, ACCESS_WRITE);

    // Copy Buffer to Image
    RImage* ri = rm_get_image(ctx->rm, ctx->font_texture);
    VkBufferImageCopy region = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {width, height, 1},
        .bufferOffset = slice.offset
    };
    vkCmdCopyBufferToImage(cmd.buffer, slice.buffer, ri->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Barrier: Shader Read
    cmd_sync_image(cmd, ctx->rm, ctx->font_texture, STATE_SHADER, ACCESS_READ);

    // 4. Store the Bindless Index as the ImGui Texture ID
    // This allows us to retrieve it inside the render loop later
    uint32_t bindless_id = rm_get_image_descriptor_index(ctx->rm, ctx->font_texture);
    ImFontAtlas_SetTexID(io->Fonts, (ImTextureID)(uintptr_t)bindless_id);
}

void imgui_vk_init(ImGuiContext_Vk* ctx, M_GPU* gpu, M_Resource* rm, M_HotReload* pr, CmdBuffer cmd, VkFormat color_format, GLFWwindow* window) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->gpu = gpu;
    ctx->rm = rm;

    // --- 1. Dear ImGui Context Setup ---
    igCreateContext(NULL);
    ImGuiIO* io = igGetIO();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking

    // Optional: Setup Style
    igStyleColorsDark(NULL);

    // --- 2. Create Resizeable Buffers ---
    // We start with a reasonable size. If frame data exceeds this, we will resize in render().
    RGBufferInfo vbi = { 
        .name="ImGui_Vertex_Buffer", 
        .capacity = 10000 * sizeof(ImDrawVert), 
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 
        .mem = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 
    };
    ctx->vtx_buffer = rm_create_buffer(rm, &vbi);

    RGBufferInfo ibi = { 
        .name="ImGui_Index_Buffer", 
        .capacity = 10000 * sizeof(ImDrawIdx), 
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
        .mem = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 
    };
    ctx->idx_buffer = rm_create_buffer(rm, &ibi);

    // --- 3. Upload Font Atlas ---
    _upload_font_atlas(ctx, cmd);

    // --- 4. Build Pipeline ---
    // We use your engine's pipeline builder to support hot-reloading automatically.
    GpConfig config = gp_init("ImGui_Pipeline");
    
    // ImGui uses standard triangle lists
    gp_set_topology(&config, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    // Disable culling so we can flip UVs if needed
    gp_set_cull(&config, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    
    // Color Blending (Standard Alpha Blend)
    gp_set_color_formats(&config, &color_format, 1);
    gp_enable_blend(&config);
    
    // No Depth Test for UI
    config.depth_test = false;
    config.depth_write = false;

    // Use your engine's Bindless Layout
    VkDescriptorSetLayout bindless_layout = rm_get_bindless_layout(rm);
    
    // Push Constants for Scale/Translate/TextureID
    struct { float scale[2]; float translate[2]; uint32_t tex_id; } pc = {0};
    gp_set_layout(&config, bindless_layout, sizeof(pc));

    // Register with Hot Reloader
    // Ensure shaders/imgui.vert and shaders/imgui.frag exist!
    ctx->pipeline = pr_build_reg(pr, &config, "shaders/imgui.vert", "shaders/imgui.frag");
}