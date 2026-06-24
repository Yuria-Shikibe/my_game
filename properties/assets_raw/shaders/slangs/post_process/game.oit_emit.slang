import post_process_lib;

static const uint INVALID_NODE = 0xffffffffu;
static const uint NODES_PER_PIXEL = 6u;
static const uint MAX_SHAPES = 96u;
static const uint TILE_MASK_WORD_COUNT = 4u;
static const uint MAX_POLYGON_VERTICES = 8u;
static const uint PACKED_POLYGON_VERTEX_COUNT = MAX_POLYGON_VERTICES / 2u;
static const uint MAX_DEBUG_LINES = 4096u;
static const uint MAX_DEBUG_RINGS = 4096u;
static const uint MAX_DEBUG_CLOSED_POLYLINES = 2048u;
static const uint MAX_DEBUG_POLYLINE_VERTICES = 16384u;
static const float GAME_OIT_SHAPE_SOFTNESS = 0.012f;

struct GameOitNode {
    float4 color_base;
    float4 color_light;
    float depth;
    uint next;
};

struct GameOitEmitShape {
    float2 center;
    float2 half_extent;
    float2 axis_begin;
    float2 axis_end;
    float4 polygon_vertices[PACKED_POLYGON_VERTEX_COUNT];
    float4 color_base;
    float4 color_light;
    float radius;
    float depth;
    float opacity;
    int shape_kind;
    int enabled;
    int polygon_vertex_count;
    float reserved0;
    float reserved1;
};

struct GameOitEmitParams {
    int shape_count;
    int tile_grid_width;
    int tile_grid_height;
    int use_tile_mask;
    int debug_line_count;
    int debug_ring_count;
    int debug_closed_polyline_count;
    int debug_vertex_count;
    float time_seconds;
    float bloom_hint_strength;
    int transparent_overlap_enabled;
    int bloom_enabled;
    float reserved0;
    float reserved1;
};

struct GameOitTileMask {
    uint4 shape_words;
};

struct GameDebugDrawLine {
    float2 src;
    float2 dst;
    float4 color;
    float stroke;
    float depth;
    float reserved0;
    float reserved1;
};

struct GameDebugDrawRing {
    float2 center;
    float2 radius;
    float4 color;
    float initial_angle;
    float turn_range;
    uint segments;
    float depth;
};

struct GameDebugDrawClosedPolyline {
    uint vertex_offset;
    uint vertex_count;
    float stroke;
    float depth;
    float4 color;
};

struct GameDebugDrawVertex {
    float2 position;
    float reserved0;
    float reserved1;
};

[[vk::binding(0, 0)]]
RWStructuredBuffer<GameOitNode> output_nodes;

[[vk::binding(1, 0)]]
restrict RWTexture2D<uint> output_head_indices;

[[vk::binding(2, 0)]]
restrict RWTexture2D<float> output_depth;

[[vk::binding(3, 0)]]
restrict RWTexture2D<float4> output_color_base;

[[vk::binding(4, 0)]]
restrict RWTexture2D<float4> output_color_light;

[[vk::binding(5, 0)]]
Texture2D input_color_base;

[[vk::binding(6, 0)]]
StructuredBuffer<GameOitEmitShape> input_shapes;

[[vk::binding(7, 0)]]
StructuredBuffer<GameOitTileMask> input_tile_masks;

[[vk::binding(9, 0)]]
StructuredBuffer<GameDebugDrawLine> input_debug_lines;

[[vk::binding(10, 0)]]
StructuredBuffer<GameDebugDrawRing> input_debug_rings;

[[vk::binding(11, 0)]]
StructuredBuffer<GameDebugDrawClosedPolyline> input_debug_closed_polylines;

[[vk::binding(12, 0)]]
StructuredBuffer<GameDebugDrawVertex> input_debug_vertices;

[[vk::binding(8, 0)]]
ConstantBuffer<GameOitEmitParams> params;

