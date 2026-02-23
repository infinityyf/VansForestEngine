#pragma once
#include <PxPhysicsAPI.h>
#include <vector>

using namespace physx;

namespace VansEngine
{
	// Collider Shape Types
	enum class VansColliderType
	{
		Box,
		Sphere,
		Capsule,
		Mesh,
		ConvexMesh
	};

	// Collider Wrapper
	class VansCollider
	{
	public:
		VansCollider(VansColliderType type);
		~VansCollider();
		
		// Shape Configuration
		void SetBox(const PxVec3& halfExtents);
		void SetSphere(float radius);
		void SetCapsule(float radius, float halfHeight);
		void SetMesh(const std::vector<PxVec3>& vertices, const std::vector<uint32_t>& indices);
		void SetConvexMesh(const std::vector<PxVec3>& vertices);
		
		// Material Properties
		void SetStaticFriction(float friction);
		void SetDynamicFriction(float friction);
		void SetRestitution(float restitution);
		
		float GetStaticFriction() const;
		float GetDynamicFriction() const;
		float GetRestitution() const;
		
		// Trigger
		void SetIsTrigger(bool isTrigger);
		bool IsTrigger() const { return m_IsTrigger; }
		
		// Local Transform (offset from rigid body)
		void SetLocalPosition(const PxVec3& pos);
		void SetLocalRotation(const PxQuat& rot);
		PxVec3 GetLocalPosition() const { return m_LocalPosition; }
		PxQuat GetLocalRotation() const { return m_LocalRotation; }
		
		// Internal
		PxShape* GetShape() { return m_Shape; }
		PxMaterial* GetMaterial() { return m_Material; }
		VansColliderType GetType() const { return m_Type; }
		
	private:
		void CreateShape();
		void UpdateShape();
		
		VansColliderType m_Type;
		PxShape* m_Shape = nullptr;
		PxMaterial* m_Material = nullptr;
		
		// Shape parameters
		PxVec3 m_BoxHalfExtents{ 0.5f, 0.5f, 0.5f };
		float m_SphereRadius = 0.5f;
		float m_CapsuleRadius = 0.5f;
		float m_CapsuleHalfHeight = 1.0f;
		
		// Mesh data
		PxTriangleMesh* m_TriangleMesh = nullptr;
		PxConvexMesh* m_ConvexMesh = nullptr;
		
		// Local transform
		PxVec3 m_LocalPosition{ 0.0f, 0.0f, 0.0f };
		PxQuat m_LocalRotation{ 0.0f, 0.0f, 0.0f, 1.0f };
		
		bool m_IsTrigger = false;
	};
}
