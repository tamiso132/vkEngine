#pragma once
#include "shaders/rt/rt_shared.glsl"
#include "vector.h"
#include <cglm/cglm.h>
#include <common.h>

#define MAX_CHUNK_VISIBILITY 4
#define GRID_VOL (MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY)
