#include "VansCollider.h"
#include "VansPhysics.h"

namespace VansEngine
{
	// ============================================================================
	// VansCollider
	// ============================================================================
	VansCollider::VansCollider(VansColliderType type)
		: m_Type(type)
	{
		auto& physics = VansPhysicsSystem::GetInstance();
		
		// Create material with default values
		m_Material = physics.GetPhysics()->createMaterial(0.5f, 0.5f, 0.6f);
		
		CreateShape();
	}

	VansCollider::~VansCollider()
	{
		if (m_Shape) m_Shape->release();
		if (m_Material) m_Material->release();
		if (m_TriangleMesh) m_TriangleMesh->release();
		if (m_ConvexMesh) m_ConvexMesh->release();
	}

	void VansCollider::CreateShape()
	{
		auto& physics = VansPhysicsSystem::GetInstance();
		PxGeometry* geometry = nullptr;
		PxBoxGeometry boxGeom;
		PxSphereGeometry sphereGeom;
		PxCapsuleGeometry capsuleGeom;
		PxTriangleMeshGeometry meshGeom;
		PxConvexMeshGeometry convexGeom;

		switch (m_Type)
		{
		case VansColliderType::Box:
			boxGeom = PxBoxGeometry(m_BoxHalfExtents);
			geometry = &boxGeom;
			break;
		case VansColliderType::Sphere:
			sphereGeom = PxSphereGeometry(m_SphereRadius);
			geometry = &sphereGeom;
			break;
		case VansColliderType::Capsule:
			capsuleGeom = PxCapsuleGeometry(m_CapsuleRadius, m_CapsuleHalfHeight);
			geometry = &capsuleGeom;
			break;
		case VansColliderType::Mesh:
			if (m_TriangleMesh)
			{
				meshGeom = PxTriangleMeshGeometry(m_TriangleMesh);
				geometry = &meshGeom;
			}
			break;
		case VansColliderType::ConvexMesh:
			if (m_ConvexMesh)
			{
				convexGeom = PxConvexMeshGeometry(m_ConvexMesh);
				geometry = &convexGeom;
			}
			break;
		}

		if (geometry)
		{
			PxTransform localTransform(m_LocalPosition, m_LocalRotation);
			m_Shape = physics.GetPhysics()->createShape(*geometry, *m_Material, false, PxShapeFlag::eVISUALIZATION | PxShapeFlag::eSCENE_QUERY_SHAPE | PxShapeFlag::eSIMULATION_SHAPE);
			m_Shape->setLocalPose(localTransform);
			
			if (m_IsTrigger)
			{
				m_Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
				m_Shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
			}
		}
	}

	void VansCollider::UpdateShape()
	{
		if (m_Shape)
		{
			m_Shape->release();
			m_Shape = nullptr;
		}
		CreateShape();
	}

	void VansCollider::SetBox(const PxVec3& halfExtents)
	{
		m_Type = VansColliderType::Box;
		m_BoxHalfExtents = halfExtents;
		UpdateShape();
	}

	void VansCollider::SetSphere(float radius)
	{
		m_Type = VansColliderType::Sphere;
		m_SphereRadius = radius;
		UpdateShape();
	}

	void VansCollider::SetCapsule(float radius, float halfHeight)
	{
		m_Type = VansColliderType::Capsule;
		m_CapsuleRadius = radius;
		m_CapsuleHalfHeight = halfHeight;
		UpdateShape();
	}

	void VansCollider::SetMesh(const std::vector<PxVec3>& vertices, const std::vector<uint32_t>& indices)
	{
		auto& physics = VansPhysicsSystem::GetInstance();
		
		// Clean up old mesh
		if (m_TriangleMesh) m_TriangleMesh->release();
		
		// Create mesh description
		PxTriangleMeshDesc meshDesc;
		meshDesc.points.count = static_cast<PxU32>(vertices.size());
		meshDesc.points.stride = sizeof(PxVec3);
		meshDesc.points.data = vertices.data();
		
		meshDesc.triangles.count = static_cast<PxU32>(indices.size() / 3);
		meshDesc.triangles.stride = 3 * sizeof(uint32_t);
		meshDesc.triangles.data = indices.data();
		
		// Cook the mesh using PhysX 5 API
		m_TriangleMesh = physics.CookTriangleMesh(meshDesc);
		
		m_Type = VansColliderType::Mesh;
		UpdateShape();
	}

	void VansCollider::SetConvexMesh(const std::vector<PxVec3>& vertices)
	{
		auto& physics = VansPhysicsSystem::GetInstance();
		
		// Clean up old mesh
		if (m_ConvexMesh) m_ConvexMesh->release();
		
		// Create convex mesh description
		PxConvexMeshDesc convexDesc;
		convexDesc.points.count = static_cast<PxU32>(vertices.size());
		convexDesc.points.stride = sizeof(PxVec3);
		convexDesc.points.data = vertices.data();
		convexDesc.flags = PxConvexFlag::eCOMPUTE_CONVEX;
		
		// Cook the mesh using PhysX 5 API
		m_ConvexMesh = physics.CookConvexMesh(convexDesc);
		
		m_Type = VansColliderType::ConvexMesh;
		UpdateShape();
	}

	void VansCollider::SetStaticFriction(float friction)
	{
		if (m_Material) m_Material->setStaticFriction(friction);
	}

	void VansCollider::SetDynamicFriction(float friction)
	{
		if (m_Material) m_Material->setDynamicFriction(friction);
	}

	void VansCollider::SetRestitution(float restitution)
	{
		if (m_Material) m_Material->setRestitution(restitution);
	}

	float VansCollider::GetStaticFriction() const
	{
		return m_Material ? m_Material->getStaticFriction() : 0.0f;
	}

	float VansCollider::GetDynamicFriction() const
	{
		return m_Material ? m_Material->getDynamicFriction() : 0.0f;
	}

	float VansCollider::GetRestitution() const
	{
		return m_Material ? m_Material->getRestitution() : 0.0f;
	}

	void VansCollider::SetIsTrigger(bool isTrigger)
	{
		m_IsTrigger = isTrigger;
		if (m_Shape)
		{
			if (isTrigger)
			{
				m_Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
				m_Shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
			}
			else
			{
				m_Shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, false);
				m_Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
			}
		}
	}

	void VansCollider::SetLocalPosition(const PxVec3& pos)
	{
		m_LocalPosition = pos;
		if (m_Shape)
		{
			PxTransform localTransform(m_LocalPosition, m_LocalRotation);
			m_Shape->setLocalPose(localTransform);
		}
	}

	void VansCollider::SetLocalRotation(const PxQuat& rot)
	{
		m_LocalRotation = rot;
		if (m_Shape)
		{
			PxTransform localTransform(m_LocalPosition, m_LocalRotation);
			m_Shape->setLocalPose(localTransform);
		}
	}
}
