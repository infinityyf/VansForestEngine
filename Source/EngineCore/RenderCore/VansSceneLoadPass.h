#pragma once

// LoadSceneObjects 四阶段装配顺序不可改变。
enum class VansSceneLoadPass
{
    Pass1_ComponentInstantiation,
    Pass2_VehicleReference,
    Pass3_TransformParent,
    Pass4_AnimationRagdoll
};