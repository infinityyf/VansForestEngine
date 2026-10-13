#ifndef CLOTH_COVERAGE_INCLUDED
#define CLOTH_COVERAGE_INCLUDED
#include "../BRDF/ClothData.glsl"

// 主视图与阴影共享同一覆盖合同；tau 不参与几何裁剪。
bool ClothSurfaceVisible(ClothMaterialPayload cloth, bool frontFacing, float coverage)
{
    return (cloth.flags.y != 0u || frontFacing) &&
        (cloth.flags.x == 0u || coverage >= cloth.surface.w);
}
#endif
