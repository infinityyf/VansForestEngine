#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <PxPhysicsAPI.h>
#include "VansPhysicsEvents.h"

using namespace physx;

namespace VansEngine
{
	class VansPhysicsNode; // forward declaration

	class VansPhysicsEventCallback : public PxSimulationEventCallback
	{
	public:
		VansPhysicsEventCallback(VansPhysicsEventQueue* eventQueue);

		// ── PxSimulationEventCallback 接口 ─────────────────────────
		void onContact(const PxContactPairHeader& pairHeader,
		               const PxContactPair* pairs, PxU32 nbPairs) override;

		void onTrigger(PxTriggerPair* pairs, PxU32 count) override;

		// 以下为必须实现但不使用的接口
		void onConstraintBreak(PxConstraintInfo* constraints, PxU32 count) override {}
		void onWake(PxActor** actors, PxU32 count) override {}
		void onSleep(PxActor** actors, PxU32 count) override {}
		void onAdvance(const PxRigidBody* const* bodyBuffer,
		               const PxTransform* poseBuffer, const PxU32 count) override {}

	private:
		VansPhysicsEventQueue* m_EventQueue = nullptr;

		// 从 PxActor::userData 获取 VansPhysicsNode 的辅助方法
		static VansPhysicsNode* GetPhysicsNode(PxActor* actor);
	};
}
