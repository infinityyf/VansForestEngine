#ifndef GI_PROBE_FEEDBACK_STATUS_GLSL
#define GI_PROBE_FEEDBACK_STATUS_GLSL
// 与 GIProbeFeedbackStatus 的原生读回协议一致；沿用 16 B 反馈，不新增 GPU 缓冲。
const uint GI_FEEDBACK_COMPLETE = 1u;
const uint GI_FEEDBACK_HEIGHT_BUDGET = 2u;
const uint GI_FEEDBACK_VOXEL_PAGE = 4u;
const uint GI_FEEDBACK_VOXEL_BUDGET = 8u;
const uint GI_FEEDBACK_VOXEL_COVERAGE = 16u;
float GI_TraceFailureDistance(uint status) { return -2.0-float(status); }
uint GI_TraceFailureStatus(float distance) { return uint(max(-distance-2.0,0.0)); }
#endif
