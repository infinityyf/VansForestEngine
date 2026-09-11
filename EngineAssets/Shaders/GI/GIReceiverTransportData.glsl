#ifndef GI_RECEIVER_TRANSPORT_DATA_GLSL
#define GI_RECEIVER_TRANSPORT_DATA_GLSL
const uint GI_RECEIVER_TRANSPORT_JOBS = 2048u;
const uint GI_RECEIVER_TRANSPORT_RAYS = 4u;
// 一条任务只对应一个 Cache texel；唯一写入者替换四条失败回退，避免原子加光。
struct GIReceiverTransportJob
{
    vec4 origin; // xyz 真实表面安全起点；w 为完整追踪上限。
    vec4 directions[4]; // xyz 余弦半球方向；w 为该方向尚未由屏幕命中覆盖的比例。
    vec4 screenRadiance; // rgb 已有屏幕贡献之和 / 4；a 为屏幕命中置信度。
    uvec4 destination; // xy Cache texel；z 为 floatBitsToUint(补算混合权重)。
};
uint GI_ReceiverTransportHash(uint value)
{
    value ^= value >> 16u; value *= 0x7feb352du;
    value ^= value >> 15u; value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}
#endif
