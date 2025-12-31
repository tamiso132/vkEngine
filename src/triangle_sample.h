#include "sample_interface.h"
// PUBLIC FUNCTIONS

void tri_init(Sample *self, SampleContext *ctx);

void tri_render(Sample *self, SampleContext *ctx);

void tri_resize(Sample *self, SampleContext *ctx);

void tri_destroy(Sample *self, Managers *mg);

Sample create_triangle_sample();
