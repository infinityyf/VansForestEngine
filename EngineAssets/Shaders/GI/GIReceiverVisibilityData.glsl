#ifndef GI_RECEIVER_VISIBILITY_DATA_GLSL
#define GI_RECEIVER_VISIBILITY_DATA_GLSL
const uint GI_RECEIVER_RAY_BUDGET = 65536u;
const uint GI_RECEIVER_FAIR_ANCHORS = 1024u;
const uint GI_RECEIVER_FAIR_RAYS = GI_RECEIVER_FAIR_ANCHORS * 16u;
const uint GI_RECEIVER_REFRESH_FRAMES = 32u;
// 112 字节。16 个身份容纳植被前后半球的候选并集；普通表面最多 8 个。
// surface 始终保留真实射线的起点，复用不会更新检查时间或起点。
struct GIReceiverVisibilityRecord
{
    vec4 surface;
    uvec4 metadata; // region, traceFrame, knownMask, visibleMask
    uvec4 probes[4];
    vec4 anchor; // x：锚点建立时的像素世界尺寸；y：地形接收面；z：滚动身份位模式；w 保留。
};
uint GI_ReceiverProbe(GIReceiverVisibilityRecord r, uint i) { return r.probes[i >> 2u][i & 3u]; }
bool GI_ReceiverSurfaceMatches(vec4 previous, vec4 position, vec3 normal, float material, float footprint)
{
    float scale = max(max(abs(position.x), abs(position.y)), abs(position.z));
    // 对遮挡历史额外限制切向漂移，不能让深度容差成为沿墙滑动的无限复用半径。
    float positionTolerance = max(footprint * 2.5, max(1e-5, scale * 4.7683716e-7));
    return length(previous.xyz - position.xyz) <= positionTolerance &&
        SSGI_HistorySurfaceMatches(previous, position, normal, normal, material, footprint);
}
#endif