[ForceInline]
float circle_mask(float2 p, float2 center, float radius, float softness) {
    return 1.0f - smoothstep(radius - softness, radius + softness, length(p - center));
}

[ForceInline]
float box_mask(float2 p, float2 center, float2 half_extent, float softness) {
    let q = abs(p - center) - half_extent;
    let dist = length(max(q, 0.0f)) + min(max(q.x, q.y), 0.0f);
    return 1.0f - smoothstep(-softness, softness, dist);
}

[ForceInline]
float capsule_mask(float2 p, float2 begin, float2 end, float radius, float softness) {
    let axis = end - begin;
    let len2 = max(dot(axis, axis), 0.000001f);
    let t = saturate(dot(p - begin, axis) / len2);
    let closest = begin + axis * t;
    return 1.0f - smoothstep(radius - softness, radius + softness, length(p - closest));
}

[ForceInline]
float oriented_box_mask(float2 p, float2 center, float2 half_extent, float rotation, float softness) {
    let c = cos(rotation);
    let s = sin(rotation);
    let rel = p - center;
    let local = float2(rel.x * c + rel.y * s, -rel.x * s + rel.y * c);
    return box_mask(local, 0.0f, half_extent, softness);
}

[ForceInline]
float2 polygon_vertex(GameOitEmitShape shape, uint index) {
    let packed = shape.polygon_vertices[index / 2u];
    return (index % 2u) == 0u ? packed.xy : packed.zw;
}

[ForceInline]
float segment_distance(float2 p, float2 a, float2 b) {
    let axis = b - a;
    let len2 = max(dot(axis, axis), 0.000001f);
    let t = saturate(dot(p - a, axis) / len2);
    return length(p - (a + axis * t));
}

[ForceInline]
float polygon_mask(float2 p, GameOitEmitShape shape, float softness) {
    let count = min(uint(max(shape.polygon_vertex_count, 0)), MAX_POLYGON_VERTICES);
    if(count < 3u) {
        return box_mask(p, shape.center, shape.half_extent, softness);
    }

    var inside = false;
    var min_dist = 100000.0f;
    var previous = polygon_vertex(shape, count - 1u);
    for(uint i = 0u; i < count; ++i) {
        let current = polygon_vertex(shape, i);
        let edge_crosses = (current.y > p.y) != (previous.y > p.y);
        let edge_dy = previous.y - current.y;
        let safe_edge_dy = abs(edge_dy) < 0.000001f
            ? (edge_dy < 0.0f ? -0.000001f : 0.000001f)
            : edge_dy;
        if(edge_crosses &&
            (p.x < (previous.x - current.x) * (p.y - current.y) / safe_edge_dy + current.x)) {
            inside = !inside;
        }
        min_dist = min(min_dist, segment_distance(p, previous, current));
        previous = current;
    }

    return inside ? 1.0f : 1.0f - smoothstep(0.0f, softness, min_dist);
}

[ForceInline]
float shape_mask(float2 p, GameOitEmitShape shape, float softness) {
    if(shape.shape_kind == 0) {
        return circle_mask(p, shape.center, shape.radius, softness);
    }
    if(shape.shape_kind == 1) {
        return capsule_mask(p, shape.axis_begin, shape.axis_end, shape.radius, softness);
    }
    if(shape.shape_kind == 2) {
        return oriented_box_mask(p, shape.center, shape.half_extent, shape.radius, softness);
    }
    if(shape.shape_kind == 3) {
        return polygon_mask(p, shape, softness);
    }
    return box_mask(p, shape.center, shape.half_extent, softness);
}

[ForceInline]
bool shape_bounds_reject(float2 p, GameOitEmitShape shape, float softness) {
    if(shape.shape_kind == 2) {
        let radius_bound = length(shape.half_extent) + softness;
        return any(abs(p - shape.center) > radius_bound);
    }

    return any(abs(p - shape.center) > shape.half_extent + softness);
}

