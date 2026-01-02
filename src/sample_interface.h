#pragma once
#include "command.h"
#include "gpu/gpu.h"
#include "gpu/pipeline_hotreload.h"
#include "submit_manager.h"

typedef struct {
  ResourceMa *rm;
  M_SubmitManager *sm;
  M_Pipeline *pm;
  M_PipelineReloader *reloader;
  FileManager *fm;
} Managers;

typedef struct {
  Managers *mg;
  GPUDevice *device;
  GPUSwapchain *swapchain;
  CmdBuffer cmd;     // Den aktuella kommandobuffern för denna frame
  VkExtent2D extent; // Nuvarande fönsterstorlek
  float dt;          // Delta time (kan läggas till senare)
} SampleContext;

static inline void init_managers(Managers *mg, GPUDevice *device) {
  mg->rm = rm_init(device);
  mg->pm = pm_init(mg->rm);
  mg->fm = fm_init();
  mg->reloader = pr_init(mg->pm, mg->fm);
  mg->sm = submit_manager_create(device->device, device->graphics_queue, 1);
}
// Interface för ett sample
typedef struct Sample {
  const char *name;
  void *user_data;

  void (*init)(struct Sample *self, SampleContext *ctx);
  void (*pre_render)(struct Sample *self, SampleContext *ctx);
  void (*render)(struct Sample *self, SampleContext *ctx);
  void (*post_render)(struct Sample *self, SampleContext *ctx);
  void (*on_resize)(struct Sample *self, SampleContext *ctx);

  void (*destroy)(struct Sample *self, Managers *mg);
} Sample;

// Funktion för att köra ett sample (implementeras i sample_runner.c)
void run_sample(Sample *sample, Managers *mg, GPUDevice *device, GLFWwindow *window, GPUSwapchain *swapchain);
