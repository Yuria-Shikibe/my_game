import post_process_lib;

static const uint INVALID_NODE = 0xffffffffu;

[[vk::binding(0, 0)]]
restrict RWTexture2D<uint> output_head_indices;

[[vk::binding(1, 0)]]
restrict RWTexture2D<float> output_depth;

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

    output_head_indices.Store(pos, INVALID_NODE);
    output_depth.Store(pos, 1.0f);
}
