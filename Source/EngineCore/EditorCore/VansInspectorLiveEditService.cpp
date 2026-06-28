#include "VansInspectorLiveEditService.h"

#include "../ScriptCore/VansScriptContext.h"
#include "../ScriptCore/VansTransform.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtc/quaternion.hpp>
#include <../../GLM/gtx/quaternion.hpp>

#include <unordered_map>

namespace VansGraphics
{
namespace
{
using Json = nlohmann::ordered_json;
using JsonPointer = Json::json_pointer;

const Json* FindComponent(const Json& entity, const std::string& type)
{
	if (!entity.contains("components") || !entity["components"].is_array()) return nullptr;
	for (const Json& component : entity["components"])
		if (component.value("type", "") == type) return &component;
	return nullptr;
}

bool ReadVec3(const Json& value, glm::vec3& out)
{
	if (!value.is_array() || value.size() < 3) return false;
	out = glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
	return true;
}

bool ReadColor3(const Json& data, glm::vec3& out)
{
	const auto it = data.find("color");
	return it != data.end() && ReadVec3(*it, out);
}

bool ReadTransformRotationEuler(const Json& value, glm::vec3& out)
{
	if (!value.is_array()) return false;
	if (value.size() == 4)
	{
		const glm::quat q(value[3].get<float>(), value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
		out = glm::degrees(glm::eulerAngles(q));
		return true;
	}
	return ReadVec3(value, out);
}
}

void VansInspectorLiveEditService::ApplyEntityPatch(const Json& entity)
{
	if (!m_Scene) return;

	const std::string entityGuid = entity.value("id", "");
	if (entityGuid.empty()) return;

	VansScriptObject* obj = m_Scene->FindObjectByGuid(entityGuid);
	if (!obj) return;

	if (const Json* transformComponent = FindComponent(entity, "Transform"))
	{
		if (transformComponent->value("enabled", true) && transformComponent->contains("data") &&
			obj->m_TransformID < VansTransformStore::GlobalTransforms.size())
		{
			const Json& data = (*transformComponent)["data"];
			VansTransform& transform = VansTransformStore::GetTransform(obj->m_TransformID);
			glm::vec3 value;
			if (data.contains("position") && ReadVec3(data["position"], value))
				transform.m_Position = value;
			if (data.contains("rotation") && ReadTransformRotationEuler(data["rotation"], value))
				transform.m_Rotation = value;
			if (data.contains("scale") && ReadVec3(data["scale"], value))
				transform.m_Scale = value;

			VansTransformStore::TransformIDToTransformDirty[obj->m_TransformID] = true;
		}
	}

	auto applyDirectional = [&]()
	{
		const Json* component = FindComponent(entity, "DirectionalLight");
		auto* runtime = obj->GetComponent<VansScriptDirectionalLightComponent>();
		if (!component || !runtime || !runtime->m_LightManager || runtime->m_LightIndex < 0 ||
			!component->contains("data")) return;

		auto& lights = runtime->m_LightManager->GetDirectionLights();
		if (runtime->m_LightIndex >= static_cast<int>(lights.size())) return;
		const Json& data = (*component)["data"];
		ReadColor3(data, lights[runtime->m_LightIndex].m_Color);
		lights[runtime->m_LightIndex].m_Intensity = data.value("intensity", lights[runtime->m_LightIndex].m_Intensity);
	};

	auto applyPoint = [&]()
	{
		const Json* component = FindComponent(entity, "PointLight");
		auto* runtime = obj->GetComponent<VansScriptPointLightComponent>();
		if (!component || !runtime || !runtime->m_LightManager || runtime->m_LightIndex < 0 ||
			!component->contains("data")) return;

		auto& lights = runtime->m_LightManager->GetPointLights();
		if (runtime->m_LightIndex >= static_cast<int>(lights.size())) return;
		const Json& data = (*component)["data"];
		ReadColor3(data, lights[runtime->m_LightIndex].m_Color);
		lights[runtime->m_LightIndex].m_Intensity = data.value("intensity", lights[runtime->m_LightIndex].m_Intensity);
		lights[runtime->m_LightIndex].m_Radius = data.value("radius", lights[runtime->m_LightIndex].m_Radius);
	};

	auto applySpot = [&]()
	{
		const Json* component = FindComponent(entity, "SpotLight");
		auto* runtime = obj->GetComponent<VansScriptSpotLightComponent>();
		if (!component || !runtime || !runtime->m_LightManager || runtime->m_LightIndex < 0 ||
			!component->contains("data")) return;

		auto& lights = runtime->m_LightManager->GetSpotLight();
		if (runtime->m_LightIndex >= static_cast<int>(lights.size())) return;
		const Json& data = (*component)["data"];
		auto& light = lights[runtime->m_LightIndex];
		ReadColor3(data, light.m_Color);
		light.m_Intensity = data.value("intensity", light.m_Intensity);
		light.m_Radius = data.value("radius", light.m_Radius);
		light.m_InnerCutOff = glm::radians(data.value("innercutoff", glm::degrees(light.m_InnerCutOff)));
		light.m_OuterCutOff = glm::radians(data.value("outerCutoff", glm::degrees(light.m_OuterCutOff)));
	};

	auto applyRect = [&]()
	{
		const Json* component = FindComponent(entity, "RectLight");
		auto* runtime = obj->GetComponent<VansScriptRectLightComponent>();
		if (!component || !runtime || !runtime->m_LightManager || runtime->m_LightIndex < 0 ||
			!component->contains("data")) return;

		auto& lights = runtime->m_LightManager->GetRectLights();
		if (runtime->m_LightIndex >= static_cast<int>(lights.size())) return;
		const Json& data = (*component)["data"];
		auto& light = lights[runtime->m_LightIndex];
		ReadColor3(data, light.m_Color);
		light.m_Intensity = data.value("intensity", light.m_Intensity);
		light.m_HalfWidth = data.value("width", light.m_HalfWidth * 2.0f) * 0.5f;
		light.m_HalfHeight = data.value("height", light.m_HalfHeight * 2.0f) * 0.5f;
		light.m_Range = data.value("range", light.m_Range);
		light.m_TwoSided = data.value("two_sided", light.m_TwoSided != 0.0f) ? 1.0f : 0.0f;
		light.m_ShadowIndex = data.value("shadow", light.m_ShadowIndex >= 0.0f) ? 0.0f : -1.0f;
	};

	applyDirectional();
	applyPoint();
	applySpot();
	applyRect();
}

void VansInspectorLiveEditService::ApplyComponentEnabledFromPointer(
	const std::string& selectedGuid, const std::string& jsonPointer, bool enabled)
{
	if (selectedGuid.empty() || !m_Scene || !m_Document) return;

	VansScriptObject* obj = m_Scene->FindObjectByGuid(selectedGuid);
	if (!obj) return;

	const auto& root = m_Document->Root();
	std::string jsonType;
	try
	{
		jsonType = root.at(JsonPointer(jsonPointer + "/type")).get<std::string>();
	}
	catch (const Json::exception&)
	{
		return;
	}

	static const std::unordered_map<std::string, std::string> kTypeToRuntime = {
		{"ModelRenderer",       "render"},
		{"Physics",             "physics"},
		{"Camera",              "camera"},
		{"Cloth",               "cloth"},
		{"Vehicle",             "vehicle"},
		{"Animator",            "animation"},
		{"CharacterController", "CharacterController"},
	};

	std::string runtimeName = jsonType;
	auto mapIt = kTypeToRuntime.find(jsonType);
	if (mapIt != kTypeToRuntime.end())
		runtimeName = mapIt->second;

	for (auto* comp : obj->m_Components)
	{
		if (comp && comp->m_ComponentName == runtimeName)
		{
			comp->SetEnabled(enabled);
			return;
		}
	}
}
}
