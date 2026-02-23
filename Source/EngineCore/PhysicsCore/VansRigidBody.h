#pragma once
#include <PxPhysicsAPI.h>
#include <vector>
#include <memory>

using namespace physx;

namespace VansEngine
{
	// Forward declaration
	class VansCollider;

	// RigidBody Wrapper
	class VansRigidBody
	{
	public:
		VansRigidBody(bool isDynamic = true);
		~VansRigidBody();
		
		// Transform
		void SetPosition(const PxVec3& position);
		void SetRotation(const PxQuat& rotation);
		void SetTransform(const PxTransform& transform);
		
		PxVec3 GetPosition() const;
		PxQuat GetRotation() const;
		PxTransform GetTransform() const;
		
		// Physics Properties (Dynamic only)
		void SetMass(float mass);
		void SetLinearVelocity(const PxVec3& velocity);
		void SetAngularVelocity(const PxVec3& velocity);
		void SetLinearDamping(float damping);
		void SetAngularDamping(float damping);
		void SetMaxLinearVelocity(float maxVelocity);
		void SetMaxAngularVelocity(float maxVelocity);
		
		float GetMass() const;
		PxVec3 GetLinearVelocity() const;
		PxVec3 GetAngularVelocity() const;
		float GetLinearDamping() const;
		float GetAngularDamping() const;
		
		// Forces (Dynamic only)
		void AddForce(const PxVec3& force, PxForceMode::Enum mode = PxForceMode::eFORCE);
		void AddTorque(const PxVec3& torque, PxForceMode::Enum mode = PxForceMode::eFORCE);
		void ClearForces();
		
		// Constraints
		void SetKinematic(bool isKinematic);
		void SetGravityEnabled(bool enabled);
		void LockLinearAxis(bool lockX, bool lockY, bool lockZ);
		void LockAngularAxis(bool lockX, bool lockY, bool lockZ);
		
		bool IsKinematic() const;
		bool IsGravityEnabled() const;
		
		// Collider Management
		void AddCollider(std::shared_ptr<VansCollider> collider);
		void RemoveCollider(std::shared_ptr<VansCollider> collider);
		void ClearColliders();
		const std::vector<std::shared_ptr<VansCollider>>& GetColliders() const { return m_Colliders; }
		
		// Scene Management
		void AddToScene();
		void RemoveFromScene();
		bool IsInScene() const { return m_IsInScene; }
		
		// Type
		bool IsDynamic() const { return m_IsDynamic; }
		bool IsStatic() const { return !m_IsDynamic; }
		
		// Internal
		PxRigidActor* GetActor() { return m_Actor; }
		PxRigidDynamic* GetDynamicActor();
		PxRigidStatic* GetStaticActor();
		
	private:
		void CreateActor();
		void UpdateMassAndInertia();
		
		PxRigidActor* m_Actor = nullptr;
		bool m_IsDynamic;
		bool m_IsInScene = false;
		
		std::vector<std::shared_ptr<VansCollider>> m_Colliders;
		
		// Cached properties
		float m_Mass = 1.0f;
		bool m_IsKinematic = false;
	};
}
