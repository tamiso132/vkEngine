#define GLFW_INCLUDE_VULKAN
#define VK_NO_PROTOTYPES // Usually handled by volk or similar, but if your gpu.h handles loading, keep or remove as needed.
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glslang/Include/glslang_c_interface.h>

// Include our headers
#include "gpu/gpu.h"
#include "gpu/pipeline.h"
#include "resmanager.h"
#include "rendergraph.h"


// --- User Data for the Pass ---
typedef struct
{
    RGHandle vbo;
    RGHandle output_tex;
    VkExtent2D extent;
    ResourceManager *rm;
    GPUPipeline pipeline;
} TriangleData;

// --- Execution Callback (GPU Commands) ---
// This runs when rg_execute() is called.
// --- Execution Callback ---
static void draw_triangle(VkCommandBuffer cmd, void *user_data)
{
    TriangleData *data = (TriangleData *)user_data;

    // 1. Get Resources
    VkImageView view = rm_get_image_view(data->rm, data->output_tex);

    // Note: We only need the ID for the shader, not the VkBuffer handle!
    uint32_t vbo_id = data->vbo.id;

    // 2. Begin Dynamic Rendering
    VkRenderingAttachmentInfo color_att = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {{{0.05f, 0.05f, 0.05f, 1.0f}}}};

    VkRenderingInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, data->extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_att};

    vkCmdBeginRendering(cmd, &render_info);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, data->pipeline.pipeline);

    // Bind Global Set (Set 0)
    VkDescriptorSet global_set = rm_get_bindless_set(data->rm);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            data->pipeline.layout, 0, 1, &global_set, 0, NULL);

    // 4. Set Dynamic State (Viewport/Scissor)
    VkViewport vp = {0, 0, (float)data->extent.width, (float)data->extent.height, 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc = {{0, 0}, data->extent};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // 5. Push Constants (Pass the VBO ID to the shader)
    vkCmdPushConstants(cmd, data->pipeline.layout, VK_SHADER_STAGE_ALL,
                       0, sizeof(uint32_t), &vbo_id);

    // 6. Draw (3 vertices) - No vertex buffer binding needed!
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);
}


int main()
{
    // 1. Init Window
    if (!glfwInit())
        return -1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(800, 600, "RenderGraph Demo", NULL, NULL);

    // 2. Init GPU
    GPUDevice device;
    if (!gpu_init(&device, window, &(GPUInstanceInfo){.enable_validation = true}))
    {
        printf("GPU Init Failed\n");
        return 1;
    }

    // 3. Init Swapchain
    GPUSwapchain swapchain;
    gpu_swapchain_init(&device, &swapchain, 800, 600);

  


    // 4. Init Managers
    ResourceManager rm;
    rm_init(&rm, &device);

    RenderGraph *rg = rg_init(&rm);

    // 5. Create Persistent Resources (Vertex Buffer)
    float vertices[] = {
        0.0f, -0.5f, 0.0f, // Top
        0.5f, 0.5f, 0.0f,  // Right
        -0.5f, 0.5f, 0.0f  // Left
    };

    // Upload logic usually involves a Staging Buffer, but for simplicity here
    // we assume rm_create_buffer with HOST_VISIBLE or a utility function handles it.
    // In a real engine: create staging -> map -> copy -> create gpu buffer -> copy cmd.
    RGHandle vbo = rm_create_buffer(&rm, "TriangleVBO", sizeof(vertices),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                        VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT);


  if (!glslang_initialize_process()) {
        printf("Failed to initialize glslang process.\n");
        exit(1);
    }

    GpBuilder b = gp_init();
    gp_set_shaders(&b, "shaders/triangle.vert", "shaders/triangle.frag");

    VkFormat surf_fmt = VK_FORMAT_B8G8R8A8_SRGB; // Check your swapchain!
    gp_set_color_formats(&b, &surf_fmt, 1);
    gp_set_layout(&b, rm_get_bindless_layout(&rm), sizeof(uint32_t));

    GPUPipeline g_triangle_pipe = gp_build(device.device, &b);
    gp_register_hotreload(device.device, &g_triangle_pipe, &b);



    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // 6. Acquire Next Image
        if (gpu_swapchain_acquire(&device, &swapchain))
        {
            // Note: In this loop, we REUSE the RenderGraph instance (rg)
            // but we compile it fresh every frame. The 'rg_compile' resets internal state.

            // A. Import Swapchain Image
            RGHandle h_swapchain = rm_import_image(
                &rm,
                "Backbuffer",
                swapchain.images[swapchain.current_img_idx],
                swapchain.views[swapchain.current_img_idx],
                VK_IMAGE_LAYOUT_UNDEFINED // We don't know the layout from acquire
            );

            // B. Build Graph
            TriangleData pass_data = {
                .rm = &rm,
                .vbo = vbo,
                .output_tex = h_swapchain,
                .extent = {swapchain.extent.width, swapchain.extent.height},
                .pipeline = g_triangle_pipe};

            // 1. Create Pass
            RGPass p = rg_add_pass(rg, "Draw Triangle", RG_TASK_GRAPHIC, draw_triangle, &pass_data);

            // 2. Declare Usage (This generates barriers automatically)
            rg_pass_read(p, vbo, RG_USAGE_VERTEX);

            // This sets the usage to COLOR_ATTACHMENT and handles clear values logic if we extended it
            rg_pass_set_color_target(p, h_swapchain, (VkClearValue){{0, 0, 0, 1}});

            // 3. Compile (Calculates barriers)
            rg_compile(rg);

            // --- EXECUTION ---

            VkCommandBuffer cmd = device.imm_cmd_buffer; // Or from a per-frame pool

            // Reset & Begin Command Buffer
            vkResetCommandBuffer(cmd, 0);
            VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            vkBeginCommandBuffer(cmd, &bi);

            // 4. Execute Graph (Inserts barriers + runs callback)
            // Leaves h_swapchain in VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            rg_execute(rg, cmd);

            // 5. Manual Barrier for Presentation
            // RenderGraph leaves the image ready for the last task (rendering).
            // The Swapchain needs it in PRESENT_SRC_KHR.
            VkImageMemoryBarrier2 to_present = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                .dstAccessMask = 0,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .image = swapchain.images[swapchain.current_img_idx],
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

            VkDependencyInfo dep = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &to_present};
            vkCmdPipelineBarrier2(cmd, &dep);

            vkEndCommandBuffer(cmd);

            // 6. Submit
            VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo si = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &swapchain.acquire_sem,
                .pWaitDstStageMask = &wait_stage,
                .commandBufferCount = 1,
                .pCommandBuffers = &cmd,
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &swapchain.present_sem};

            vkQueueSubmit(device.graphics_queue, 1, &si, swapchain.frame_fence);

            // 7. Present
            gpu_swapchain_present(&device, &swapchain, device.graphics_queue);

            // Note: We do NOT destroy the RM/RG here inside the loop.
            // rg_compile() clears the graph for the next frame.
            // rm_import_image handles adding the swapchain image (you might want a logic to reset imported resources every frame in RM if the list grows too large).
        }
    }

    vkDeviceWaitIdle(device.device);

    // Cleanup
    rg_destroy(rg);
    rm_destroy(&rm);

    gpu_swapchain_destroy(&device, &swapchain); // Assuming this exists
    gpu_destroy(&device);

    glfwDestroyWindow(window);
    glfwTerminate();
    glslang_finalize_process();
    return 0;
}