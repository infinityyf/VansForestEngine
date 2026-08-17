#include "../../SceneRuntime/Timeline/VansTransformTimelineAccess.h"

#include "../VansScene.h"
#include "../../PhysicsCore/VansPhysicsNode.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"

namespace VansGraphics
{
namespace
{
class RenderTransformTimelineAccess final : public Vans::IVansTimelineTransformAccess
{
public:
	RenderTransformTimelineAccess(VansScene& scene, Vans::VansRuntimeWorld& world)
		: m_Scene(scene), m_World(world) {}

	std::uint32_t ParentTransform(std::uint32_t child) const override
	{ return m_Scene.GetParentTransformID(child); }

	bool CanWrite(const Vans::VansResolvedTimelineTarget& target,
		std::string_view physicsPolicy, std::string& error) const override
	{
		if (physicsPolicy != "RejectDynamicBody")
		{ error = "Transform physicsPolicy is not registered: " + std::string(physicsPolicy); return false; }
		auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimePhysicsComponent>*>(
			m_World.FindStorage(Vans::VansRuntimeComponentType_Physics));
		if (!storage || !m_World.IsAlive(target.entity)) return true;
		for (Vans::VansComponentHandle component : m_World.CollectComponentsOwnedBy(target.entity))
			if (component.typeId == Vans::VansRuntimeComponentType_Physics)
				if (const auto* runtime = storage->Get(component))
					if (runtime->physicsNode && runtime->physicsNode->GetProperties().bodyType ==
						VansEngine::PhysicsBodyType::Dynamic)
					{ error = "Transform Timeline cannot drive a dynamic rigid body"; return false; }
		return true;
	}

	void NotifyWritten(std::uint32_t transform) override
	{ m_Scene.MarkTransformOffsetDirty(transform); }

private:
	VansScene& m_Scene;
	Vans::VansRuntimeWorld& m_World;
};
}

std::shared_ptr<Vans::IVansTimelineTransformAccess> VansCreateTimelineTransformAccess(
	VansScene& scene, Vans::VansRuntimeWorld& world)
{
	return std::make_shared<RenderTransformTimelineAccess>(scene, world);
}
}
