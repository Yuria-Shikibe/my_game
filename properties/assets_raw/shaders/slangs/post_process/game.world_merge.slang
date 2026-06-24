import post_process_lib;

[[vk::binding(0, 0)]]
restrict RWTexture2D<float4> output;

[[vk::binding(1, 0)]]
Texture2D input_base;

[[vk::binding(2, 0)]]
Texture2D input_light;

[[vk::binding(3, 0)]]
Texture2D input_light_bloom;

[[vk::binding(4, 0)]]
Texture2D input_ssao;

[ForceInline]
float4 screen(float4 src, float4 dst) {
    let rgb = pp::blend::screen(src.rgb, dst.rgb);
    let alpha = max(src.a, dst.a);
    return float4(rgb, alpha);
}

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID: SV_DispatchThreadID) {
    let pos = dispatchThreadID.xy;
    uint width;
    uint height;
    output.GetDimensions(width, height);
    if(pos.x >= width || pos.y >= height) {
        return;
    }

    let base = input_base.Load(uint3(pos, 0));
    let light = input_light.Load(uint3(pos, 0));
    let bloom = input_light_bloom.Load(uint3(pos, 0));
    let ssao = input_ssao.Load(uint3(pos, 0)).r;

    let occlusion = lerp(1.0f, ssao, smoothstep(0.35f, 1.0f, base.a));
    let shaded_base = float4(base.rgb * occlusion, base.a);
    let emissive = screen(float4(light.rgb * 0.35f, light.a), bloom * 1.05f);

    output.Store(pos, screen(shaded_base, emissive));
}
