#include "VansPhysicsEventCallback.h"
#include "VansPhysicsNode.h"
#include "../Util/VansLog.h"

namespace VansEngine
{
	VansPhysicsEventCallback::VansPhysicsEventCallback(VansPhysicsEventQueue* eventQueue)
		: m_EventQueue(eventQueue)
	{
	}

	// ===========================================================================
	// onContact — 碰撞接触事件，由 PhysX 模拟线程调用
	// ===========================================================================

	void VansPhysicsEventCallback::onContact(
		const PxContactPairHeader& pairHeader,
		const PxContactPair* pairs, PxU32 nbPairs)
	{
		VANS_LOG("[PhysX Callback] onContact called, nbPairs=" << nbPairs);

		if (!m_EventQueue)
		{
			VANS_LOG_WARN("[PhysX Callback] onContact: m_EventQueue is null!");
			return;
		}

		// 跳过已被删除的 actor
		if (pairHeader.flags & PxContactPairHeaderFlag::eREMOVED_ACTOR_0 ||
			pairHeader.flags & PxContactPairHeaderFlag::eREMOVED_ACTOR_1)
		{
			VANS_LOG_WARN("[PhysX Callback] onContact: skipping removed actor pair");
			return;
		}

		for (PxU32 i = 0; i < nbPairs; ++i)
		{
			const PxContactPair& cp = pairs[i];

			VansPhysicsNode* nodeA = GetPhysicsNode(pairHeader.actors[0]);
			VansPhysicsNode* nodeB = GetPhysicsNode(pairHeader.actors[1]);
			VANS_LOG("[PhysX Callback] onContact pair " << i
			         << ": actorA=" << pairHeader.actors[0]
			         << " nodeA=" << nodeA
			         << (nodeA ? (" name='" + nodeA->GetName() + "'") : "")
			         << " actorB=" << pairHeader.actors[1]
			         << " nodeB=" << nodeB
			         << (nodeB ? (" name='" + nodeB->GetName() + "'") : "")
			         << " events=0x" << std::hex << cp.events.operator unsigned int() << std::dec);
			if (!nodeA || !nodeB) continue;

			PhysicsEventData event;
			event.transformID_A = nodeA->GetTransformID();
			event.transformID_B = nodeB->GetTransformID();
			event.nameA = nodeA->GetName();
			event.nameB = nodeB->GetName();

			// 提取接触点
			if (cp.contactCount > 0)
			{
				PxContactPairPoint contactPoints[1];
				PxU32 nbContacts = cp.extractContacts(contactPoints, 1);
				if (nbContacts > 0)
				{
					event.contactPoint  = glm::vec3(contactPoints[0].position.x,
					                                contactPoints[0].position.y,
					                                contactPoints[0].position.z);
					event.contactNormal = glm::vec3(contactPoints[0].normal.x,
					                                contactPoints[0].normal.y,
					                                contactPoints[0].normal.z);
					event.impulse       = contactPoints[0].impulse.magnitude();
				}
			}

			if (cp.events & PxPairFlag::eNOTIFY_TOUCH_FOUND)
			{
				event.type = PhysicsEventType::CollisionEnter;
				m_EventQueue->PushEvent(event);
				VANS_LOG("[PhysX Callback] CollisionEnter: '" << event.nameA << "' <-> '" << event.nameB << "'");
			}
			if (cp.events & PxPairFlag::eNOTIFY_TOUCH_LOST)
			{
				event.type = PhysicsEventType::CollisionExit;
				m_EventQueue->PushEvent(event);
				VANS_LOG("[PhysX Callback] CollisionExit: '" << event.nameA << "' <-> '" << event.nameB << "'");
			}
		}
	}

	// ===========================================================================
	// onTrigger — 触发器事件，由 PhysX 模拟线程调用
	// ===========================================================================

	void VansPhysicsEventCallback::onTrigger(PxTriggerPair* pairs, PxU32 count)
	{
		VANS_LOG("[PhysX Callback] onTrigger called, count=" << count);

		if (!m_EventQueue)
		{
			VANS_LOG_WARN("[PhysX Callback] onTrigger: m_EventQueue is null!");
			return;
		}

		for (PxU32 i = 0; i < count; ++i)
		{
			const PxTriggerPair& tp = pairs[i];

			// 跳过已被移除的 Shape
			if (tp.flags & (PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER |
			                PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
			{
				VANS_LOG_WARN("[PhysX Callback] onTrigger: skipping removed shape pair");
				continue;
			}

			VansPhysicsNode* triggerNode = GetPhysicsNode(tp.triggerActor);
			VansPhysicsNode* otherNode   = GetPhysicsNode(tp.otherActor);
			VANS_LOG("[PhysX Callback] onTrigger pair " << i
			         << ": triggerActor=" << tp.triggerActor
			         << " triggerNode=" << triggerNode
			         << (triggerNode ? (" name='" + triggerNode->GetName() + "'") : "")
			         << " otherActor=" << tp.otherActor
			         << " otherNode=" << otherNode
			         << (otherNode ? (" name='" + otherNode->GetName() + "'") : "")
			         << " status=0x" << std::hex << static_cast<PxU32>(tp.status) << std::dec);
			if (!triggerNode || !otherNode) continue;

			PhysicsEventData event;
			event.transformID_A = triggerNode->GetTransformID();
			event.transformID_B = otherNode->GetTransformID();
			event.nameA = triggerNode->GetName();
			event.nameB = otherNode->GetName();

			if (tp.status == PxPairFlag::eNOTIFY_TOUCH_FOUND)
			{
				event.type = PhysicsEventType::TriggerEnter;
				m_EventQueue->PushEvent(event);
				VANS_LOG("[PhysX Callback] TriggerEnter: trigger='" << event.nameA << "' other='" << event.nameB << "'");
			}
			else if (tp.status == PxPairFlag::eNOTIFY_TOUCH_LOST)
			{
				event.type = PhysicsEventType::TriggerExit;
				m_EventQueue->PushEvent(event);
				VANS_LOG("[PhysX Callback] TriggerExit: trigger='" << event.nameA << "' other='" << event.nameB << "'");
			}
		}
	}

	// ===========================================================================
	// GetPhysicsNode — 从 PxActor::userData 获取 VansPhysicsNode*
	// ===========================================================================

	VansPhysicsNode* VansPhysicsEventCallback::GetPhysicsNode(PxActor* actor)
	{
		if (!actor) return nullptr;
		return static_cast<VansPhysicsNode*>(actor->userData);
	}
}
