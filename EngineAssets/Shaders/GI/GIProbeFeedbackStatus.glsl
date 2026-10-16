#ifndef GI_PROBE_FEEDBACK_STATUS_GLSL
#define GI_PROBE_FEEDBACK_STATUS_GLSL
// 与 GIProbeFeedbackStatus 的原生读回协议一致；沿用 16 B 反馈，不新增 GPU 缓冲。
const uint GI_FEEDBACK_COMPLETE = 1u;
const uint GI_FEEDBACK_HEIGHT_BUDGET = 2u;
const uint GI_FEEDBACK_VOXEL_PAGE = 4u;
const uint GI_FEEDBACK_VOXEL_BUDGET = 8u;
const uint GI_FEEDBACK_VOXEL_COVERAGE = 16u;
const uint GI_FEEDBACK_SCATTER_SHIFT = 4u;
const uint GI_FEEDBACK_PRIMARY_FAILURE_MASK = 30u;
const uint GI_FEEDBACK_SCATTER_FAILURE_MASK = GI_FEEDBACK_PRIMARY_FAILURE_MASK << GI_FEEDBACK_SCATTER_SHIFT;
const uint GI_FEEDBACK_PRIMARY_PUBLISHED = 512u;
float GI_TraceFailureDistance(uint status) { return -2.0-float(status); }
uint GI_TraceFailureStatus(float distance) { return uint(max(-distance-2.0,0.0)); }
// 照明阶段只写辐射有效性，不覆盖主命中。主照明和散射使用反馈的同一组位。
// 最大组合 510 仍可被 float16 精确表示；固定几何射线为 0，有效照明为 1。
float GI_LightingFailureValidity(uint status) { return -float(status); }
uint GI_LightingFailureStatus(float validity)
{ return validity < 0.0 ? uint(-validity) : 0u; }
#endif
