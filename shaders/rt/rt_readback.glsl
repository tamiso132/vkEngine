// rt_debug_pixel_log.glsl
//
// Per-pixel multi-entry readback log.
// - Each pixel writes N records into its own slot
// - No atomics needed (1 invocation == 1 pixel)
// - Great for "click pixel -> see all data"
// - No frame history by default (just overwrite every frame)
//
// Record format (uvec4):
//   x = header: [31..24] event, [23..16] key, [15..8] type, [7..0] part
//   y/z/w = payload words (type-dependent)
//
// Push constant assumed to be named "pc".
// Debug buffer index assumed to be pc.readback_id (descriptor array).
//
// ------------------------------------------------------------

#ifndef RT_DEBUG_PIXEL_LOG_GLSL
#define RT_DEBUG_PIXEL_LOG_GLSL

// =============================
// User overrides (optional)
// =============================

#ifndef DBG_ENABLE
#define DBG_ENABLE 1
#endif

#ifndef DBG_SET
#define DBG_SET 0
#endif

#ifndef DBG_BINDING
#define DBG_BINDING 7
#endif

// How many uvec4 records each pixel can store
#ifndef DBG_MAX_RECORDS_PER_PIXEL
#define DBG_MAX_RECORDS_PER_PIXEL 16u
#endif

// Which debug buffer to write to (descriptor array index)
#ifndef DBG_STREAM_INDEX
#define DBG_STREAM_INDEX nonuniformEXT(pc.readback_id)
#endif

// How to compute pixel linear index
// Default assumes pc.extent.x exists
#ifndef DBG_WIDTH_U32
#define DBG_WIDTH_U32 uint(pc.extent.x)
#endif

// =============================
// Type IDs
// =============================
#define DBG_T_U32    1u
#define DBG_T_I32    2u
#define DBG_T_F32    3u
#define DBG_T_VEC2   4u
#define DBG_T_VEC3   5u
#define DBG_T_VEC4   6u
#define DBG_T_IVEC2  7u
#define DBG_T_IVEC3  8u
#define DBG_T_UVEC2  9u
#define DBG_T_UVEC3  10u

#if DBG_ENABLE

// active buffer
#define __DBG_BUF (__dbg_pixel_logs[DBG_STREAM_INDEX])

// pixel stride in uvec4 units
#define __DBG_STRIDE (1u + DBG_MAX_RECORDS_PER_PIXEL)

// =============================
// Internal state (per invocation)
// =============================
uvec4 __dbg_local_records[DBG_MAX_RECORDS_PER_PIXEL];
uint __dbg_local_count = 0u;
uvec2 __dbg_local_pixel = uvec2(0u);
uint __dbg_runtime_enabled = 1u;

// =============================
// Internal helpers
// =============================

uint __dbg_pack_header(uint event_, uint key_, uint type_, uint part_) {
        return (event_ << 24u) | (key_ << 16u) | (type_ << 8u) | (part_ & 0xFFu);
}

uint __dbg_pixel_index(uvec2 px) {
        return px.x + px.y * DBG_WIDTH_U32;
}

void __dbg_begin(uvec2 px) {
        __dbg_local_pixel = px;
        __dbg_local_count = 0u;
}

void __dbg_set_enabled(uint enabled) {
        __dbg_runtime_enabled = enabled;
}

void __dbg_push(uvec4 rec) {
        if (__dbg_runtime_enabled == 0u) return;
        if (__dbg_local_count < DBG_MAX_RECORDS_PER_PIXEL) {
                __dbg_local_records[__dbg_local_count++] = rec;
        }
}

// =============================
// Typed writers -> local records
// =============================

void __dbg_u32(uint ev, uint key, uint v) {
        __dbg_push(uvec4(__dbg_pack_header(ev, key, DBG_T_U32, 0u), v, 0u, 0u));
}

void __dbg_i32(uint ev, uint key, int v) {
        __dbg_push(uvec4(__dbg_pack_header(ev, key, DBG_T_I32, 0u), uint(v), 0u, 0u));
}

void __dbg_f32(uint ev, uint key, float v) {
        __dbg_push(uvec4(__dbg_pack_header(ev, key, DBG_T_F32, 0u), floatBitsToUint(v), 0u, 0u));
}

void __dbg_vec2(uint ev, uint key, vec2 v) {
        __dbg_push(uvec4(__dbg_pack_header(ev, key, DBG_T_VEC2, 0u),
                        floatBitsToUint(v.x), floatBitsToUint(v.y), 0u));
}

// vec3 uses 2 records: part0(x,y), part1(z,_)
void __dbg_vec3(uint ev, uint key, vec3 v) {
        __dbg_push(uvec4(__dbg_pack_header(ev, key, DBG_T_VEC3, 0u),
                        floatBitsToUint(v.x), floatBitsToUint(v.y), 0u));
        __dbg_push(uvec4(__dbg_pack_header(ev, key, DBG_T_VEC3, 1u),
                        floatBitsToUint(v.z), 0u, 0u));
}

