#pragma once
#include <volk.h>

#include <GLFW/glfw3.h>
#include <stdbool.h>
#include <string.h>
#include <cglm/cglm.h>
#include "common.h"
// GLFW constants are usually < 350 for keys and < 10 for mouse
#define MAX_KEYS 512
#define MAX_BUTTONS 16
#define INPUT_TEXT_MAX 256

typedef struct Input {
    GLFWwindow* window;
    
    // Keyboard State
    uint8_t keys[MAX_KEYS];      // Current frame
    uint8_t prev_keys[MAX_KEYS]; // Previous frame
    
    // Mouse Button State
    uint8_t buttons[MAX_BUTTONS];
    uint8_t prev_buttons[MAX_BUTTONS];

    // Mouse Position
    double mouse_x, mouse_y;
    double prev_mouse_x, prev_mouse_y;
    double mouse_dx, mouse_dy; // Delta movement this frame

    u32 text[INPUT_TEXT_MAX];
    u32 text_len;
} Input;

void input_init(Input* input, GLFWwindow* window);
void input_update(Input* input);

// Returns true ONLY on the frame the key was pressed (Rising Edge)
bool input_key_pressed(Input* input, int key);
// Returns true as long as the key is held down
bool input_key_down(Input* input, int key);
// Returns true ONLY on the frame the key was released (Falling Edge)
bool input_key_released(Input* input, int key);

// Same for Mouse
bool input_mouse_pressed(Input* input, int button);
bool input_button_down(Input* input, int button);

void input_get_mouse_position(Input* input, ivec2 mouse_pos);