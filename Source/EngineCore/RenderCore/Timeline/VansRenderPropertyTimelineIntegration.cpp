#include "VansRenderPropertyTimelineIntegration.h"

#include "../BRDFData/VansLight.h"
#include "../VansRenderNode.h"
#include "../VansScene.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../../TimelineRuntime/VansTimelineEvaluator.h"
#include "../../TimelineRuntime/VansTimelineModuleApplierState.h"
#include "../../TimelineRuntime/VansTimelinePropertyAccessRegistry.h"
#include "../VansCamera.h"
#include "../../TimelineRuntime/VansTimelineSampleExtension.h"

#include <algorithm>
#include <cmath>
#include <variant>
#include <unordered_map>
#include <type_traits>

namespace VansGraphics
{
namespace
{
using namespace Vans;

VansCamera* ResolveCamera(const VansTimelinePropertyAccessContext& context)
{
	if (!context.world) return nullptr;
	auto* storage = static_cast<VansComponentStorage<VansRuntimeCameraComponent>*>(
		context.world->FindStorage(VansRuntimeComponentType_Camera));
	if (!storage) return nullptr;
	if (context.target.component.IsValid() && context.target.component.typeId == VansRuntimeComponentType_Camera)
		if (const auto* runtime = storage->Get(context.target.component)) return runtime->camera;
	if (!context.world->IsAlive(context.target.entity)) return nullptr;
	for (VansComponentHandle component : context.world->CollectComponentsOwnedBy(context.target.entity))
		if (component.typeId == VansRuntimeComponentType_Camera)
			if (const auto* runtime = storage->Get(component)) return runtime->camera;
	return nullptr;
}

#define VANS_CAMERA_FLOAT_PROPERTY(Name, Getter, Setter) \
	bool ReadCamera##Name(const VansTimelinePropertyAccessContext& context, VansTimelineValue& value, std::string& error) \
	{ auto* camera = ResolveCamera(context); if (!camera) { error = "Camera." #Name " requires a Camera binding"; return false; } value = static_cast<float>(camera->Getter()); return true; } \
	bool WriteCamera##Name(const VansTimelinePropertyAccessContext& context, const VansTimelineValue& value, std::string& error) \
	{ auto* camera = ResolveCamera(context); const auto* typed = std::get_if<float>(&value); if (!camera || !typed) { error = "Camera." #Name " target or value is invalid"; return false; } camera->Setter(*typed); return true; }
VANS_CAMERA_FLOAT_PROPERTY(FieldOfView, GetFov, SetFov)
VANS_CAMERA_FLOAT_PROPERTY(NearClip, GetNearClip, SetNearClip)
VANS_CAMERA_FLOAT_PROPERTY(FarClip, GetFarClip, SetFarClip)
#undef VANS_CAMERA_FLOAT_PROPERTY

VansComponentHandle ResolveLightComponent(VansRuntimeWorld& world,
	const VansResolvedTimelineTarget& target)
{
	if (target.component.IsValid() && target.component.typeId >= VansRuntimeComponentType_DirectionalLight &&
		target.component.typeId <= VansRuntimeComponentType_RectLight) return target.component;
	if (!world.IsAlive(target.entity)) return {};
	for (VansComponentHandle component : world.CollectComponentsOwnedBy(target.entity))
		if (component.typeId >= VansRuntimeComponentType_DirectionalLight &&
			component.typeId <= VansRuntimeComponentType_RectLight) return component;
	return {};
}

VansRuntimeLightComponent* ResolveLight(VansRuntimeWorld& world,
	const VansResolvedTimelineTarget& target, VansComponentHandle* handle = nullptr)
{
	const VansComponentHandle component = ResolveLightComponent(world, target);
	if (handle) *handle = component;
	if (!component.IsValid()) return nullptr;
	auto* storage = static_cast<VansComponentStorage<VansRuntimeLightComponent>*>(
		world.FindStorage(component.typeId));
	return storage ? storage->Get(component) : nullptr;
}

double Number(const VansTimelineValue& value, double fallback)
{
	if (const auto* typed = std::get_if<std::int32_t>(&value)) return *typed;
	if (const auto* typed = std::get_if<std::int64_t>(&value)) return static_cast<double>(*typed);
	if (const auto* typed = std::get_if<float>(&value)) return *typed;
	if (const auto* typed = std::get_if<double>(&value)) return *typed;
	return fallback;
}

bool Boolean(const VansTimelineValue& value, bool fallback)
{
	if (const auto* typed = std::get_if<bool>(&value)) return *typed;
	return Number(value, fallback ? 1.0 : 0.0) != 0.0;
}

glm::vec3 Color(const VansTimelineValue& value, const glm::vec3& fallback)
{
	if (const auto* typed = std::get_if<VansTimelineColorLinear>(&value))
		return { typed->value[0], typed->value[1], typed->value[2] };
	if (const auto* typed = std::get_if<VansTimelineColorSrgb>(&value))
	{
		const glm::vec3 srgb(typed->value[0], typed->value[1], typed->value[2]);
		return glm::pow(glm::max(srgb, glm::vec3(0.0f)), glm::vec3(2.2f));
	}
	if (const auto* typed = std::get_if<VansTimelineVec3>(&value))
		return { typed->value[0], typed->value[1], typed->value[2] };
	return fallback;
}

glm::vec3 TemperatureColor(double kelvin)
{
	const double temperature = std::clamp(kelvin, 1000.0, 40000.0) / 100.0;
	const double red = temperature <= 66.0 ? 255.0 :
		329.698727446 * std::pow(temperature - 60.0, -0.1332047592);
	const double green = temperature <= 66.0
		? 99.4708025861 * std::log(temperature) - 161.1195681661
		: 288.1221695283 * std::pow(temperature - 60.0, -0.0755148492);
	const double blue = temperature >= 66.0 ? 255.0 : (temperature <= 19.0 ? 0.0 :
		138.5177312231 * std::log(temperature - 10.0) - 305.0447927307);
	return glm::pow(glm::vec3(
		static_cast<float>(std::clamp(red, 0.0, 255.0) / 255.0),
		static_cast<float>(std::clamp(green, 0.0, 255.0) / 255.0),
		static_cast<float>(std::clamp(blue, 0.0, 255.0) / 255.0)), glm::vec3(2.2f));
}

double BlendNumber(double current, double sampled, VansTimelineBlendMode mode)
{
	if (mode == VansTimelineBlendMode::Additive || mode == VansTimelineBlendMode::Relative)
		return current + sampled;
	if (mode == VansTimelineBlendMode::Multiply) return current * sampled;
	return sampled;
}

glm::vec3 BlendColor(const glm::vec3& current, const glm::vec3& sampled,
	VansTimelineBlendMode mode)
{
	if (mode == VansTimelineBlendMode::Additive || mode == VansTimelineBlendMode::Relative)
		return current + sampled;
	if (mode == VansTimelineBlendMode::Multiply) return current * sampled;
	return sampled;
}

struct LightSnapshot
{
	VansTimelineWriterHandle writer;
	VansComponentHandle component;
	VansLightManager* manager = nullptr;
	int index = -1;
	VansRuntimeLightKind kind = VansRuntimeLightKind::Directional;
	std::variant<VansDirectionalLight, VansPointLight, VansSpotLight, VansRectLight> light;
	VansPunctualShadowSettings shadow;
	bool hasShadow = false;
};

bool CaptureLight(const VansRuntimeLightComponent& runtime, VansComponentHandle component,
	VansTimelineWriterHandle writer, LightSnapshot& snapshot, std::string& error)
{
	if (!runtime.lightManager || runtime.lightIndex < 0)
	{ error = "Light binding has no live renderer light"; return false; }
	snapshot.writer = writer; snapshot.component = component; snapshot.manager = runtime.lightManager;
	snapshot.index = runtime.lightIndex; snapshot.kind = runtime.kind;
	const std::size_t index = static_cast<std::size_t>(runtime.lightIndex);
	if (runtime.kind == VansRuntimeLightKind::Directional)
	{
		auto& lights = runtime.lightManager->GetDirectionLights();
		if (index >= lights.size()) { error = "Directional Light runtime index is invalid"; return false; }
		snapshot.light = lights[index]; return true;
	}
	if (runtime.kind == VansRuntimeLightKind::Point)
	{
		auto& lights = runtime.lightManager->GetPointLights();
		if (index >= lights.size()) { error = "Point Light runtime index is invalid"; return false; }
		snapshot.light = lights[index];
		auto& shadows = runtime.lightManager->GetPointShadowRegistrations();
		if (index < shadows.size()) { snapshot.shadow = shadows[index].settings; snapshot.hasShadow = true; }
		return true;
	}
	if (runtime.kind == VansRuntimeLightKind::Spot)
	{
		auto& lights = runtime.lightManager->GetSpotLight();
		if (index >= lights.size()) { error = "Spot Light runtime index is invalid"; return false; }
		snapshot.light = lights[index];
		auto& shadows = runtime.lightManager->GetSpotShadowRegistrations();
		if (index < shadows.size()) { snapshot.shadow = shadows[index].settings; snapshot.hasShadow = true; }
		return true;
	}
	auto& lights = runtime.lightManager->GetRectLights();
	if (index >= lights.size()) { error = "Rect Light runtime index is invalid"; return false; }
	snapshot.light = lights[index];
	auto& shadows = runtime.lightManager->GetRectShadowRegistrations();
	if (index < shadows.size()) { snapshot.shadow = shadows[index].settings; snapshot.hasShadow = true; }
	return true;
}

void RestoreLight(const LightSnapshot& snapshot)
{
	if (!snapshot.manager || snapshot.index < 0) return;
	const std::size_t index = static_cast<std::size_t>(snapshot.index);
	if (snapshot.kind == VansRuntimeLightKind::Directional)
	{
		auto& lights = snapshot.manager->GetDirectionLights();
		if (index < lights.size()) lights[index] = std::get<VansDirectionalLight>(snapshot.light);
	}
	else if (snapshot.kind == VansRuntimeLightKind::Point)
	{
		auto& lights = snapshot.manager->GetPointLights();
		if (index < lights.size()) lights[index] = std::get<VansPointLight>(snapshot.light);
		auto& shadows = snapshot.manager->GetPointShadowRegistrations();
		if (snapshot.hasShadow && index < shadows.size()) shadows[index].settings = snapshot.shadow;
	}
	else if (snapshot.kind == VansRuntimeLightKind::Spot)
	{
		auto& lights = snapshot.manager->GetSpotLight();
		if (index < lights.size()) lights[index] = std::get<VansSpotLight>(snapshot.light);
		auto& shadows = snapshot.manager->GetSpotShadowRegistrations();
		if (snapshot.hasShadow && index < shadows.size()) shadows[index].settings = snapshot.shadow;
	}
	else
	{
		auto& lights = snapshot.manager->GetRectLights();
		if (index < lights.size()) lights[index] = std::get<VansRectLight>(snapshot.light);
		auto& shadows = snapshot.manager->GetRectShadowRegistrations();
		if (snapshot.hasShadow && index < shadows.size()) shadows[index].settings = snapshot.shadow;
	}
	snapshot.manager->UpdateLightCPUData();
}

class LightTimelineApplier final : public IVansTimelineOutputApplier
{
public:
	explicit LightTimelineApplier(VansRuntimeWorld& world) : m_World(world) {}
	VansTimelineOutputTypeId OutputType() const override
	{ return VansMakeStableId<VansTimelineOutputTypeTag>(std::string(TimelineNames::Light) + ".Output"); }
	std::string_view StableName() const override { return "Render.LightTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<VansTimelineSampleOutput>();
		if (!sample || !sample->active || !context.section) return { VansTimelineApplyStatus::Ignored };
		VansComponentHandle component; VansRuntimeLightComponent* runtime = ResolveLight(m_World, target, &component);
		if (!runtime) return { VansTimelineApplyStatus::Failed, {}, "Light track requires a runtime Light component" };
		std::string captureError;
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{
			LightSnapshot snapshot; CaptureLight(*runtime, component, context.writer, snapshot, captureError); return snapshot;
		});
		if (!captureError.empty()) { m_State.Release(restore); return { VansTimelineApplyStatus::Failed, {}, captureError }; }
		VansLightManager& manager = *runtime->lightManager;
		const std::size_t index = static_cast<std::size_t>(runtime->lightIndex);
		for (const VansTimelineChannel& channel : context.section->channels)
		{
			const auto value = VansTimelineEvaluator::SampleChannel(channel, sample->localTick);
			if (!value) continue;
			const std::string& property = channel.name;
			auto sampledColor = [&](const glm::vec3& current)
			{
				return property == "temperature" ? TemperatureColor(Number(*value, 6500.0)) : Color(*value, current);
			};
			if (runtime->kind == VansRuntimeLightKind::Directional)
			{
				auto& light = manager.GetDirectionLights()[index];
				if (property == "color" || property == "temperature") light.m_Color = BlendColor(light.m_Color, sampledColor(light.m_Color), context.blendMode);
				else if (property == "intensity") light.m_Intensity = static_cast<float>(std::max(0.0, BlendNumber(light.m_Intensity, Number(*value, light.m_Intensity), context.blendMode)));
				else return { VansTimelineApplyStatus::Failed, {}, "Directional Light property is unavailable: " + property };
			}
			else if (runtime->kind == VansRuntimeLightKind::Point)
			{
				auto& light = manager.GetPointLights()[index];
				if (property == "color" || property == "temperature") light.m_Color = BlendColor(light.m_Color, sampledColor(light.m_Color), context.blendMode);
				else if (property == "intensity") light.m_Intensity = static_cast<float>(std::max(0.0, BlendNumber(light.m_Intensity, Number(*value, light.m_Intensity), context.blendMode)));
				else if (property == "range" || property == "radius") light.m_Radius = static_cast<float>(std::max(0.0, BlendNumber(light.m_Radius, Number(*value, light.m_Radius), context.blendMode)));
				else if (property == "castShadows")
				{
					auto& shadows = manager.GetPointShadowRegistrations();
					if (index >= shadows.size()) return { VansTimelineApplyStatus::Failed, {}, "Point Light shadow registration is unavailable" };
					shadows[index].settings.castShadows = Boolean(*value, false);
				}
				else return { VansTimelineApplyStatus::Failed, {}, "Point Light property is unavailable: " + property };
			}
			else if (runtime->kind == VansRuntimeLightKind::Spot)
			{
				auto& light = manager.GetSpotLight()[index];
				if (property == "color" || property == "temperature") light.m_Color = BlendColor(light.m_Color, sampledColor(light.m_Color), context.blendMode);
				else if (property == "intensity") light.m_Intensity = static_cast<float>(std::max(0.0, BlendNumber(light.m_Intensity, Number(*value, light.m_Intensity), context.blendMode)));
				else if (property == "range" || property == "radius") light.m_Radius = static_cast<float>(std::max(0.0, BlendNumber(light.m_Radius, Number(*value, light.m_Radius), context.blendMode)));
				else if (property == "innerCone") light.m_InnerCutOff = static_cast<float>(BlendNumber(light.m_InnerCutOff, Number(*value, light.m_InnerCutOff), context.blendMode));
				else if (property == "outerCone") light.m_OuterCutOff = static_cast<float>(BlendNumber(light.m_OuterCutOff, Number(*value, light.m_OuterCutOff), context.blendMode));
				else if (property == "castShadows")
				{
					auto& shadows = manager.GetSpotShadowRegistrations();
					if (index >= shadows.size()) return { VansTimelineApplyStatus::Failed, {}, "Spot Light shadow registration is unavailable" };
					shadows[index].settings.castShadows = Boolean(*value, false);
				}
				else return { VansTimelineApplyStatus::Failed, {}, "Spot Light property is unavailable: " + property };
			}
			else
			{
				auto& light = manager.GetRectLights()[index];
				if (property == "color" || property == "temperature") light.m_Color = BlendColor(light.m_Color, sampledColor(light.m_Color), context.blendMode);
				else if (property == "intensity") light.m_Intensity = static_cast<float>(std::max(0.0, BlendNumber(light.m_Intensity, Number(*value, light.m_Intensity), context.blendMode)));
				else if (property == "range") light.m_Range = static_cast<float>(std::max(0.0, BlendNumber(light.m_Range, Number(*value, light.m_Range), context.blendMode)));
				else if (property == "width") light.m_HalfWidth = static_cast<float>(std::max(0.0, BlendNumber(light.m_HalfWidth * 2.0, Number(*value, light.m_HalfWidth * 2.0), context.blendMode)) * 0.5);
				else if (property == "height") light.m_HalfHeight = static_cast<float>(std::max(0.0, BlendNumber(light.m_HalfHeight * 2.0, Number(*value, light.m_HalfHeight * 2.0), context.blendMode)) * 0.5);
				else if (property == "castShadows")
				{
					auto& shadows = manager.GetRectShadowRegistrations();
					if (index >= shadows.size()) return { VansTimelineApplyStatus::Failed, {}, "Rect Light shadow registration is unavailable" };
					shadows[index].settings.castShadows = Boolean(*value, false);
				}
				else return { VansTimelineApplyStatus::Failed, {}, "Rect Light property is unavailable: " + property };
			}
		}
		manager.UpdateLightCPUData();
		const std::uint64_t instance = (static_cast<std::uint64_t>(component.generation) << 32) |
			(static_cast<std::uint64_t>(component.typeId) << 16) | (component.index + 1ull);
		return { VansTimelineApplyStatus::Applied,
			{ restore, {}, {}, { VansStableHash64("Render.Light"), instance } } };
	}
	bool Restore(VansTimelineRestoreToken token) override
	{
		LightSnapshot* state = m_State.Resolve(token.handle);
		if (!state) return false;
		if (m_World.GetComponentHeader(state->component)) RestoreLight(*state);
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(VansTimelineWriterHandle writer) override { m_State.ReleaseWriter(writer); }
	void ReleaseAll() override { m_State.Clear(); }
private:
	VansRuntimeWorld& m_World;
	VansTimelineModuleApplierState<LightSnapshot> m_State;
};

VansComponentHandle ResolveRenderComponent(VansRuntimeWorld& world,
	const VansResolvedTimelineTarget& target)
{
	if (target.component.IsValid() && target.component.typeId == VansRuntimeComponentType_Render)
		return target.component;
	if (!world.IsAlive(target.entity)) return {};
	for (VansComponentHandle component : world.CollectComponentsOwnedBy(target.entity))
		if (component.typeId == VansRuntimeComponentType_Render) return component;
	return {};
}

std::vector<VansRenderNode*> ResolveRenderNodes(VansRuntimeWorld& world,
	const VansResolvedTimelineTarget& target, const std::string& slot)
{
	std::vector<VansRenderNode*> nodes;
	const VansComponentHandle component = ResolveRenderComponent(world, target);
	auto* storage = static_cast<VansComponentStorage<VansRuntimeRenderComponent>*>(
		world.FindStorage(VansRuntimeComponentType_Render));
	const VansRuntimeRenderComponent* runtime = storage ? storage->Get(component) : nullptr;
	if (!runtime) return nodes;
	if (!runtime->renderNodes.empty()) nodes = runtime->renderNodes;
	else if (runtime->renderNode) nodes.push_back(runtime->renderNode);
	nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](VansRenderNode* node)
	{
		if (!node) return true;
		if (slot.empty() || slot == "default" || slot == "all" || slot == "*") return false;
		if (slot == "0" && node->m_SubmeshIndex == UINT32_MAX) return false;
		return node->m_SubmeshIndex == UINT32_MAX || slot != std::to_string(node->m_SubmeshIndex);
	}), nodes.end());
	return nodes;
}

std::string String(const VansTimelineCompiledDataReader& reader,
	const VansTimelineCompiledDataView& data, std::size_t slot)
{
	const VansTimelineValue* value = reader.ValueAt(data, slot);
	const auto* typed = value ? std::get_if<std::string>(value) : nullptr;
	return typed ? *typed : std::string{};
}

VansMaterialParameterValue MaterialValue(const VansTimelineValue& value)
{
	return std::visit([](const auto& typed) -> VansMaterialParameterValue
	{
		using T = std::decay_t<decltype(typed)>;
		if constexpr (std::is_same_v<T, std::monostate>) return std::monostate{};
		else if constexpr (std::is_same_v<T, bool>) return typed;
		else if constexpr (std::is_same_v<T, std::int32_t>) return typed;
		else if constexpr (std::is_same_v<T, std::int64_t>) return static_cast<std::int32_t>(typed);
		else if constexpr (std::is_same_v<T, float>) return typed;
		else if constexpr (std::is_same_v<T, double>) return static_cast<float>(typed);
		else if constexpr (std::is_same_v<T, std::string>) return typed;
		else if constexpr (std::is_same_v<T, VansTimelineVec2>) return glm::vec2(typed.value[0], typed.value[1]);
		else if constexpr (std::is_same_v<T, VansTimelineVec3>) return glm::vec3(typed.value[0], typed.value[1], typed.value[2]);
		else if constexpr (std::is_same_v<T, VansTimelineVec4> ||
			std::is_same_v<T, VansTimelineQuaternion> ||
			std::is_same_v<T, VansTimelineColorLinear> ||
			std::is_same_v<T, VansTimelineColorSrgb>)
			return glm::vec4(typed.value[0], typed.value[1], typed.value[2], typed.value[3]);
		else if constexpr (std::is_same_v<T, VansTimelineObjectReference>) return typed.guid;
		else return std::monostate{};
	}, value);
}

RenderNodeType RuntimeNodeTypeForMaterial(const VansMaterial& material, RenderNodeType fallback)
{
	if (material.m_MaterialType == VAN_HAIR) return HAIR_NODE;
	if (material.m_MaterialType == VAN_TRANSPARENT || material.m_MaterialType == VAN_PBR_TRANSMISSION)
		return TRANSPARENT_NODE;
	if (material.m_MaterialType == VAN_CUSTOM_SHADER)
		return material.HasPass(VansPass::GBUFFER) ? OPAQUE_NODE
			: (material.m_CustomShaderDepthWrite ? FORWARD_OPAQUE_AFTER_DEFERRED_NODE : TRANSPARENT_NODE);
	if (fallback == DECAL_NODE || material.m_MaterialType == VAN_DECAL) return DECAL_NODE;
	return OPAQUE_NODE;
}

bool RuntimeNodeClassCompatible(const VansRenderNode& node, RenderNodeType target)
{
	if ((node.GetNodeType() == DECAL_NODE) != (target == DECAL_NODE)) return false;
	return (node.GetNodeType() == TRANSPARENT_NODE) == (target == TRANSPARENT_NODE);
}

VansTimelineResourceId MaterialResource(VansRenderNode* node)
{
	return { VansStableHash64("Render.MaterialSlot"),
		static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(node)) };
}

struct MaterialParameterState
{
	VansTimelineWriterHandle writer;
	VansRenderNode* node = nullptr;
	VansMaterial* previous = nullptr;
	VansMaterial* instance = nullptr;
	std::string instanceKey;
};

class MaterialParameterTimelineApplier final : public IVansTimelineOutputApplier
{
public:
	MaterialParameterTimelineApplier(VansScene& scene, VansRuntimeWorld& world)
		: m_Scene(scene), m_World(world) {}
	VansTimelineOutputTypeId OutputType() const override
	{ return VansMakeStableId<VansTimelineOutputTypeTag>(std::string(TimelineNames::MaterialParameter) + ".Output"); }
	std::string_view StableName() const override { return "Render.MaterialParameterTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<VansTimelineSampleOutput>();
		if (!sample || !sample->active || !context.section) return { VansTimelineApplyStatus::Ignored };
		const VansTimelineCompiledDataReader reader(context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		const std::string slot = String(reader, context.section->extensionData, 0);
		const std::string parameter = String(reader, context.section->extensionData, 1);
		const std::string policy = String(reader, context.section->extensionData, 3);
		if (policy != "PerEntityRuntimeInstance")
			return { VansTimelineApplyStatus::Failed, {}, "Material Parameter requires PerEntityRuntimeInstance" };
		std::vector<VansRenderNode*> nodes = ResolveRenderNodes(m_World, target, slot);
		if (nodes.size() != 1)
			return { VansTimelineApplyStatus::Failed, {}, "Material Parameter materialSlotId must resolve exactly one renderer slot" };
		VansRenderNode* node = nodes.front();
		if (!node || !node->m_Material)
			return { VansTimelineApplyStatus::Failed, {}, "Material Parameter renderer has no source material" };
		const VansTimelineChannel* channel = context.section->channels.empty() ? nullptr : &context.section->channels.front();
		const auto value = channel ? VansTimelineEvaluator::SampleChannel(*channel, sample->localTick) : std::nullopt;
		if (!value) return { VansTimelineApplyStatus::Ignored };
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{
			MaterialParameterState created;
			created.writer = context.writer; created.node = node; created.previous = node->m_Material;
			created.instanceKey = "Timeline.MaterialParameter." + std::to_string(context.writer.index) + "." +
				std::to_string(context.writer.generation) + "." + std::to_string(reinterpret_cast<std::uintptr_t>(node));
			created.instance = m_Scene.GetMaterialManager()->AcquireRuntimeMaterialInstance(
				created.instanceKey, *created.previous, m_Scene.GetGlobalDescriptorSet());
			return created;
		});
		if (!state || !state->instance)
		{
			m_State.Release(restore);
			return { VansTimelineApplyStatus::Failed, {}, "Runtime material instance pool is exhausted or source type is unsupported" };
		}
		if (state->node->m_Material != state->instance)
		{
			state->node->m_Material = state->instance;
			state->node->RecreateDescriptorSets(m_Scene.GetCamera(), *m_Scene.GetLightManager(), *m_Scene.GetMaterialManager());
		}
		if (!m_Scene.GetMaterialManager()->ApplyMaterialParameter(*state->instance, parameter, MaterialValue(*value)))
		{
			DeactivateWriter(context.writer);
			m_Scene.GetMaterialManager()->ReleaseRuntimeMaterialInstance(state->instanceKey);
			m_State.Release(restore);
			return { VansTimelineApplyStatus::Failed, {}, "Material parameter is unavailable for the runtime material: " + parameter };
		}
		return { VansTimelineApplyStatus::Applied, { restore, {}, {}, MaterialResource(node) } };
	}
	void DeactivateWriter(VansTimelineWriterHandle writer) override
	{
		if (MaterialParameterState* state = m_State.ResolveWriter(writer))
			if (state->node && state->node->m_Material == state->instance)
			{
				state->node->m_Material = state->previous;
				state->node->RecreateDescriptorSets(m_Scene.GetCamera(), *m_Scene.GetLightManager(), *m_Scene.GetMaterialManager());
			}
	}
	bool Restore(VansTimelineRestoreToken token) override
	{
		MaterialParameterState* state = m_State.Resolve(token.handle);
		if (!state) return false;
		DeactivateWriter(state->writer);
		m_Scene.GetMaterialManager()->ReleaseRuntimeMaterialInstance(state->instanceKey);
		return m_State.Release(token.handle);
	}
	void ReleaseWriter(VansTimelineWriterHandle writer) override
	{
		MaterialParameterState* state = m_State.ResolveWriter(writer);
		if (!state) return;
		if (!state->node || state->node->m_Material != state->instance)
			m_Scene.GetMaterialManager()->ReleaseRuntimeMaterialInstance(state->instanceKey);
		// A current KeepState instance remains scene-owned by MaterialManager until
		// another material selection replaces it or the scene releases its pools.
		m_State.ReleaseWriter(writer);
	}
	void ReleaseAll() override { m_State.Clear(); }
private:
	VansScene& m_Scene;
	VansRuntimeWorld& m_World;
	VansTimelineModuleApplierState<MaterialParameterState> m_State;
};

struct MaterialSwitchState
{
	VansTimelineWriterHandle writer;
	VansRenderNode* node = nullptr;
	VansMaterial* previous = nullptr;
	VansMaterial* applied = nullptr;
	RenderNodeType previousType = NONE_NODE;
	RenderNodeType appliedType = NONE_NODE;
	bool previousRayTracing = false;
};

class MaterialSwitchTimelineApplier final : public IVansTimelineOutputApplier
{
public:
	MaterialSwitchTimelineApplier(VansScene& scene, VansRuntimeWorld& world)
		: m_Scene(scene), m_World(world) {}
	VansTimelineOutputTypeId OutputType() const override
	{ return VansMakeStableId<VansTimelineOutputTypeTag>(std::string(TimelineNames::MaterialSwitch) + ".Output"); }
	std::string_view StableName() const override { return "Render.MaterialSwitchTimelineApplier"; }
	std::uint32_t PayloadSize() const override { return sizeof(VansTimelineSampleOutput); }
	std::uint32_t PayloadAlignment() const override { return alignof(VansTimelineSampleOutput); }
	VansTimelineApplyResult Apply(const VansTimelineApplyContext& context,
		const VansResolvedTimelineTarget& target, VansTimelineOutputPayloadView view) override
	{
		const auto* sample = view.As<VansTimelineSampleOutput>();
		if (!sample || !sample->active || !context.section) return { VansTimelineApplyStatus::Ignored };
		const VansTimelineCompiledDataReader reader(context.timeline.CompiledBytes(), context.timeline.CompiledValues());
		const std::string slot = String(reader, context.section->extensionData, 0);
		VansMaterial* material = dynamic_cast<VansMaterial*>(m_Scene.FindMaterialAsset(context.section->assetGuid));
		if (!material) return { VansTimelineApplyStatus::Failed, {}, "Material Switch dependency is not loaded: " + context.section->assetGuid };
		std::vector<VansRenderNode*> nodes = ResolveRenderNodes(m_World, target, slot);
		if (nodes.size() != 1)
			return { VansTimelineApplyStatus::Failed, {}, "Material Switch materialSlotId must resolve exactly one renderer slot" };
		VansRenderNode* node = nodes.front();
		const RenderNodeType targetType = RuntimeNodeTypeForMaterial(*material, node->GetNodeType());
		if (!RuntimeNodeClassCompatible(*node, targetType))
			return { VansTimelineApplyStatus::Failed, {}, "Material Switch crosses renderer node classes and requires a renderer rebuild" };
		auto [restore, state] = m_State.Acquire(context.writer, [&]
		{ return MaterialSwitchState{ context.writer, node, node->m_Material, material,
			node->GetNodeType(), targetType, node->m_RayTracingEnabled }; });
		if (node->GetNodeType() != targetType)
		{
			m_Scene.RemoveRenderNodeFromVector(node); node->SetNodeType(targetType); m_Scene.RegistRenderNode(node, targetType);
		}
		node->m_Material = material;
		node->m_RayTracingEnabled = material->m_MaterialType != VAN_TRANSPARENT && material->m_MaterialType != VAN_PBR_TRANSMISSION;
		node->RecreateDescriptorSets(m_Scene.GetCamera(), *m_Scene.GetLightManager(), *m_Scene.GetMaterialManager());
		return { VansTimelineApplyStatus::Applied, { restore, {}, {}, MaterialResource(node) } };
	}
	void DeactivateWriter(VansTimelineWriterHandle writer) override
	{
		MaterialSwitchState* state = m_State.ResolveWriter(writer);
		if (!state || !state->node) return;
		// A hidden writer may leave while another Timeline writer owns this slot.
		// Only remove the material selection if this writer is still visible.
		if (state->node->m_Material != state->applied ||
			state->node->GetNodeType() != state->appliedType) return;
		if (state->node->GetNodeType() != state->previousType)
		{
			m_Scene.RemoveRenderNodeFromVector(state->node); state->node->SetNodeType(state->previousType);
			m_Scene.RegistRenderNode(state->node, state->previousType);
		}
		state->node->m_Material = state->previous; state->node->m_RayTracingEnabled = state->previousRayTracing;
		state->node->RecreateDescriptorSets(m_Scene.GetCamera(), *m_Scene.GetLightManager(), *m_Scene.GetMaterialManager());
	}
	bool Restore(VansTimelineRestoreToken token) override
	{
		MaterialSwitchState* state = m_State.Resolve(token.handle);
		if (!state) return false;
		DeactivateWriter(state->writer); return m_State.Release(token.handle);
	}
	void ReleaseWriter(VansTimelineWriterHandle writer) override { m_State.ReleaseWriter(writer); }
	void ReleaseAll() override { m_State.Clear(); }
private:
	VansScene& m_Scene;
	VansRuntimeWorld& m_World;
	VansTimelineModuleApplierState<MaterialSwitchState> m_State;
};
}

bool VansRegisterRenderPropertyTimelineExtensions(
	VansTimelineTrackExtensionRegistry& registry,
	std::string& error)
{
	using F = VansTimelineValueType;
	if (!registry.Register(VansMakeTimelineSampleExtension(
		TimelineNames::Light, "Light", "Render", VansTimelineEvaluationPhase::PostScript,
		VansTimelineBindingRequirement::Required, VansTimelineContinuousTrackFlags(),
		{ {}, {
			VansMakeTimelineChannelSchema("color", F::ColorLinear),
			VansMakeTimelineChannelSchema("temperature", F::Float),
			VansMakeTimelineChannelSchema("intensity", F::Float),
			VansMakeTimelineChannelSchema("range", F::Float),
			VansMakeTimelineChannelSchema("radius", F::Float),
			VansMakeTimelineChannelSchema("innerCone", F::Float),
			VansMakeTimelineChannelSchema("outerCone", F::Float),
			VansMakeTimelineChannelSchema("width", F::Float),
			VansMakeTimelineChannelSchema("height", F::Float),
			VansMakeTimelineChannelSchema("castShadows", F::Bool) }, false, false }), error)) return false;
	if (!registry.Register(VansMakeTimelineSampleExtension(
		TimelineNames::MaterialParameter, "Material Parameter", "Render", VansTimelineEvaluationPhase::PostScript,
		VansTimelineBindingRequirement::Required, VansTimelineContinuousTrackFlags(),
		{ { VansMakeTimelineSourceField("materialSlotId", F::String, std::string(), true),
			VansMakeTimelineSourceField("parameterName", F::String, std::string(), true),
			VansMakeTimelineSourceField("parameterType", F::Enum, std::string("Float"), false,
				{ "Bool", "Int32", "Float", "Vec2", "Vec3", "Vec4", "ColorLinear", "ColorSrgb", "String" }),
			VansMakeTimelineSourceField("instancePolicy", F::Enum, std::string("PerEntityRuntimeInstance"), false,
				{ "PerEntityRuntimeInstance" }) },
			{ VansMakeTimelineChannelSchema("value", F::Float, true, "parameterType") }, false, false }), error)) return false;
	return registry.Register(VansMakeTimelineSampleExtension(
		TimelineNames::MaterialSwitch, "Material Switch", "Render", VansTimelineEvaluationPhase::PostScript,
		VansTimelineBindingRequirement::Required, VansTimelineContinuousTrackFlags(false),
		{ { VansMakeTimelineSourceField("materialSlotId", F::String, std::string(), true) }, {}, false, false }), error);
}

bool VansRegisterRenderTimelinePropertyAccessors(
	VansTimelinePropertyAccessRegistry& registry,
	std::string& error)
{
	auto add = [&](std::string name, VansTimelinePropertyReadFn read, VansTimelinePropertyWriteFn write)
	{
		VansTimelinePropertyAccessDescriptor descriptor;
		descriptor.id = VansMakeStableId<VansTimelinePropertyAccessTag>(name);
		descriptor.stableName = std::move(name); descriptor.componentTypeId = VansRuntimeComponentType_Camera;
		descriptor.valueType = VansTimelineValueType::Float; descriptor.read = read; descriptor.write = write;
		return registry.Register(std::move(descriptor), error);
	};
	return add("Camera.FieldOfView", ReadCameraFieldOfView, WriteCameraFieldOfView) &&
		add("Camera.NearClip", ReadCameraNearClip, WriteCameraNearClip) &&
		add("Camera.FarClip", ReadCameraFarClip, WriteCameraFarClip);
}

bool VansRegisterRenderPropertyTimelineIntegration(
	VansScene& scene,
	VansRuntimeWorld& world,
	VansTimelineApplierRegistry& registry,
	std::string& error)
{
	if (!registry.Register(std::make_shared<LightTimelineApplier>(world), error)) return false;
	if (!registry.Register(std::make_shared<MaterialParameterTimelineApplier>(scene, world), error)) return false;
	return registry.Register(std::make_shared<MaterialSwitchTimelineApplier>(scene, world), error);
}
}