// vec4 uses 2 records: part0(x,y), part1(z,w)
void __dbg_vec4(uint ev, uint key, vec4 v) {
        __dbg_push(uvec4(__dbg_pack_header(ev, key, DBG_T_VEC4, 0u),
                        floatBitsToUint(v.x), floatBitsToUint(v.y), 0u));
        __dbg_push(uvec4(__dbg_pack_header(ev, key, DBG_T_VEC4, 1u),
                        floatBitsToUint(v.z), floatBitsToUint(v.w), 0u));
}

void __dbg_ivec2(uint ev, uint key, ivec2 v) {
        __dbg_push(uvec4(__dbg_pack_header(ev, key, DBG_T_IVEC2, 0u),
                        uint(v.x), uint(v.y), 0u));
}

void __dbg_ivec3(uint ev, uint key, ivec3 v) {
        __dbg_push(uvec4(__dbg_pack_header(ev, key, DBG_T_IVEC3, 0u),
                        uint(v.x), uint(v.y), 0u));
        __dbg_push(uvec4(__dbg_pack_header(ev, key, DBG_T_IVEC3, 1u),
                        uint(v.z), 0u, 0u));
}

void __dbg_uvec2(uint ev, uint key, uvec2 v) {
        __dbg_push(uvec4(__dbg_pack_header(ev, key, DBG_T_UVEC2, 0u),
                        v.x, v.y, 0u));
}

void __dbg_uvec3(uint ev, uint key, uvec3 v) {
        __dbg_push(uvec4(__dbg_pack_header(ev, key, DBG_T_UVEC3, 0u),
                        v.x, v.y, 0u));
        __dbg_push(uvec4(__dbg_pack_header(ev, key, DBG_T_UVEC3, 1u),
                        v.z, 0u, 0u));
}

// =============================
// Commit: write to SSBO (one pixel)
// =============================
//
// Call once per invocation (pixel).
// Overwrites old data each frame (no history).
//
void __dbg_commit() {
        uint pixIdx = __dbg_pixel_index(__dbg_local_pixel);
        uint base = pixIdx * __DBG_STRIDE;

        // meta slot: count in .x
        __DBG_BUF.data[base + 0u] = uvec4(__dbg_local_count, 0u, 0u, 0u);

        // records
        uint n = min(__dbg_local_count, DBG_MAX_RECORDS_PER_PIXEL);
        for (uint i = 0u; i < n; i++) {
                __DBG_BUF.data[base + 1u + i] = __dbg_local_records[i];
        }

        // Optional: clear remaining slots (only needed if your viewer reads beyond count)
        // for (uint i = n; i < DBG_MAX_RECORDS_PER_PIXEL; i++) {
        //     __DBG_BUF.data[base + 1u + i] = uvec4(0u);
        // }
}

#endif // DBG_ENABLE

// =============================
// Public macros (preferred API)
// =============================

#if DBG_ENABLE

#define DBG_BEGIN_DEFAULT()         __dbg_begin(uvec2(gl_GlobalInvocationID.xy))
#define DBG_BEGIN_PX(px_uvec2)      __dbg_begin((px_uvec2))
#define DBG_SET_ENABLED(flag_u32)   __dbg_set_enabled((flag_u32))

#define DBG_U32(ev,key,v)           __dbg_u32(uint(ev), uint(key), uint(v))
#define DBG_I32(ev,key,v)           __dbg_i32(uint(ev), uint(key), int(v))
#define DBG_F32(ev,key,v)           __dbg_f32(uint(ev), uint(key), float(v))
#define DBG_VEC2(ev,key,v)          __dbg_vec2(uint(ev), uint(key), vec2(v))
#define DBG_VEC3(ev,key,v)          __dbg_vec3(uint(ev), uint(key), vec3(v))
#define DBG_VEC4(ev,key,v)          __dbg_vec4(uint(ev), uint(key), vec4(v))
#define DBG_IVEC2(ev,key,v)         __dbg_ivec2(uint(ev), uint(key), ivec2(v))
#define DBG_IVEC3(ev,key,v)         __dbg_ivec3(uint(ev), uint(key), ivec3(v))
#define DBG_UVEC2(ev,key,v)         __dbg_uvec2(uint(ev), uint(key), uvec2(v))
#define DBG_UVEC3(ev,key,v)         __dbg_uvec3(uint(ev), uint(key), uvec3(v))

#define DBG_COMMIT()                __dbg_commit()

#else

#define DBG_BEGIN_DEFAULT()
#define DBG_BEGIN_PX(px_uvec2)
#define DBG_SET_ENABLED(flag_u32)

#define DBG_U32(ev,key,v)
#define DBG_I32(ev,key,v)
#define DBG_F32(ev,key,v)
#define DBG_VEC2(ev,key,v)
#define DBG_VEC3(ev,key,v)
#define DBG_VEC4(ev,key,v)
#define DBG_IVEC2(ev,key,v)
#define DBG_IVEC3(ev,key,v)
#define DBG_UVEC2(ev,key,v)
#define DBG_UVEC3(ev,key,v)

#define DBG_COMMIT()

#endif // DBG_ENABLE

#endif // RT_DEBUG_PIXEL_LOG_GLSL
