import post_process_lib;

static const uint INVALID_NODE = 0xffffffffu;
static const uint MAX_FRAGMENTS = 15u;

struct GameOitNode {
    float4 color_base;
    float4 color_light;
    float depth;
    uint next;
};

[[vk::binding(0, 0)]]
StructuredBuffer<GameOitNode> oit_nodes;

[[vk::binding(1, 0)]]
Texture2D<uint> oit_head_indices;

[[vk::binding(2, 0)]]
Texture2D input_depth;

[[vk::binding(3, 0)]]
Texture2D input_color_base;

[[vk::binding(4, 0)]]
Texture2D input_color_light;

[[vk::binding(5, 0)]]
restrict RWTexture2D<float4> output_color_base;

[[vk::binding(6, 0)]]
restrict RWTexture2D<float4> output_color_light;

[ForceInline]
float4 premultiplied_blend(float4 top, float4 bottom) {
    return pp::blend::pre_scaled_alpha_blend(top, bottom);
}

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID: SV_DispatchThreadID) {
    let pos = dispatchThreadID.xy;
    uint width;
    uint height;
    output_color_base.GetDimensions(width, height);
    if(pos.x >= width || pos.y >= height) {
        return;
    }

    let depth = input_depth.Load(uint3(pos, 0)).r;

    var base = input_color_base.Load(uint3(pos, 0));
    base = float4(base.rgb * base.a, 1.0f);
    var light = input_color_light.Load(uint3(pos, 0));

    var fragments: GameOitNode[MAX_FRAGMENTS];
    var count: uint = 0;
    var node_index = oit_head_indices.Load(uint3(pos, 0));

    while(node_index != INVALID_NODE && count < MAX_FRAGMENTS) {
        fragments[count] = oit_nodes[node_index];
        node_index = fragments[count].next;
        count += 1;
    }

    for(uint i = 1; i < count; ++i) {
        let inserted = fragments[i];
        var j = i;
        while(j > 0 && inserted.depth > fragments[j - 1].depth) {
            fragments[j] = fragments[j - 1];
            j -= 1;
        }
        fragments[j] = inserted;
    }

    for(uint i = 0; i < count; ++i) {
        let fragment = fragments[i];
        if(fragment.depth >= depth) {
            continue;
        }

        base = premultiplied_blend(fragment.color_base, base);
        light = premultiplied_blend(fragment.color_light, light);
    }

    output_color_base.Store(pos, base);
    output_color_light.Store(pos, light);
}
