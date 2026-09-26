#include "../VansScene.h"

#include "../../AnimationCore/VansAnimationNode.h"
#include "../../PhysicsCore/VansRagdollSystem.h"

namespace VansGraphics
{
	void VansScene::ApplyRagdollPose(VansAnimationNode& node)
	{
		Vans::VansRagdollPoseView animationPose;
		if (!node.GetRagdollPoseView(animationPose))
			return;

		Vans::VansRagdollPose ragdollPose;
		if (VansEngine::VansRagdollSystem::GetInstance().ResolvePose(
			node.GetRagdollKey(), animationPose, ragdollPose))
		{
			node.ApplyRagdollPose(ragdollPose);
		}
	}
}
