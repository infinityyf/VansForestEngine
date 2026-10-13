#include "VansPhysicsCollisionGeometry.h"
#include "VansPhysicsNativeAccess.h"
#include "VansPhysicsShapeDimensions.h"
#include <glm/gtc/quaternion.hpp>
#include <cstring>
#include <algorithm>
#include <array>
#include <cmath>
#include <new>
#include <limits>
#include <glm/gtc/constants.hpp>

namespace VansEngine
{
namespace
{
bool Positive(float value) { return std::isfinite(value) && value>0; }
bool ReserveGeometry(Vans::VansTriangleMeshData& mesh,std::uint64_t vertices,std::uint64_t indices,
    std::uint64_t maximumBytes,std::string& error)
{
    if(vertices>10000000 || indices>30000000 || vertices*3*sizeof(float)+indices*sizeof(std::uint32_t)>maximumBytes)
    { error="Collision CPU geometry exceeds its allocation budget"; return false; }
    mesh.positions.reserve(static_cast<std::size_t>(vertices)*3);
    mesh.indices.reserve(static_cast<std::size_t>(indices)); return true;
}
void Box(const glm::vec3& extents,Vans::VansTriangleMeshData& mesh)
{
	constexpr std::array<std::uint32_t,36> indices{4,7,6,4,6,5,0,1,2,0,2,3,0,4,5,0,5,1,1,5,6,1,6,2,2,6,7,2,7,3,3,7,4,3,4,0};
	constexpr std::array<glm::vec3,8> points{glm::vec3(-1,-1,-1),glm::vec3(1,-1,-1),glm::vec3(1,-1,1),glm::vec3(-1,-1,1),
		glm::vec3(-1,1,-1),glm::vec3(1,1,-1),glm::vec3(1,1,1),glm::vec3(-1,1,1)};
	for(const auto& p:points) { const auto value=p*extents; mesh.positions.insert(mesh.positions.end(),{value.x,value.y,value.z}); }
	mesh.indices.assign(indices.begin(),indices.end());
}
bool Round(float radius,float halfHeight,float errorMeters,std::uint64_t maximumBytes,Vans::VansTriangleMeshData& mesh,std::string& error)
{
	const float angle=std::acos((std::max)(0.f,1.f-errorMeters/(4.f*radius)));
	if(!Positive(angle)) { error="Collision curve tolerance is not representable"; return false; }
	const double sliceCount=std::ceil(glm::two_pi<double>()/angle),capCount=std::ceil(glm::half_pi<double>()/angle);
	if(!std::isfinite(sliceCount) || !std::isfinite(capCount) || sliceCount>256 || capCount>128) { error="Collision curve triangulation exceeds bounded sampling capacity"; return false; }
	const int slices=(std::max)(8,static_cast<int>(sliceCount));
	const int capSteps=(std::max)(2,static_cast<int>(capCount));
    const auto ordinaryRings=static_cast<std::uint64_t>(2*capSteps-(halfHeight==0?1:0));
    if(!ReserveGeometry(mesh,ordinaryRings*slices+2,ordinaryRings*slices*6,maximumBytes,error)) return false;
	// 膨胀不超过显式误差；弦面不会侵入真实曲面而漏掉阻挡。
	const float conservativeRadius=radius+errorMeters*.5f;
	std::vector<std::pair<std::uint32_t,int>> rings;
    rings.reserve(static_cast<std::size_t>(ordinaryRings)+2);
	for(int cap=0;cap<2;++cap) for(int step=0;step<=capSteps;++step)
	{
		if(halfHeight==0 && cap==1 && step==0) continue;
		const float theta=(cap+static_cast<float>(step)/capSteps)*glm::half_pi<float>();
		const float axial=std::cos(theta)*conservativeRadius+(cap==0?halfHeight:-halfHeight);
		const bool pole=(cap==0 && step==0)||(cap==1 && step==capSteps);
		const float radial=pole?0.f:std::sin(theta)*conservativeRadius;
		const int count=pole?1:slices;
		rings.emplace_back(static_cast<std::uint32_t>(mesh.VertexCount()),count);
		for(int slice=0;slice<count;++slice)
		{
			const float phi=glm::two_pi<float>()*slice/slices;
			mesh.positions.insert(mesh.positions.end(),{axial,radial*std::cos(phi),radial*std::sin(phi)});
		}
	}
	for(std::size_t ring=0;ring+1<rings.size();++ring) for(int slice=0;slice<slices;++slice)
	{
		const auto a=rings[ring].first+(rings[ring].second==1?0:slice),
			b=rings[ring+1].first+(rings[ring+1].second==1?0:slice),
			c=rings[ring+1].first+(rings[ring+1].second==1?0:(slice+1)%slices),
			d=rings[ring].first+(rings[ring].second==1?0:(slice+1)%slices);
		if(b!=c) mesh.indices.insert(mesh.indices.end(),{a,b,c});
		if(a!=d) mesh.indices.insert(mesh.indices.end(),{a,c,d});
	}
	return true;
}
bool ExportNative(const physx::PxGeometryHolder& native,const physx::PxTransform& pose,float tolerance,
	std::uint64_t maximumBytes,Vans::VansTriangleMeshData& output,std::string& error)
{
	Vans::VansTriangleMeshData mesh;
	physx::PxMeshScale scale;
	bool scaled=false;
	switch(native.getType())
	{
	case physx::PxGeometryType::eBOX:
	{
		const auto extents=native.box().halfExtents;
		if(!native.box().isValid()) { error="Actual Physics box dimensions are invalid"; return false; }
		if(!ReserveGeometry(mesh,8,36,maximumBytes,error)) return false;
		Box(glm::vec3(extents.x,extents.y,extents.z),mesh); break;
	}
	case physx::PxGeometryType::eSPHERE:
		if(!native.sphere().isValid()) { error="Actual Physics sphere dimensions are invalid"; return false; }
		if(!Round(native.sphere().radius,0,tolerance,maximumBytes,mesh,error)) return false;
		break;
	case physx::PxGeometryType::eCAPSULE:
		if(!native.capsule().isValid()) { error="Actual Physics capsule dimensions are invalid"; return false; }
		if(!Round(native.capsule().radius,native.capsule().halfHeight,tolerance,maximumBytes,mesh,error)) return false;
		break;
	case physx::PxGeometryType::eTRIANGLEMESH:
	{
		const auto& geometry=native.triangleMesh(); const auto* cooked=geometry.triangleMesh;
		if(!geometry.isValid() || !cooked || cooked->getNbVertices()>10000000 || cooked->getNbTriangles()>10000000)
		{ error="Actual triangle collision geometry is invalid or exceeds capacity"; return false; }
		if(!ReserveGeometry(mesh,cooked->getNbVertices(),static_cast<std::uint64_t>(cooked->getNbTriangles())*3,maximumBytes,error)) return false;
		for(physx::PxU32 i=0;i<cooked->getNbVertices();++i) { const auto p=cooked->getVertices()[i]; mesh.positions.insert(mesh.positions.end(),{p.x,p.y,p.z}); }
		const auto count=cooked->getNbTriangles()*3u;
		if(cooked->getTriangleMeshFlags().isSet(physx::PxTriangleMeshFlag::e16_BIT_INDICES))
		{ const auto* indices=static_cast<const physx::PxU16*>(cooked->getTriangles()); mesh.indices.assign(indices,indices+count); }
		else { const auto* indices=static_cast<const physx::PxU32*>(cooked->getTriangles()); mesh.indices.assign(indices,indices+count); }
		scale=geometry.scale; scaled=true; break;
	}
	case physx::PxGeometryType::eCONVEXMESH:
	{
		const auto& geometry=native.convexMesh(); const auto* cooked=geometry.convexMesh;
		if(!geometry.isValid() || !cooked) { error="Actual convex collision geometry is invalid"; return false; }
        std::uint64_t indexCount=0;
        for(physx::PxU32 i=0;i<cooked->getNbPolygons();++i)
        {
            physx::PxHullPolygon polygon;
            if(!cooked->getPolygonData(i,polygon) || polygon.mNbVerts<3) { error="Actual convex polygon is invalid"; return false; }
            indexCount+=static_cast<std::uint64_t>(polygon.mNbVerts-2)*3;
        }
        if(!ReserveGeometry(mesh,cooked->getNbVertices(),indexCount,maximumBytes,error)) return false;
		for(physx::PxU32 i=0;i<cooked->getNbVertices();++i) { const auto p=cooked->getVertices()[i]; mesh.positions.insert(mesh.positions.end(),{p.x,p.y,p.z}); }
		for(physx::PxU32 i=0;i<cooked->getNbPolygons();++i)
		{
			physx::PxHullPolygon polygon;
			if(!cooked->getPolygonData(i,polygon)) { error="Actual convex polygon is unavailable"; return false; }
			const auto* indices=cooked->getIndexBuffer()+polygon.mIndexBase;
			for(physx::PxU32 k=1;k+1<polygon.mNbVerts;++k) mesh.indices.insert(mesh.indices.end(),{indices[0],indices[k],indices[k+1]});
		}
		scale=geometry.scale; scaled=true; break;
	}
	default: error="Actual Physics shape has no CPU collision exporter"; return false;
	}
	const auto matrix=scale.toMat33();
	for(std::size_t i=0;i<mesh.VertexCount();++i)
	{
		physx::PxVec3 p(mesh.positions[i*3],mesh.positions[i*3+1],mesh.positions[i*3+2]);
		if(scaled) p=matrix*p;
		p=pose.transform(p);
		if(!p.isFinite()) { error="Actual collision world vertex is non-finite"; return false; }
		mesh.positions[i*3]=p.x; mesh.positions[i*3+1]=p.y; mesh.positions[i*3+2]=p.z;
	}
	if(scaled && scale.scale.x*scale.scale.y*scale.scale.z<0)
		for(std::size_t i=0;i<mesh.indices.size();i+=3) std::swap(mesh.indices[i+1],mesh.indices[i+2]);
	output=std::move(mesh); return true;
}

std::uint64_t ShapeSignature(const physx::PxGeometryHolder& native,const physx::PxTransform& pose,float tolerance)
{
	std::uint64_t hash=14695981039346656037ull;
	const auto bytes=[&](const void* data,std::size_t size)
	{ const auto* p=static_cast<const unsigned char*>(data); for(std::size_t i=0;i<size;++i) { hash^=p[i]; hash*=1099511628211ull; } };
	const auto value=[&](const auto& v) { bytes(&v,sizeof(v)); };
	const auto vector=[&](const physx::PxVec3& v) { value(v.x); value(v.y); value(v.z); };
	const auto rotation=[&](const physx::PxQuat& q) { value(q.x); value(q.y); value(q.z); value(q.w); };
	value(native.getType()); vector(pose.p); rotation(pose.q); value(tolerance);
	switch(native.getType())
	{
	case physx::PxGeometryType::eBOX: vector(native.box().halfExtents); break;
	case physx::PxGeometryType::eSPHERE: value(native.sphere().radius); break;
	case physx::PxGeometryType::eCAPSULE: value(native.capsule().radius); value(native.capsule().halfHeight); break;
	case physx::PxGeometryType::eTRIANGLEMESH:
		value(native.triangleMesh().triangleMesh); vector(native.triangleMesh().scale.scale); rotation(native.triangleMesh().scale.rotation); break;
	case physx::PxGeometryType::eCONVEXMESH:
		value(native.convexMesh().convexMesh); vector(native.convexMesh().scale.scale); rotation(native.convexMesh().scale.rotation); break;
	default: break;
	}
	return hash?hash:1;
}
bool ResolveShapeInput(const PhysicsNodeProperties& properties,const Vans::VansTransform& world,const glm::vec3& creationScale,
    physx::PxTransform& shapePose,VansPhysicsShapeDimensions& dimensions,std::string& error)
{
    const auto finite=[](const glm::vec3& value){return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);};
    if(!finite(world.m_Position) || !finite(world.m_Rotation) || !finite(world.m_Scale) || !finite(creationScale) ||
        !finite(properties.shapeOffset) || world.m_Scale.x==0 || world.m_Scale.y==0 || world.m_Scale.z==0)
    { error="Collision geometry requires finite world TRS and nonzero scale"; return false; }
    const auto rotation=glm::quat(glm::radians(world.m_Rotation));
    const auto pose=physx::PxTransform(physx::PxVec3(world.m_Position.x,world.m_Position.y,world.m_Position.z),
        physx::PxQuat(rotation.x,rotation.y,rotation.z,rotation.w)).getNormalized();
    shapePose=pose*physx::PxTransform(physx::PxVec3(properties.shapeOffset.x,properties.shapeOffset.y,properties.shapeOffset.z));
    if(!shapePose.isValid()) { error="Physics collision pose is invalid"; return false; }
    const auto delta=glm::abs(world.m_Scale-creationScale);
    const auto phase=delta.x<=1.e-5f && delta.y<=1.e-5f && delta.z<=1.e-5f?
        VansPhysicsShapeScalePhase::Creation:VansPhysicsShapeScalePhase::Update;
    dimensions=ResolvePhysicsShapeDimensions(properties,world.m_Scale,phase);
    return true;
}
bool ExportBoxShape(const physx::PxBoxGeometry& box,const physx::PxTransform& pose,VansPhysicsBoxShapeSnapshot& output,std::string& error)
{
    if(!box.isValid() || !pose.isValid()) { error="Physics Box shape or world pose is invalid"; return false; }
    VansPhysicsBoxShapeSnapshot candidate;
    candidate.worldTransform=glm::mat4_cast(glm::quat(pose.q.w,pose.q.x,pose.q.y,pose.q.z));
    candidate.worldTransform[3]=glm::vec4(pose.p.x,pose.p.y,pose.p.z,1);
    candidate.halfExtents={box.halfExtents.x,box.halfExtents.y,box.halfExtents.z};
    output=candidate; error.clear(); return true;
}
}
bool VansPhysicsCollisionGeometry::BuildBoxShape(const PhysicsNodeProperties& properties,const Vans::VansTransform& world,
    const glm::vec3& creationScale,VansPhysicsBoxShapeSnapshot& output,std::string& error)
{
    if(properties.colliderType!=PhysicsColliderType::Box) { error="Physics collision is not a Box shape"; return false; }
    physx::PxTransform pose; VansPhysicsShapeDimensions dimensions;
    if(!ResolveShapeInput(properties,world,creationScale,pose,dimensions,error)) return false;
    return ExportBoxShape(physx::PxBoxGeometry(dimensions.boxExtents.x,dimensions.boxExtents.y,dimensions.boxExtents.z),pose,output,error);
}
bool VansPhysicsCollisionGeometry::CaptureBoxShape(const VansPhysicsNode& node,VansPhysicsBoxShapeSnapshot& output,std::string& error)
{
    std::lock_guard<std::mutex> lock(VansPhysicsSystem::GetInstance().GetSimulationMutex());
    if(!node.m_Actor || !node.m_Shape) { error="Physics node has no actual collision shape"; return false; }
    const physx::PxGeometryHolder native(node.m_Shape->getGeometry());
    if(native.getType()!=physx::PxGeometryType::eBOX) { error="Physics collision is not a Box shape"; return false; }
    return ExportBoxShape(native.box(),node.m_Actor->getGlobalPose()*node.m_Shape->getLocalPose(),output,error);
}
bool VansPhysicsCollisionGeometry::Build(const PhysicsNodeProperties& properties,const Vans::VansTransform& world,
    const glm::vec3& creationScale,const Vans::VansTriangleMeshData* source,float tolerance,std::uint64_t maximumBytes,Vans::VansTriangleMeshData& output,std::string& error)
