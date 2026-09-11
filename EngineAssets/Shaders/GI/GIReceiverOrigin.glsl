#ifndef GI_RECEIVER_ORIGIN_GLSL
#define GI_RECEIVER_ORIGIN_GLSL
// 仅跨过浮点表面误差，不以厘米级固定距离跨越真实狭缝。
float GI_ReceiverOriginEpsilon(vec3 position)
{
    float scale = max(max(abs(position.x), abs(position.y)), abs(position.z));
    return max(0.0001, scale * 2.3841858e-7);
}
#endif
