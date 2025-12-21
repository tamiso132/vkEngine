#include "gpu/gpu.h"
#include "rendergraph.h"
#include <stdio.h>

void draw_task(VkCommandBuffer cmd, void* user_data) {
    // 1. Rendering is now just commands
    // In DAXA/C-RenderGraph, barriers are already handled before this is called!
    
    // Simple clear pass using Dynamic Rendering (Vulkan 1.3)
    VkRenderingAttachmentInfo colorAtt = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = (VkImageView)user_data, // Passed from main loop
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .color = { {0.2f, 0.4f, 1.0f, 1.0f} } }
    };
    
    VkRenderingInfo renderInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {0,0}, {800,600} },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAtt
    };
    
    vkCmdBeginRendering(cmd, &renderInfo);
    vkCmdEndRendering(cmd);
}

int main() {
    // 1. Init Window
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Mini DAXA C", NULL, NULL);

    // 2. Init GPU (DAXA style: just works)
    GPUDevice device;
    if (!gpu_init(&device, window, &(GPUInstanceInfo){ .enable_validation = true })) {
        printf("GPU Init Failed\n");
        return 1;
    }

    // 3. Init Swapchain
    GPUSwapchain swapchain;
    gpu_swapchain_init(&device, &swapchain, 800, 600);

    // 4. Init RenderGraph
    RenderGraph rg;
    rg_init(&rg, device.device);

    // 5. Create a managed buffer (Example of easy API)
    GPUBuffer meshBuffer = gpu_create_buffer(&device, &(GPUBufferInfo){
        .size = 1024,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
        .debug_name = "My Mesh"
    });

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // 1. Acquire
        if (gpu_swapchain_acquire(&device, &swapchain)) {
            
            // 2. Define Graph (rebuilt per frame usually in immediate mode graphs)
            rg_import_image(&rg, 0, swapchain.images[swapchain.current_img_idx], VK_IMAGE_LAYOUT_UNDEFINED);
            
            RGTask* task = rg_add_task(&rg, "Clear Pass", RG_TASK_GRAPHIC, draw_task, swapchain.views[swapchain.current_img_idx]);
            
            // Declare that this task writes to the swapchain image
            // The RenderGraph will automatically insert the Layout Transition barrier 
            // from UNDEFINED -> COLOR_ATTACHMENT -> PRESENT_SRC
            rg_task_write_image(task, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

            // 3. Execute
            VkCommandBuffer cmd = device.imm_cmd_buffer; // Using immediate for simplicity here, real engine uses per-frame cmd
            vkResetCommandBuffer(cmd, 0);
            
            VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            vkBeginCommandBuffer(cmd, &bi);
            
            rg_compile(&rg);
            rg_execute(&rg, cmd);
            
            // Manual barrier to transition to PRESENT (if RG doesn't handle final layout)
            // ... (Usually RG handles this if you add a 'present' node)

            vkEndCommandBuffer(cmd);

            // 4. Submit
            VkSubmitInfo si = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .waitSemaphoreCount = 1, .pWaitSemaphores = &swapchain.acquire_sem,
                .commandBufferCount = 1, .pCommandBuffers = &cmd,
                .signalSemaphoreCount = 1, .pSignalSemaphores = &swapchain.present_sem
            };
            vkQueueSubmit(device.graphics_queue, 1, &si, swapchain.frame_fence);

            // 5. Present
            gpu_swapchain_present(&device, &swapchain, device.graphics_queue);
        }
    }

    gpu_destroy(&device);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}