[ForceInline]
float debug_line_mask(float2 p, float2 src, float2 dst, float stroke, float softness) {
    let axis = dst - src;
    let len2 = max(dot(axis, axis), 0.000001f);
    let t = saturate(dot(p - src, axis) / len2);
    let dist = length(p - (src + axis * t));
    let radius = max(stroke * 0.5f, softness);
    return 1.0f - smoothstep(radius - softness, radius + softness, dist);
}

[ForceInline]
float debug_ring_mask(float2 p, GameDebugDrawRing ring, float softness) {
    let radius_mid = max((ring.radius.x + ring.radius.y) * 0.5f, 0.0f);
    let radius_half = max(abs(ring.radius.y - ring.radius.x) * 0.5f, softness);
    let dist = abs(length(p - ring.center) - radius_mid);
    return 1.0f - smoothstep(radius_half - softness, radius_half + softness, dist);
}

[ForceInline]
void blend_debug(float mask, float4 color, inout float4 base, inout float4 light) {
    if(mask <= 0.001f) {
        return;
    }

    let opacity = saturate(color.a * mask);
    base = lerp(base, float4(color.rgb, 1.0f), opacity);
    light += float4(max(color.rgb - 1.0f, 0.0f), opacity) * opacity *
        max(params.bloom_hint_strength, 0.0f);
}

[ForceInline]
uint tile_shape_word(GameOitTileMask mask, uint word_index) {
    if(word_index == 0u) {
        return mask.shape_words.x;
    }
    if(word_index == 1u) {
        return mask.shape_words.y;
    }
    if(word_index == 2u) {
        return mask.shape_words.z;
    }
    return mask.shape_words.w;
}

