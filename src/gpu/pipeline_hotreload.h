#pragma once
#include "filewatch.h"
#include "pipeline.h"

typedef struct M_PipelineReloader M_PipelineReloader;

// PUBLIC FUNCTIONS
M_PipelineReloader *pr_init(M_Pipeline *pm, FileManager *fm);
void pr_update_modifed(M_PipelineReloader *pr);

PipelineHandle pr_build_reg_cs(M_PipelineReloader *pr, CpConfig b);
PipelineHandle pr_build_reg(M_PipelineReloader *pr, GpConfig *b, const char *vs_path, const char *fs_path);