try
{
    if(!Positive(tolerance)) { error="Collision geometry requires positive finite tolerance"; return false; }
    physx::PxTransform shapePose; VansPhysicsShapeDimensions dimensions;
    if(!ResolveShapeInput(properties,world,creationScale,shapePose,dimensions,error)) return false;
    physx::PxGeometryHolder native;
    auto& physics=VansPhysicsSystem::GetInstance();
    std::unique_lock<std::mutex> cookingLease(physics.GetSimulationMutex(),std::defer_lock);
    std::unique_ptr<physx::PxTriangleMesh,void(*)(physx::PxTriangleMesh*)> triangle(nullptr,[](auto* p){if(p) p->release();});
    std::unique_ptr<physx::PxConvexMesh,void(*)(physx::PxConvexMesh*)> convex(nullptr,[](auto* p){if(p) p->release();});
    switch(properties.colliderType)
    {
    case PhysicsColliderType::Box:
        native.storeAny(physx::PxBoxGeometry(dimensions.boxExtents.x,dimensions.boxExtents.y,dimensions.boxExtents.z)); break;
    case PhysicsColliderType::Sphere:
        native.storeAny(physx::PxSphereGeometry(dimensions.sphereRadius)); break;
    case PhysicsColliderType::Capsule:
        native.storeAny(physx::PxCapsuleGeometry(dimensions.capsuleRadius,dimensions.capsuleHalfHeight)); break;
    case PhysicsColliderType::Mesh:
    case PhysicsColliderType::ConvexMesh:
    {
        if(!properties.useMeshCollider || !source ||
            source->VertexCount()>10000000 || source->TriangleCount()>10000000)
        { error="Included mesh collision source requires bounded valid CPU mesh data"; return false; }
        if(!source->Validate(maximumBytes/4,error)) return false;
        cookingLease.lock();
        if(!VansPhysicsNativeAccess::CookingParams(physics)) { error="Physics cooking service is not initialized"; return false; }
        const physx::PxMeshScale scale(physx::PxVec3(world.m_Scale.x,world.m_Scale.y,world.m_Scale.z));
        if(properties.colliderType==PhysicsColliderType::ConvexMesh)
        {
            physx::PxConvexMeshDesc desc; desc.points.count=static_cast<physx::PxU32>(source->VertexCount());
            desc.points.stride=3*sizeof(float); desc.points.data=source->positions.data();
            desc.flags=physx::PxConvexFlag::eCOMPUTE_CONVEX; desc.vertexLimit=255;
            convex.reset(VansPhysicsNativeAccess::CookConvexMesh(physics,desc));
            if(!convex) { error="Included convex collision source failed actual PhysX cooking"; return false; }
            native.storeAny(physx::PxConvexMeshGeometry(convex.get(),scale));
        }
        else
        {
            physx::PxTriangleMeshDesc desc; desc.points.count=static_cast<physx::PxU32>(source->VertexCount());
            desc.points.stride=3*sizeof(float); desc.points.data=source->positions.data();
            desc.triangles.count=static_cast<physx::PxU32>(source->TriangleCount());
            desc.triangles.stride=3*sizeof(std::uint32_t); desc.triangles.data=source->indices.data();
            triangle.reset(VansPhysicsNativeAccess::CookTriangleMesh(physics,desc));
            if(!triangle) { error="Included triangle collision source failed actual PhysX cooking"; return false; }
            native.storeAny(physx::PxTriangleMeshGeometry(triangle.get(),scale));
        }
        break;
    }
    default: error="Included Physics collider type has no CPU geometry exporter"; return false;
    }
    if(!ExportNative(native,shapePose,tolerance,maximumBytes,output,error)) return false;
    error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Collision CPU geometry allocation failed"; return false; }

bool VansPhysicsCollisionGeometry::Capture(const VansPhysicsNode& node,float tolerance,std::uint64_t maximumBytes,
	VansPhysicsCollisionGeometrySnapshot& output,std::string& error)
try
{
	if(!Positive(tolerance)) { error="Collision capture requires a positive finite tolerance"; return false; }
	std::lock_guard<std::mutex> lock(VansPhysicsSystem::GetInstance().GetSimulationMutex());
	if(!node.m_Actor || !node.m_Shape) { error="Included Physics node has no actual collision shape"; return false; }
	const physx::PxGeometryHolder native(node.m_Shape->getGeometry());
	const auto pose=node.m_Actor->getGlobalPose()*node.m_Shape->getLocalPose();
	if(!pose.isValid()) { error="Actual Physics collision pose is invalid"; return false; }
	const auto signature=ShapeSignature(native,pose,tolerance);
	if(output.geometry && output.signature==signature) { error.clear(); return true; }
	auto geometry=std::make_shared<Vans::VansTriangleMeshData>();
	if(!ExportNative(native,pose,tolerance,maximumBytes,*geometry,error)) return false;
    glm::vec3 minimum((std::numeric_limits<float>::max)()),maximum=-minimum;
    for(std::size_t i=0;i<geometry->VertexCount();++i)
    {
        const glm::vec3 p(geometry->positions[i*3],geometry->positions[i*3+1],geometry->positions[i*3+2]);
        minimum=glm::min(minimum,p); maximum=glm::max(maximum,p);
    }
	output={signature,std::move(geometry),minimum,maximum}; error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Actual Physics CPU snapshot allocation failed"; return false; }
}
