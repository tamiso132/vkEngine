#include "input.h"

void input_init(Input* input, GLFWwindow* window) {
    memset(input, 0, sizeof(Input));
    input->window = window;
    
    // Init mouse pos to avoid huge jump on first frame
    glfwGetCursorPos(window, &input->mouse_x, &input->mouse_y);
    input->prev_mouse_x = input->mouse_x;
    input->prev_mouse_y = input->mouse_y;
}

void input_update(Input* input) {
    // 1. Cycle State (Current -> Prev)
    memcpy(input->prev_keys, input->keys, MAX_KEYS);
    memcpy(input->prev_buttons, input->buttons, MAX_BUTTONS);
    
    input->prev_mouse_x = input->mouse_x;
    input->prev_mouse_y = input->mouse_y;

    // 2. Poll New State from GLFW
    // Note: We scan all keys. This is very fast (just array reads in GLFW).
    // If you want to optimize, you can use a callback to set flags instead.
    for (int i = 32; i < GLFW_KEY_LAST; i++) {
        input->keys[i] = (uint8_t)glfwGetKey(input->window, i);
    }

    for (int i = 0; i < GLFW_MOUSE_BUTTON_LAST; i++) {
        input->buttons[i] = (uint8_t)glfwGetMouseButton(input->window, i);
    }

    glfwGetCursorPos(input->window, &input->mouse_x, &input->mouse_y);
    input->mouse_dx = input->mouse_x - input->prev_mouse_x;
    input->mouse_dy = input->mouse_y - input->prev_mouse_y;
}

bool input_key_pressed(Input* input, int key) {
    return (input->keys[key] == GLFW_PRESS) && (input->prev_keys[key] == GLFW_RELEASE);
}

bool input_key_down(Input* input, int key) {
    return (input->keys[key] == GLFW_PRESS);
}

bool input_key_released(Input* input, int key) {
    return (input->keys[key] == GLFW_RELEASE) && (input->prev_keys[key] == GLFW_PRESS);
}

bool input_button_pressed(Input* input, int button) {
    return (input->buttons[button] == GLFW_PRESS) && (input->prev_buttons[button] == GLFW_RELEASE);
}

bool input_button_down(Input* input, int button) {
    return (input->buttons[button] == GLFW_PRESS);
}