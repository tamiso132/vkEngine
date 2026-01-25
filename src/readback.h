#pragma once
#include "resource/resmanager.h"



// --- Public API ---

// Create the GPU-side storage buffer for debug logging
ResHandle dbgr_create_buffer(M_Resource* rm, uint32_t width, uint32_t height);

// Read back a specific pixel from the buffer and print the log to stdout
void dbgr_analyze_pixel(M_GPU* gpu, M_Resource* rm, ResHandle buf_handle, uint32_t buffer_width, int x, int y);