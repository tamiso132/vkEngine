#pragma once

// PUBLIC FUNCTIONS

 void init_managers(Managers *mg, GPUDevice *device);

 void glslang_compile_test(GPUDevice device);

 void triangle_hotreload_test(Managers *mg, GLFWwindow *window, GPUSwapchain *swapchain);

 int main();

