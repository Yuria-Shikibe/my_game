import post_process_lib;

static const uint MAX_SSAO_SAMPLES = 64u;

struct GameSsaoSample {
    float2 offset;
    float weight;
    float radius;
};

struct GameSsaoParams {
    GameSsaoSample samples[MAX_SSAO_SAMPLES];
    int sample_count;
    int enabled;
    float strength;
    float reserved0;
    float scale;
    float radius;
    float depth_threshold;
    float reserved1;
};

[[vk::binding(0, 0)]]
restrict RWTexture2D<float4> output_occlusion;

[[vk::binding(1, 0)]]
Sampler2D depth_input;

[[vk::binding(2, 0)]]
Sampler2D light_input;

[[vk::binding(3, 0)]]
ConstantBuffer<GameSsaoParams> params;

[ForceInline]
float luminance(float4 color) {
    return pp::luma(color.rgb) * color.a;
}

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID: SV_DispatchThreadID) {
    let pos = dispatchThreadID.xy;
    uint width;
    uint height;
    output_occlusion.GetDimensions(width, height);
    if(pos.x >= width || pos.y >= height) {
        return;
    }

    let size = float2(float(width), float(height));
    if(params.enabled == 0 || params.strength <= 0.0f) {
        output_occlusion.Store(pos, float4(1.0f));
        return;
    }

    let uv = (float2(pos) + 0.5f) / size;

    let depth = depth_input.SampleLevel(uv, 0).r;
    if(depth >= 1.0f) {
        output_occlusion.Store(pos, float4(1.0f));
        return;
    }

    var occlusion = 0.0f;
    let sample_count = min(params.sample_count, int(MAX_SSAO_SAMPLES));
    for(int i = 0; i < sample_count; ++i) {
        let sample = params.samples[i];
        let sample_uv = uv + sample.offset * params.radius * params.scale;
        let sample_depth = depth_input.SampleLevel(sample_uv, 0).r;
        if(sample_depth >= depth) {
            continue;
        }

        let sample_light = luminance(light_input.SampleLevel(sample_uv, 0));
        let depth_delta = max(abs(depth - sample_depth), 0.0001f);
        let range_check = smoothstep(params.depth_threshold, 1.0f, params.radius / depth_delta);
        occlusion += max(range_check * sample.weight - sample_light, 0.0f);
    }

    let divisor = max(float(sample_count), 1.0f);
    let raw_occlusion = 1.0f - saturate(occlusion / divisor);
    output_occlusion.Store(pos, float4(lerp(1.0f, raw_occlusion, saturate(params.strength))));
}
