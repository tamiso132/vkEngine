#pragma once
#include "filewatch.h"
#include "pipeline.h"
// PUBLIC FUNCTIONS
typedef struct M_PipelineReloader M_PipelineReloader;

M_PipelineReloader *pr_init(M_Pipeline *pm, FileManager *fm);

PipelineHandle pr_build_reg(M_PipelineReloader *pr, GpBuilder *b,
                            const char *vs_path, const char *fs_path);

void pr_update_modifed(M_PipelineReloader *pr);
void gp_register_hotreload(M_Pipeline *pm);
