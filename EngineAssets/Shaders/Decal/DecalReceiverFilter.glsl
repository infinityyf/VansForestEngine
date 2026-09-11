#ifndef VANS_DECAL_RECEIVER_FILTER_GLSL
#define VANS_DECAL_RECEIVER_FILTER_GLSL
#include "../Common/Common.glsl"
// 半精度附件可精确保存 1..256 和 1024；其他材质的 w 继续使用各自约定。
bool DecalReceiverMatches(int materialID, float encodedReceiver, float requiredReceiver)
{
    return requiredReceiver < 0.5 ||
        (materialID == MATERIAL_ID_PBR && encodedReceiver == -requiredReceiver);
}
bool DecalNormalMatches(vec3 surfaceNormal, vec3 projectionNormal, float minimumDot)
{
    return minimumDot < 0.0 || (dot(surfaceNormal, surfaceNormal) > 1e-8 &&
        dot(normalize(surfaceNormal), normalize(projectionNormal)) >= minimumDot);
}
#endif
