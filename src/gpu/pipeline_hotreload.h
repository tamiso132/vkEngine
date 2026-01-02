#pragma once
#include "filewatch.h"
#include "pipeline.h"

// PUBLIC FUNCTIONS

SystemFunc pr_system_get_func();

PipelineHandle pr_build_reg_cs(M_HotReload *pr, CpConfig b);
PipelineHandle pr_build_reg(M_HotReload *pr, GpConfig *b, const char *vs_path, const char *fs_path);