[ForceInline]
void emit_shape(
    uint shape_index,
    float2 centered,
    uint node_index,
    inout float4 base,
    inout float4 light,
    inout float depth,
    inout uint head,
    inout uint emitted) {
    let shape = input_shapes[shape_index];
    if(shape.enabled == 0) {
        return;
    }

    let softness = GAME_OIT_SHAPE_SOFTNESS;
    if(shape_bounds_reject(centered, shape, softness)) {
        return;
    }

    let mask = shape_mask(centered, shape, softness);
    if(mask <= 0.001f) {
        return;
    }

    let opacity = saturate(shape.opacity * mask);
    let opaque_weight = opacity * 0.32f;
    base = lerp(base, float4(shape.color_base.rgb, 1.0f), opaque_weight);
    light += float4(shape.color_light.rgb, max(shape.color_light.a, opacity)) * opacity *
        max(params.bloom_hint_strength, 0.0f);
    depth = min(depth, shape.depth);

    if(params.transparent_overlap_enabled != 0 && emitted < NODES_PER_PIXEL) {
        output_nodes[node_index + emitted] = GameOitNode(
            float4(shape.color_base.rgb, opacity),
            float4(shape.color_light.rgb, opacity * max(shape.color_light.a, 0.25f)),
            max(shape.depth - 0.045f, 0.0f),
            head);
        head = node_index + emitted;
        emitted += 1u;
    }
}

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID: SV_DispatchThreadID) {
    let pos = dispatchThreadID.xy;

    uint width;
    uint height;
    output_head_indices.GetDimensions(width, height);
    if(pos.x >= width || pos.y >= height) {
        return;
    }

    let pixel_index = pos.y * width + pos.x;
    let node_index = pixel_index * NODES_PER_PIXEL;
    let size = float2(float(width), float(height));
    let uv = (float2(pos) + 0.5f) / max(size, 1.0f);
    let centered = (uv - 0.5f) * float2(size.x / max(size.y, 1.0f), 1.0f);

    var base = input_color_base.Load(uint3(pos, 0));
    base.rgb = max(base.rgb, float3(0.012f, 0.014f, 0.018f));
    base.a = max(base.a, 1.0f);

    var head = INVALID_NODE;
    var depth = 1.0f;
    var light = float4(0.0f);
    var emitted = 0u;
    let shape_count = min(uint(max(params.shape_count, 0)), MAX_SHAPES);

    if(params.use_tile_mask != 0 && params.tile_grid_width > 0 && params.tile_grid_height > 0) {
        let tile_x = pos.x / GROUP_SIZE;
        let tile_y = pos.y / GROUP_SIZE;
        let tile_index = tile_y * uint(params.tile_grid_width) + tile_x;
        let tile_mask = input_tile_masks[tile_index];
        for(uint word_index = 0u; word_index < TILE_MASK_WORD_COUNT; ++word_index) {
            let word = tile_shape_word(tile_mask, word_index);
            for(uint bit_index = 0u; bit_index < 32u; ++bit_index) {
                let shape_index = word_index * 32u + bit_index;
                if(shape_index >= shape_count) {
                    break;
                }
                if((word & (1u << bit_index)) != 0u) {
                    emit_shape(shape_index, centered, node_index, base, light, depth, head, emitted);
                }
            }
        }
    } else {
        for(uint i = 0u; i < shape_count; ++i) {
            emit_shape(i, centered, node_index, base, light, depth, head, emitted);
        }
    }

    if(shape_count == 0u) {
        let fallback = max(
            box_mask(centered, float2(-0.20f, -0.08f), float2(0.23f, 0.07f), 0.012f),
            circle_mask(centered, float2(0.03f, 0.02f), 0.105f, 0.012f));
        base = lerp(base, float4(0.10f, 0.36f, 0.55f, 1.0f), fallback * 0.72f);
        light = float4(fallback * 1.35f, fallback * 0.58f, fallback * 1.95f, fallback * 0.72f);
        depth = lerp(1.0f, 0.48f, fallback);
    }

    let debug_softness = 0.0016f;
    let debug_line_count = min(uint(max(params.debug_line_count, 0)), MAX_DEBUG_LINES);
    for(uint i = 0u; i < debug_line_count; ++i) {
        let line = input_debug_lines[i];
        let mask = debug_line_mask(centered, line.src, line.dst, line.stroke, debug_softness);
        blend_debug(mask, line.color, base, light);
        depth = lerp(depth, min(depth, line.depth), saturate(mask));
    }

    let debug_ring_count = min(uint(max(params.debug_ring_count, 0)), MAX_DEBUG_RINGS);
    for(uint i = 0u; i < debug_ring_count; ++i) {
        let ring = input_debug_rings[i];
        let mask = debug_ring_mask(centered, ring, debug_softness);
        blend_debug(mask, ring.color, base, light);
        depth = lerp(depth, min(depth, ring.depth), saturate(mask));
    }

    let debug_polyline_count = min(uint(max(params.debug_closed_polyline_count, 0)), MAX_DEBUG_CLOSED_POLYLINES);
    let debug_vertex_count = min(uint(max(params.debug_vertex_count, 0)), MAX_DEBUG_POLYLINE_VERTICES);
    for(uint i = 0u; i < debug_polyline_count; ++i) {
        let polyline = input_debug_closed_polylines[i];
        if(polyline.vertex_count < 3u || polyline.vertex_offset >= debug_vertex_count) {
            continue;
        }

        let count = min(polyline.vertex_count, debug_vertex_count - polyline.vertex_offset);
        var mask = 0.0f;
        var previous = input_debug_vertices[polyline.vertex_offset + count - 1u].position;
        for(uint vertex_index = 0u; vertex_index < count; ++vertex_index) {
            let current = input_debug_vertices[polyline.vertex_offset + vertex_index].position;
            mask = max(mask, debug_line_mask(centered, previous, current, polyline.stroke, debug_softness));
            previous = current;
        }
        blend_debug(mask, polyline.color, base, light);
        depth = lerp(depth, min(depth, polyline.depth), saturate(mask));
    }

    output_depth.Store(pos, depth);
    output_color_base.Store(pos, base);
    output_color_light.Store(pos, light);
    output_head_indices.Store(pos, head);
}
