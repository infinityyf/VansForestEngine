#include "VansRigidBody.h"
#include "VansCollider.h"
#include "VansPhysics.h"

namespace VansEngine
{
	// ============================================================================
	// VansRigidBody
	// ============================================================================
	VansRigidBody::VansRigidBody(bool isDynamic)
		: m_IsDynamic(isDynamic)
	{
		CreateActor();
	}

	VansRigidBody::~VansRigidBody()
	{
		RemoveFromScene();
		if (m_Actor) m_Actor->release();
	}

	void VansRigidBody::CreateActor()
	{
		auto& physics = VansPhysicsSystem::GetInstance();
		
		PxTransform transform(PxVec3(0.0f, 0.0f, 0.0f));
		
		if (m_IsDynamic)
		{
			m_Actor = physics.GetPhysics()->createRigidDynamic(transform);
		}
		else
		{
			m_Actor = physics.GetPhysics()->createRigidStatic(transform);
		}
	}

	void VansRigidBody::SetPosition(const PxVec3& position)
	{
		if (m_Actor)
		{
			PxTransform transform = m_Actor->getGlobalPose();
			transform.p = position;
			m_Actor->setGlobalPose(transform);
		}
	}

	void VansRigidBody::SetRotation(const PxQuat& rotation)
	{
		if (m_Actor)
		{
			PxTransform transform = m_Actor->getGlobalPose();
			transform.q = rotation;
			m_Actor->setGlobalPose(transform);
		}
	}

	void VansRigidBody::SetTransform(const PxTransform& transform)
	{
		if (m_Actor)
		{
			m_Actor->setGlobalPose(transform);
		}
	}

	PxVec3 VansRigidBody::GetPosition() const
	{
		return m_Actor ? m_Actor->getGlobalPose().p : PxVec3(0.0f);
	}

	PxQuat VansRigidBody::GetRotation() const
	{
		return m_Actor ? m_Actor->getGlobalPose().q : PxQuat(0.0f, 0.0f, 0.0f, 1.0f);
	}

	PxTransform VansRigidBody::GetTransform() const
	{
		return m_Actor ? m_Actor->getGlobalPose() : PxTransform(PxIdentity);
	}

