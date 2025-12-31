
#include "raytrace_sample.h"

// --- Private Prototypes ---
static void _raytrace_init(Sample *self, SampleContext *ctx);
static void _render(Sample *self, SampleContext *ctx);
static void __resize(Sample *self, SampleContext *ctx);
static void __destroy(Sample *self, Managers *mg);

Sample create_raytrace_sample();
// --- Private Functions ---

static void _raytrace_init(Sample *self, SampleContext *ctx) {}

static void _render(Sample *self, SampleContext *ctx) {}

static void __resize(Sample *self, SampleContext *ctx) {}

static void __destroy(Sample *self, Managers *mg) {}
