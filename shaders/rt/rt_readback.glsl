// rt_debug_pixel_log.glsl
// TLV (Type-Length-Value) Logging System
// ------------------------------------------------------------

#extension GL_EXT_nonuniform_qualifier : require


#ifndef RT_DEBUG_PIXEL_LOG_GLSL
#define RT_DEBUG_PIXEL_LOG_GLSL

#include "rt_shared.glsl"



#ifndef DBG_ENABLE
    #define DBG_ENABLE 1
#endif

#ifndef DBG_MAX_WORDS
    #define DBG_MAX_WORDS 64u
#endif

#if DBG_ENABLE

layout(set = 0, binding = BINDING_STORAGE_BUFFER) writeonly buffer ReadBackBuffer {
        uint data[];
} _dbg_pixel_logs[];

uint  _dbgi_local_buf[DBG_MAX_WORDS];
uint  _dbgi_cursor = 0u;
uint _dbgi_records_count = 0u;
uvec2 _dbgi_coords = uvec2(0u);


/**
 * Header Packing: [Event:8][Key:8][Type:8][Length:8]
 */
uint _dbgi_pack(uint ev, uint key, uint type, uint len) {
    return ((ev & 0xFFu) << 24u) | ((key & 0xFFu) << 16u) | ((type & 0xFFu) << 8u) | (len & 0xFFu);
}

void _dbgi_begin(uvec2 px) {
    _dbgi_coords = px;
    _dbgi_cursor = 0u;
    _dbgi_records_count = 0;
}

void _dbgi_push_tlv(uint header, uint len, uint d[4]) {
    _dbgi_records_count = _dbgi_records_count +1;
    if (_dbgi_cursor + 1u + len <= DBG_MAX_WORDS) {
        _dbgi_local_buf[_dbgi_cursor++] = header;
        for (uint i = 0u; i < len; ++i) {
            _dbgi_local_buf[_dbgi_cursor++] = d[i];
        }
    }
}

// --- Internal Writers ---


void _dbgi_f32(uint ev, uint key, float v) {
    uint d[4] = uint[4](floatBitsToUint(v), 0u, 0u, 0u);
    _dbgi_push_tlv(_dbgi_pack(ev, key, DBG_T_F32, 1u), 1u, d);
}

void _dbgi_u32(uint ev, uint key, uint v) {
    uint d[4] = uint[4](v, 0u, 0u, 0u);
    _dbgi_push_tlv(_dbgi_pack(ev, key, DBG_T_U32, 1u), 1u, d);
}

void _dbgi_vec3(uint ev, uint key, vec3 v) {
    uint d[4] = uint[4](floatBitsToUint(v.x), floatBitsToUint(v.y), floatBitsToUint(v.z), 0u);
    _dbgi_push_tlv(_dbgi_pack(ev, key, DBG_T_VEC3, 3u), 3u, d);
}

void _dbgi_vec4(uint ev, uint key, vec4 v) {
    uint d[4] = uint[4](floatBitsToUint(v.x), floatBitsToUint(v.y), floatBitsToUint(v.z), floatBitsToUint(v.w));
    _dbgi_push_tlv(_dbgi_pack(ev, key, DBG_T_VEC4, 4u), 4u, d);
}


void _dbgi_commit(PushRay p) {
    // Component-wise check to avoid the structure comparison error
    //if (_dbgi_coords.x != p.mouse_px.x || _dbgi_coords.y != p.mouse_px.y) return;

    // Use current extent to find pixel address
    uint pixIdx = _dbgi_coords.x + _dbgi_coords.y * uint(p.extent.x);
    uint base   = pixIdx * (1u + DBG_MAX_WORDS);
    
    // Use the index provided in the push constants for bindless access

    // [0] = Count, [1..N] = Data
    _dbg_pixel_logs[nonuniformEXT(p.readback_idx)].data[base] = _dbgi_records_count;

    for (uint i = 0u; i < _dbgi_cursor; i++) {
        _dbg_pixel_logs[nonuniformEXT(p.readback_idx)].data[base + 1u + i] = _dbgi_local_buf[i];
    }
}

#endif // DBG_ENABLE

// --- API Macros ---

#if DBG_ENABLE
    #define DBG_BEGIN()            _dbgi_begin(uvec2(gl_GlobalInvocationID.xy))
    #define DBG_BEGIN_PX(px)       _dbgi_begin(px)
    
    #define DBG_LOG_F32(ev, key, v)    _dbgi_f32(uint(ev), uint(key), float(v))
    #define DBG_LOG_U32(ev, key, v)    _dbgi_u32(uint(ev), uint(key), uint(v))
    #define DBG_LOG_VEC3(ev, key, v)   _dbgi_vec3(uint(ev), uint(key), vec3(v))
    #define DBG_LOG_VEC4(ev, key, v)   _dbgi_vec4(uint(ev), uint(key), vec4(v))

    #define DBG_COMMIT()           _dbgi_commit(pc)
#else
    #define DBG_BEGIN()
    #define DBG_BEGIN_PX(px)
    #define DBG_LOG_F32(ev, key, v)
    #define DBG_LOG_U32(ev, key, v)
    #define DBG_LOG_VEC3(ev, key, v)
    #define DBG_LOG_VEC4(ev, key, v)
    #define DBG_COMMIT()
#endif

#endif // RT_DEBUG_PIXEL_LOG_GLSL