	void VansRigidBody::SetMass(float mass)
	{
		m_Mass = mass;
		if (PxRigidDynamic* dynamic = GetDynamicActor())
		{
			PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, mass);
		}
	}

	void VansRigidBody::SetLinearVelocity(const PxVec3& velocity)
	{
		if (PxRigidDynamic* dynamic = GetDynamicActor())
		{
			dynamic->setLinearVelocity(velocity);
		}
	}

	void VansRigidBody::SetAngularVelocity(const PxVec3& velocity)
	{
		if (PxRigidDynamic* dynamic = GetDynamicActor())
		{
			dynamic->setAngularVelocity(velocity);
		}
	}

	void VansRigidBody::SetLinearDamping(float damping)
	{
		if (PxRigidDynamic* dynamic = GetDynamicActor())
		{
			dynamic->setLinearDamping(damping);
		}
	}

	void VansRigidBody::SetAngularDamping(float damping)
	{
		if (PxRigidDynamic* dynamic = GetDynamicActor())
		{
			dynamic->setAngularDamping(damping);
		}
	}

	void VansRigidBody::SetMaxLinearVelocity(float maxVelocity)
	{
		if (PxRigidDynamic* dynamic = GetDynamicActor())
		{
			dynamic->setMaxLinearVelocity(maxVelocity);
		}
	}

	void VansRigidBody::SetMaxAngularVelocity(float maxVelocity)
	{
		if (PxRigidDynamic* dynamic = GetDynamicActor())
		{
			dynamic->setMaxAngularVelocity(maxVelocity);
		}
	}

	float VansRigidBody::GetMass() const
	{
		if (PxRigidDynamic* dynamic = const_cast<VansRigidBody*>(this)->GetDynamicActor())
		{
			return dynamic->getMass();
		}
		return 0.0f;
	}

	PxVec3 VansRigidBody::GetLinearVelocity() const
	{
		if (PxRigidDynamic* dynamic = const_cast<VansRigidBody*>(this)->GetDynamicActor())
		{
			return dynamic->getLinearVelocity();
		}
		return PxVec3(0.0f);
	}

	PxVec3 VansRigidBody::GetAngularVelocity() const
	{
		if (PxRigidDynamic* dynamic = const_cast<VansRigidBody*>(this)->GetDynamicActor())
		{
			return dynamic->getAngularVelocity();
		}
		return PxVec3(0.0f);
	}

	float VansRigidBody::GetLinearDamping() const
	{
		if (PxRigidDynamic* dynamic = const_cast<VansRigidBody*>(this)->GetDynamicActor())
		{
			return dynamic->getLinearDamping();
		}
		return 0.0f;
	}

	float VansRigidBody::GetAngularDamping() const
	{
		if (PxRigidDynamic* dynamic = const_cast<VansRigidBody*>(this)->GetDynamicActor())
		{
			return dynamic->getAngularDamping();
		}
		return 0.0f;
	}

	void VansRigidBody::AddForce(const PxVec3& force, PxForceMode::Enum mode)
	{
		if (PxRigidDynamic* dynamic = GetDynamicActor())
		{
			dynamic->addForce(force, mode);
		}
	}

	void VansRigidBody::AddTorque(const PxVec3& torque, PxForceMode::Enum mode)
	{
		if (PxRigidDynamic* dynamic = GetDynamicActor())
		{
			dynamic->addTorque(torque, mode);
		}
	}

	void VansRigidBody::ClearForces()
	{
		if (PxRigidDynamic* dynamic = GetDynamicActor())
		{
			dynamic->clearForce();
			dynamic->clearTorque();
		}
	}

	void VansRigidBody::SetKinematic(bool isKinematic)
	{
		m_IsKinematic = isKinematic;
		if (PxRigidDynamic* dynamic = GetDynamicActor())
		{
			dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, isKinematic);
		}
	}

	void VansRigidBody::SetGravityEnabled(bool enabled)
	{
		if (m_Actor)
		{
			m_Actor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !enabled);
		}
	}

	void VansRigidBody::LockLinearAxis(bool lockX, bool lockY, bool lockZ)
	{
		if (PxRigidDynamic* dynamic = GetDynamicActor())
		{
			dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_LINEAR_X, lockX);
			dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_LINEAR_Y, lockY);
			dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_LINEAR_Z, lockZ);
		}
	}

	void VansRigidBody::LockAngularAxis(bool lockX, bool lockY, bool lockZ)
	{
		if (PxRigidDynamic* dynamic = GetDynamicActor())
		{
			dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_X, lockX);
			dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y, lockY);
			dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z, lockZ);
		}
	}

	bool VansRigidBody::IsKinematic() const
	{
		return m_IsKinematic;
	}

	bool VansRigidBody::IsGravityEnabled() const
	{
		if (m_Actor)
		{
			return !(m_Actor->getActorFlags() & PxActorFlag::eDISABLE_GRAVITY);
		}
		return true;
	}

	void VansRigidBody::AddCollider(std::shared_ptr<VansCollider> collider)
	{
		if (!collider || !m_Actor) return;
		
		m_Colliders.push_back(collider);
		
		if (collider->GetShape())
		{
			m_Actor->attachShape(*collider->GetShape());
		}
		
		// Update mass if dynamic
		UpdateMassAndInertia();
	}

	void VansRigidBody::RemoveCollider(std::shared_ptr<VansCollider> collider)
	{
		if (!collider || !m_Actor) return;
		
		auto it = std::find(m_Colliders.begin(), m_Colliders.end(), collider);
		if (it != m_Colliders.end())
		{
			if (collider->GetShape())
			{
				m_Actor->detachShape(*collider->GetShape());
			}
			m_Colliders.erase(it);
			
			UpdateMassAndInertia();
		}
	}

	void VansRigidBody::ClearColliders()
	{
		for (auto& collider : m_Colliders)
		{
			if (collider && collider->GetShape())
			{
				m_Actor->detachShape(*collider->GetShape());
			}
		}
		m_Colliders.clear();
	}

	void VansRigidBody::AddToScene()
	{
		if (m_IsInScene || !m_Actor) return;
		
		auto& physics = VansPhysicsSystem::GetInstance();
		if (physics.GetScene())
		{
			physics.GetScene()->addActor(*m_Actor);
			m_IsInScene = true;
		}
	}

	void VansRigidBody::RemoveFromScene()
	{
		if (!m_IsInScene || !m_Actor) return;
		
		auto& physics = VansPhysicsSystem::GetInstance();
		if (physics.GetScene())
		{
			physics.GetScene()->removeActor(*m_Actor);
			m_IsInScene = false;
		}
	}

	PxRigidDynamic* VansRigidBody::GetDynamicActor()
	{
		if (m_IsDynamic && m_Actor)
		{
			return m_Actor->is<PxRigidDynamic>();
		}
		return nullptr;
	}

	PxRigidStatic* VansRigidBody::GetStaticActor()
	{
		if (!m_IsDynamic && m_Actor)
		{
			return m_Actor->is<PxRigidStatic>();
		}
		return nullptr;
	}

	void VansRigidBody::UpdateMassAndInertia()
	{
		if (PxRigidDynamic* dynamic = GetDynamicActor())
		{
			if (m_Colliders.size() > 0)
			{
				PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, m_Mass);
			}
		}
	}
}
