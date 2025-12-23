#include "rendergraph.h"
#include "util.h" // Your vector helper
#include <string.h>

// --- Lookup Table ---
static const struct
{
    VkPipelineStageFlagBits2 stage;
    VkAccessFlags2 access;
    VkImageLayout layout;
} USAGE_INFO[] = {
    [RG_USAGE_VERTEX] = {VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED},
    [RG_USAGE_INDEX] = {VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED},
    [RG_USAGE_UNIFORM] = {VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED},
    [RG_USAGE_SAMPLED] = {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    [RG_USAGE_STORAGE_READ] = {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL},
    [RG_USAGE_STORAGE_WRITE] = {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL},
    [RG_USAGE_TRANSFER_SRC] = {VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
    [RG_USAGE_TRANSFER_DST] = {VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
    [RG_USAGE_COLOR] = {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    [RG_USAGE_DEPTH] = {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
};

// --- Internal Structs ---
typedef struct
{
    uint32_t id;
    VkPipelineStageFlagBits2 stage;
    VkAccessFlags2 access;
    VkImageLayout layout;
} RGRef;

typedef struct
{
    const char *name;
    RGExecuteCallback exec;
    void *user_data;
    RGRef *reads;  // Vector
    RGRef *writes; // Vector
} RGTask;

typedef struct
{
    VkPipelineStageFlagBits2 stage;
    VkAccessFlags2 access;
    VkImageLayout layout;
    VkPipelineStageFlagBits2 stage_write;
    VkAccessFlags2 access_write;
    bool initialized;
} RGTransState;

typedef struct
{
    uint32_t task_idx;
    VkImageMemoryBarrier2 *img_bars;
    VkBufferMemoryBarrier2 *buf_bars;
} RGBatch;

struct RenderGraph
{
    ResourceManager *rm;
    RGTask *tasks;        // Vector
    RGBatch *batches;     // Vector
    RGTransState *states; // Vector
};

// --- Helpers ---
static void _push_ref(RenderGraph *rg, uint32_t task_id, RGHandle res, RGUsage usage, bool write)
{
    RGTask *t = &rg->tasks[task_id];
    RGRef ref = {
        .id = res.id,
        .stage = USAGE_INFO[usage].stage,
        .access = USAGE_INFO[usage].access,
        .layout = USAGE_INFO[usage].layout};
    if (write)
        v_push(t->writes, ref);
    else
        v_push(t->reads, ref);
}

static void sync_resource(RGResource *res, RGTransState *state, RGRef *ref, bool is_write,
                          VkImageMemoryBarrier2 **i_bars, VkBufferMemoryBarrier2 **b_bars)
{
    if (!state->initialized)
    {
        state->stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        state->access = 0;
        state->layout = (res->type == RES_TYPE_IMAGE) ? res->img.current_layout : VK_IMAGE_LAYOUT_UNDEFINED;
        state->initialized = true;
    }

    bool layout_change = (res->type == RES_TYPE_IMAGE && state->layout != ref->layout && ref->layout != VK_IMAGE_LAYOUT_UNDEFINED);
    bool hazard = is_write ? (state->access != 0) : (state->access_write != 0);

    if (hazard || layout_change)
    {
        if (res->type == RES_TYPE_IMAGE)
        {
            VkImageMemoryBarrier2 b = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .image = res->img.img.vk,
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                .srcStageMask = state->stage,
                .srcAccessMask = state->access,
                .dstStageMask = ref->stage,
                .dstAccessMask = ref->access,
                .oldLayout = state->layout,
                .newLayout = ref->layout};
            v_push(*i_bars, b);
        }
        else
        {
            VkBufferMemoryBarrier2 b = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .buffer = res->buf.vk,
                .size = VK_WHOLE_SIZE,
                .srcStageMask = is_write ? state->stage : state->stage_write,
                .srcAccessMask = is_write ? state->access : state->access_write,
                .dstStageMask = ref->stage,
                .dstAccessMask = ref->access};
            v_push(*b_bars, b);
        }
    }

    state->stage = ref->stage;
    state->access = ref->access;
    if (res->type == RES_TYPE_IMAGE && ref->layout != VK_IMAGE_LAYOUT_UNDEFINED)
        state->layout = ref->layout;
    if (is_write)
    {
        state->stage_write = ref->stage;
        state->access_write = ref->access;
    }
}

// --- Public API ---

RenderGraph *rg_init(ResourceManager *rm)
{
    RenderGraph *rg = calloc(1, sizeof(RenderGraph));
    rg->rm = rm;
    return rg;
}

void rg_destroy(RenderGraph *rg)
{
    for (size_t i = 0; i < v_len(rg->tasks); i++)
    {
        v_free(rg->tasks[i].reads);
        v_free(rg->tasks[i].writes);
    }
    v_free(rg->tasks);
    for (size_t i = 0; i < v_len(rg->batches); i++)
    {
        v_free(rg->batches[i].img_bars);
        v_free(rg->batches[i].buf_bars);
    }
    v_free(rg->batches);
    v_free(rg->states);
    free(rg);
}

// 1. Create Pass (Builder Start)
RGPass rg_add_pass(RenderGraph *rg, const char *name, RGTaskType type, RGExecuteCallback exec, void *user_data)
{
    RGTask t = {.name = name, .exec = exec, .user_data = user_data};
    v_push(rg->tasks, t);
    return (RGPass){.rg = rg, .id = (uint32_t)v_len(rg->tasks) - 1};
}

// 2. Configure (Builder Methods)
void rg_pass_read(RGPass pass, RGHandle res, RGUsage usage)
{
    _push_ref(pass.rg, pass.id, res, usage, false);
}

void rg_pass_write(RGPass pass, RGHandle res, RGUsage usage)
{
    _push_ref(pass.rg, pass.id, res, usage, true);
}

void rg_pass_set_color_target(RGPass pass, RGHandle res, VkClearValue clear)
{
    rg_pass_write(pass, res, RG_USAGE_COLOR);
    // TODO: Store clear value in task if needed for dynamic rendering setup
}

void rg_pass_set_depth_target(RGPass pass, RGHandle res, VkClearValue clear)
{
    rg_pass_write(pass, res, RG_USAGE_DEPTH);
}

// 3. Compile
void rg_compile(RenderGraph *rg)
{
    // Reset state scratchpad
    if (v_len(rg->states) > 0)
        v_hdr(rg->states)->len = 0;
    size_t res_count = v_len(rg->rm->resources);
    for (size_t i = 0; i < res_count; i++)
        v_push(rg->states, (RGTransState){0});

    // Reset batches
    for (size_t i = 0; i < v_len(rg->batches); i++)
    {
        v_hdr(rg->batches[i].img_bars)->len = 0;
        v_hdr(rg->batches[i].buf_bars)->len = 0;
    }
    if (v_len(rg->batches) > 0)
        v_hdr(rg->batches)->len = 0;

    // Process Tasks
    for (size_t t_idx = 0; t_idx < v_len(rg->tasks); t_idx++)
    {
        RGTask *task = &rg->tasks[t_idx];
        RGBatch batch = {.task_idx = (uint32_t)t_idx};

        for (size_t i = 0; i < v_len(task->reads); i++)
            sync_resource(&rg->rm->resources[task->reads[i].id], &rg->states[task->reads[i].id],
                          &task->reads[i], false, &batch.img_bars, &batch.buf_bars);

        for (size_t i = 0; i < v_len(task->writes); i++)
            sync_resource(&rg->rm->resources[task->writes[i].id], &rg->states[task->writes[i].id],
                          &task->writes[i], true, &batch.img_bars, &batch.buf_bars);

        v_push(rg->batches, batch);
    }

    // Persist Layouts
    for (size_t i = 0; i < res_count; i++)
    {
        if (rg->states[i].initialized && rg->rm->resources[i].type == RES_TYPE_IMAGE)
        {
            rg->rm->resources[i].img.current_layout = rg->states[i].layout;
        }
    }
}

void rg_execute(RenderGraph *rg, VkCommandBuffer cmd)
{
    for (size_t i = 0; i < v_len(rg->batches); i++)
    {
        RGBatch *b = &rg->batches[i];

        if (v_len(b->img_bars) || v_len(b->buf_bars))
        {
            VkDependencyInfo dep = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = (uint32_t)v_len(b->img_bars),
                .pImageMemoryBarriers = b->img_bars,
                .bufferMemoryBarrierCount = (uint32_t)v_len(b->buf_bars),
                .pBufferMemoryBarriers = b->buf_bars};
            vkCmdPipelineBarrier2(cmd, &dep);
        }

        RGTask *t = &rg->tasks[b->task_idx];
        if (t->exec)
            t->exec(cmd, t->user_data);
    }
}