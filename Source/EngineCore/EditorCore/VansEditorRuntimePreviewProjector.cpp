#include "VansEditorRuntimePreviewProjector.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../MaterialCore/VansClothMaterialParameters.h"
#include "../MaterialCore/VansTreeLeafMaterialParameters.h"
#include "../SceneCore/VansSceneAuthoringEntityProjection.h"
#include "../SceneCore/VansSceneRuntimeProjection.h"
#include "../SceneCore/VansSceneDocument.h"
#include "../SceneCore/VansSceneParentReference.h"
#include "../EngineAPILayer/Public/ISceneInteractionEditorAPI.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#include <../../GLM/gtx/quaternion.hpp>

#include <cstdint>
#include <algorithm>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Vans
{
namespace
{
bool AppendRuntimeLightPreview(
	const std::string& entityGuid,
	const VansSceneAuthoringLightProjection& projection,
    EditorAPI::RuntimePreviewLightType lightType,
    std::vector<EditorAPI::RuntimeLightEdit>& edits)
{
    EditorAPI::RuntimeLightEdit edit;
    edit.type = lightType;
    edit.entityGuid = entityGuid;
    if (edit.entityGuid.empty())
        return false;
	const auto applyCookie = [&](const VansSceneLightCookieConfig& cookie)
    {
		edit.writeCookie = projection.hasCookie;
		edit.cookie.enabled = cookie.enabled;
		edit.cookie.textureGuid = cookie.textureGuid;
		edit.cookie.strength = cookie.strength;
		edit.cookie.sizeX = cookie.sizeX;
		edit.cookie.sizeY = cookie.sizeY;
		edit.cookie.scaleX = cookie.scaleX;
		edit.cookie.scaleY = cookie.scaleY;
		edit.cookie.offsetX = cookie.offsetX;
		edit.cookie.offsetY = cookie.offsetY;
		edit.cookie.rotationDegrees = cookie.rotationDegrees;
		edit.cookie.repeat = cookie.repeat;
		edit.cookie.useAlpha = cookie.useAlpha;
	};
	const auto applyCommon = [&](const auto& config)
	{
		applyCookie(config.cookie);
		if (config.color) { edit.color = {(*config.color)[0], (*config.color)[1], (*config.color)[2]}; edit.writeColor = true; }
		if (config.intensity) { edit.intensity = *config.intensity; edit.writeIntensity = true; }
	};
	std::visit([&](const auto& config)
	{
		applyCommon(config);
		using T = std::decay_t<decltype(config)>;
		if constexpr (std::is_same_v<T, VansScenePointLightComponentConfig> ||
			std::is_same_v<T, VansSceneSpotLightComponentConfig>)
			if (config.radius) { edit.radius = *config.radius; edit.writeRadius = true; }
		if constexpr (std::is_same_v<T, VansSceneSpotLightComponentConfig>)
		{
			if (config.innerCutoffDegrees) { edit.innerCutoffRadians = glm::radians(*config.innerCutoffDegrees); edit.writeInnerCutoff = true; }
			if (config.outerCutoffDegrees) { edit.outerCutoffRadians = glm::radians(*config.outerCutoffDegrees); edit.writeOuterCutoff = true; }
		}
		if constexpr (std::is_same_v<T, VansSceneRectLightComponentConfig>)
		{
			if (config.width) { edit.rectWidth = *config.width; edit.writeRectWidth = true; }
			if (config.height) { edit.rectHeight = *config.height; edit.writeRectHeight = true; }
			if (config.range) { edit.rectRange = *config.range; edit.writeRectRange = true; }
			if (config.twoSided) { edit.rectTwoSided = *config.twoSided ? 1.0f : 0.0f; edit.writeRectTwoSided = true; }
			if (config.shadow.castShadows) { edit.rectShadowIndex = *config.shadow.castShadows ? 0.0f : -1.0f; edit.writeRectShadow = true; }
		}
	}, projection.config);

    if (edit.writeCookie || edit.writeColor || edit.writeIntensity || edit.writeRadius ||
        edit.writeInnerCutoff || edit.writeOuterCutoff ||
        edit.writeRectWidth || edit.writeRectHeight || edit.writeRectRange ||
        edit.writeRectTwoSided || edit.writeRectShadow)
    {
        edits.push_back(edit);
        return true;
    }
    return false;
}

const VansSerializedValue& UnwrapMaterialParameterValue(const VansSerializedValue& value)
{
    if (const VansSerializedValue* wrappedValue = FindObjectField(value, "value"))
        return *wrappedValue;
    if (const VansSerializedValue* defaultValue = FindObjectField(value, "default"))
        return *defaultValue;
    return value;
}

EditorAPI::PropertyValue ToMaterialPropertyValue(const VansSerializedValue& rawValue)
{
    const VansSerializedValue& value = UnwrapMaterialParameterValue(rawValue);
    switch (value.kind)
    {
    case VansSerializedValue::Kind::Bool:
        return value.boolValue;
    case VansSerializedValue::Kind::Int:
        if (value.intValue >= static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::min)()) &&
            value.intValue <= static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::max)()))
        {
            return static_cast<std::int32_t>(value.intValue);
        }
        if (value.intValue >= 0 &&
            value.intValue <= static_cast<std::int64_t>((std::numeric_limits<std::uint32_t>::max)()))
        {
            return static_cast<std::uint32_t>(value.intValue);
        }
        break;
    case VansSerializedValue::Kind::Float:
        return static_cast<float>(value.floatValue);
    case VansSerializedValue::Kind::String:
        return value.stringValue;
    case VansSerializedValue::Kind::Array:
    {
        const auto read = [&](std::size_t index) -> float
        {
            const VansSerializedValue* item = FindArrayItem(value, index);
            if (!item)
                return 0.0f;
            return static_cast<float>(ReadSerializedNumber(*item));
        };
        if (value.arrayItems.size() >= 4)
            return EditorAPI::Vec4{ read(0), read(1), read(2), read(3) };
        if (value.arrayItems.size() == 3)
            return EditorAPI::Vec3{ read(0), read(1), read(2) };
        if (value.arrayItems.size() == 2)
            return EditorAPI::Vec2{ read(0), read(1) };
        break;
    }
    case VansSerializedValue::Kind::Null:
    case VansSerializedValue::Kind::Object:
    default:
        break;
    }
    return std::monostate{};
}

void AppendMaterialPreviewParameter(
    EditorAPI::RuntimeMaterialPreviewChange& change,
    std::string parameterPath,
    const VansSerializedValue& value)
{
    EditorAPI::PropertyValue typedValue = ToMaterialPropertyValue(value);
    if (std::holds_alternative<std::monostate>(typedValue))
        return;
    change.parameters.push_back({
        std::move(parameterPath),
        std::move(typedValue)
    });
}

std::string ReadAssetReferenceGuid(const VansSerializedValue& value)
{
    if (value.kind == VansSerializedValue::Kind::String)
        return value.stringValue;
    if (value.kind == VansSerializedValue::Kind::Object)
        return ReadSerializedStringField(value, "guid");
    return {};
}

const VansSerializedValue* UnwrapTextureReference(const VansSerializedValue& value)
{
    if (value.kind == VansSerializedValue::Kind::Object)
    {
        if (const VansSerializedValue* texture = FindObjectField(value, "texture"))
            return texture;
        if (const VansSerializedValue* unwrapped = FindObjectField(value, "value"))
            return unwrapped;
    }
    return &value;
}

void AppendMaterialPreviewTexture(
    EditorAPI::RuntimeMaterialPreviewChange& change,
    std::string slot,
    const VansSerializedValue& value)
{
    const VansSerializedValue* reference = UnwrapTextureReference(value);
    if (!reference || slot.empty())
        return;
    change.textures.push_back({
        std::move(slot),
        ReadAssetReferenceGuid(*reference)
    });
}

void AppendTextureObjectPreviewEdits(
    EditorAPI::RuntimeMaterialPreviewChange& change,
    const VansSerializedValue& textures,
    const std::string& changedSlot = {})
{
    if (textures.kind != VansSerializedValue::Kind::Object)
        return;
    for (const auto& [slot, value] : textures.objectFields)
    {
        if (!changedSlot.empty() && slot != changedSlot)
            continue;
        AppendMaterialPreviewTexture(change, slot, value);
    }
}

void AppendTransparentTextureArrayPreviewEdits(
    EditorAPI::RuntimeMaterialPreviewChange& change,
    const VansSerializedValue& textures,
    const std::vector<std::string>& pointerTokens)
{
    if (textures.kind != VansSerializedValue::Kind::Array)
        return;

    std::size_t onlyIndex = textures.arrayItems.size();
    if (pointerTokens.size() >= 4 && pointerTokens[1] == "textures")
    {
        try
        {
            onlyIndex = static_cast<std::size_t>(std::stoull(pointerTokens[2]));
        }
        catch (...)
        {
            onlyIndex = textures.arrayItems.size();
        }
    }

    for (std::size_t i = 0; i < textures.arrayItems.size(); ++i)
    {
        if (onlyIndex < textures.arrayItems.size() && i != onlyIndex)
            continue;
        const VansSerializedValue& entry = textures.arrayItems[i];
        const std::string slot = ReadSerializedStringField(entry, "slot");
        if (const VansSerializedValue* texture = FindObjectField(entry, "texture"))
            AppendMaterialPreviewTexture(change, slot, *texture);
    }
}

EditorAPI::RuntimeEntityPreviewChange TranslateRuntimeEntityPreview(
	const VansSceneAuthoringEntityProjection& projection)
{
	EditorAPI::RuntimeEntityPreviewChange change;
	change.nameEdits.push_back({ projection.entityGuid, projection.name });
	change.activeEdits.push_back({ projection.entityGuid, projection.active });
	for (const VansSceneAuthoringComponentProjection& component : projection.components)
	{
		if (component.guid.empty()) continue;
		change.componentEnabled.push_back({
			projection.entityGuid, component.guid, component.type, component.enabled });
		if (component.localVolumetricFogComponent)
			change.localVolumetricFogEdits.push_back({
				projection.entityGuid, component.guid, *component.localVolumetricFogComponent });
		if(component.navigationData) change.navigationEdits.push_back({
			projection.entityGuid,component.guid,component.type,*component.navigationData});
	}
	if (projection.transform)
	{
		change.hasTransform = true;
		change.transform.entityGuid = projection.entityGuid;
		change.transform.space = EditorAPI::RuntimeTransformSpace::Local;
		const auto toVec3 = [](const std::array<float, 3>& value)
		{ return EditorAPI::Vec3{ value[0], value[1], value[2] }; };
		if (projection.transform->position)
		{
			change.transform.position = toVec3(*projection.transform->position);
			change.transform.writePosition = true;
		}
		if (projection.transform->rotationDegrees)
		{
			change.transform.rotationDegrees = toVec3(*projection.transform->rotationDegrees);
			change.transform.writeRotation = true;
		}
		if (projection.transform->scale)
		{
			change.transform.scale = toVec3(*projection.transform->scale);
			change.transform.writeScale = true;
		}
	}
	for (const VansSceneAuthoringLightProjection& light : projection.lights)
	{
		EditorAPI::RuntimePreviewLightType type;
		if (light.componentType == "DirectionalLight")
			type = EditorAPI::RuntimePreviewLightType::Directional;
		else if (light.componentType == "PointLight")
			type = EditorAPI::RuntimePreviewLightType::Point;
		else if (light.componentType == "SpotLight")
			type = EditorAPI::RuntimePreviewLightType::Spot;
		else if (light.componentType == "RectLight")
			type = EditorAPI::RuntimePreviewLightType::Rect;
		else
			continue;
		AppendRuntimeLightPreview(projection.entityGuid, light, type, change.lights);
	}
	for (const VansSceneAuthoringMaterialOverrideProjection& overrideValue :
		projection.materialOverrides)
		change.materialOverrides.push_back({
			projection.entityGuid, overrideValue.slot, overrideValue.materialGuid });
	return change;
}
}

EditorAPI::RuntimeEntityPreviewChange BuildRuntimeEntityPreviewChange(
    const VansSerializedValue& entity)
{
	VansSceneAuthoringEntityProjection projection;
	return VansSceneRuntimeProjection::ProjectAuthoringEntity(entity, projection)
		? TranslateRuntimeEntityPreview(projection)
		: EditorAPI::RuntimeEntityPreviewChange{};
}

EditorAPI::RuntimeEntityPreviewChange BuildRuntimeEntityHierarchyPreviewChange(const VansSerializedValue& entity)
{
    const auto preview = BuildRuntimeEntityPreviewChange(entity);
    EditorAPI::RuntimeEntityPreviewChange change;
    change.hasTransform = preview.hasTransform;
    change.transform = preview.transform;
    EditorAPI::RuntimeEntityParentEdit parent;
    parent.entityGuid = ReadSerializedStringField(entity, "id");
    parent.transformPolicy = EditorAPI::RuntimeReparentTransformPolicy::KeepWorld;
    const auto* value = FindObjectField(entity, "parent");
    if (value && !value->IsNull())
    {
        VansSceneParentReference reference;
        std::string error;
        if (!TryReadSceneParentReference(*value, reference, error)) return {};
        parent.parent.kind = reference.kind == VansSceneParentKind::Entity ? EditorAPI::RuntimeParentKind::Entity :
            reference.kind == VansSceneParentKind::Bone ? EditorAPI::RuntimeParentKind::Bone : EditorAPI::RuntimeParentKind::Socket;
        parent.parent.entityGuid = reference.entityGuid.ToString();
        if (reference.IsAnchor())
        {
            parent.parent.animationComponentGuid = reference.animationComponentGuid.ToString();
            parent.parent.anchorGuid = reference.anchorGuid.ToString();
            parent.parent.poseCheckpoint = reference.poseCheckpoint;
        }
    }
    change.parentEdits.push_back(std::move(parent));
    return change;
}

bool RequiresRuntimeEntityRebuild(const VansSerializedValue& before, const VansSerializedValue& after)
{
    // Incremental writes do not reset omitted fields to construction defaults.
    const auto removedField = [](const auto& self, const VansSerializedValue& oldValue,
        const VansSerializedValue& newValue) -> bool
    {
        if (oldValue.kind == VansSerializedValue::Kind::Object)
            for (const auto& [key, value] : oldValue.objectFields)
            {
                const auto* next = FindObjectField(newValue, key);
                if (!next || self(self, value, *next)) return true;
            }
        if (oldValue.kind == VansSerializedValue::Kind::Array)
        {
            if (newValue.kind != VansSerializedValue::Kind::Array || oldValue.arrayItems.size() > newValue.arrayItems.size()) return true;
            for (std::size_t i = 0; i < oldValue.arrayItems.size(); ++i)
                if (self(self, oldValue.arrayItems[i], newValue.arrayItems[i])) return true;
        }
        return false;
    };
    if (removedField(removedField, before, after)) return true;
    VansSceneAuthoringEntityProjection oldProjection, newProjection;
    if (!VansSceneRuntimeProjection::ProjectAuthoringEntity(before, oldProjection) ||
        !VansSceneRuntimeProjection::ProjectAuthoringEntity(after, newProjection) ||
        oldProjection.entityGuid != newProjection.entityGuid ||
        oldProjection.components.size() != newProjection.components.size()) return true;
    for (std::size_t i = 0; i < oldProjection.components.size(); ++i)
        if (!SerializedValuesEqual(oldProjection.components[i].unprojectedData, newProjection.components[i].unprojectedData))
            return true;
    // Clearing an override needs reconstruction; the incremental material API only assigns slots.
    for (const auto& oldOverride : oldProjection.materialOverrides)
        if (std::none_of(newProjection.materialOverrides.begin(), newProjection.materialOverrides.end(),
            [&](const auto& value) { return value.slot == oldOverride.slot; })) return true;
    auto oldFields = before, newFields = after;
    for (const char* field : {"name", "active", "components"})
    {
        EraseSerializedObjectField(oldFields, field);
        EraseSerializedObjectField(newFields, field);
    }
    return !SerializedValuesEqual(oldFields, newFields);
}

bool RestoreRuntimeChildHierarchy(EditorAPI::ISceneInteractionEditorAPI& api,
    const VansSceneDocument& document, const std::string& parentGuid)
{
    const auto snapshot = document.CreateSnapshot();
    const auto* entities = FindObjectField(snapshot.Root(), "entities");
    if (!entities || entities->kind != VansSerializedValue::Kind::Array) return false;
    for (const auto& entity : entities->arrayItems)
    {
        const auto* parent = FindObjectField(entity, "parent");
        if (!parent || ReadSceneParentEntityGuid(*parent) != parentGuid) continue;
        const auto change = BuildRuntimeEntityHierarchyPreviewChange(entity);
        if (change.Empty() || !api.ApplyRuntimeEntityPreviewChange(change)) return false;
    }
    return true;
}

EditorAPI::RuntimeEntityPreviewChange BuildRuntimeEntityPreviewChangeFromSceneRoot(
    const VansSerializedValue& sceneRoot,
    const std::string& entityGuid)
{
	VansSceneAuthoringEntityProjection projection;
	return VansSceneRuntimeProjection::ProjectAuthoringEntityFromSceneRoot(
		sceneRoot, entityGuid, projection)
		? TranslateRuntimeEntityPreview(projection)
		: EditorAPI::RuntimeEntityPreviewChange{};
}

EditorAPI::RuntimeMaterialPreviewChange BuildRuntimeMaterialPreviewChange(
    const std::filesystem::path& assetPath,
    const VansSerializedValue& assetRoot,
    const std::string& changedPointer)
{
    EditorAPI::RuntimeMaterialPreviewChange change;
    change.assetPath = assetPath.string();
    const std::vector<std::string> tokens = SplitSerializedPointer(changedPointer);
    const auto materialType = ReadSerializedStringField(assetRoot, "materialType");
    const auto* authorParameters = FindObjectField(assetRoot, "parameters");
    const auto* authorLeaf = authorParameters ? FindObjectField(*authorParameters, "leaf") : nullptr;
    std::optional<VansSerializedValue> leafParameters;
    if (materialType == "tree" || materialType == "pbr")
    {
        VansTreeLeafMaterialParameters parsed;
        std::string error;
        if (authorLeaf && !ReadTreeLeafMaterialParameters(*authorLeaf, parsed, error)) return change;
        leafParameters = WriteTreeLeafMaterialParameters(parsed);
    }
    const auto appendLeaf = [&](const std::string& onlyName = {})
    {
        if (!leafParameters) return;
        for (const auto& [name, value] : leafParameters->objectFields)
            if (onlyName.empty() || onlyName == name)
                AppendMaterialPreviewParameter(change, "leaf/" + name, value);
    };
    if (ReadSerializedStringField(assetRoot, "materialType") == "cloth")
    {
        const auto* sourceParameters = FindObjectField(assetRoot, "parameters");
        VansClothMaterialParameters parsed;
        std::string error;
        if (!sourceParameters || !ReadClothMaterialParameters(*sourceParameters, parsed, error))
            return change;
        const auto parameters = WriteClothMaterialParameters(parsed);
        const bool complete = tokens.empty() || (tokens.size() == 1 && tokens[0] == "asset");
        const bool parameterEdit = tokens.size() >= 2 && tokens[0] == "asset" && tokens[1] == "parameters";
        const bool textureEdit = tokens.size() >= 2 && tokens[0] == "asset" && tokens[1] == "textures";
        if (complete || parameterEdit)
        {
            if (parameterEdit && tokens.size() >= 3)
            {
                if (const auto* value = FindObjectField(parameters, tokens[2]))
                    AppendMaterialPreviewParameter(change, tokens[2], *value);
            }
            else
                for (const auto& [name, value] : parameters.objectFields)
                    AppendMaterialPreviewParameter(change, name, value);
        }
        if (complete || textureEdit)
        {
            const auto* textures = FindObjectField(assetRoot, "textures");
            for (const char* slot : VansClothTextureSlots)
            {
                if (textureEdit && tokens.size() >= 3 && tokens[2] != slot) continue;
                const auto* reference = textures ? FindObjectField(*textures, slot) : nullptr;
                // History restores a source snapshot, where an absent slot
                // means clear. It must not retain the last preview texture.
                change.textures.push_back({slot, reference ? ReadAssetReferenceGuid(*reference) : std::string{}});
            }
        }
        return change;
    }
    const std::string group = tokens.size() >= 2 && tokens[0] == "asset" ? tokens[1] : std::string{};
    const bool complete = group != "parameters" && group != "customParameters" &&
        group != "textures" && group != "customTextures";
    const std::string field = !complete && tokens.size() >= 3 ? tokens[2] : std::string{};
    // Inspector publishes the edited top-level group. Keep that scope so a
    // parameter edit never submits unchanged textures to the render thread.
    if (complete || group == "parameters")
    {
        if (field == "leaf" && leafParameters)
        {
            appendLeaf(tokens.size() >= 4 ? tokens[3] : std::string{});
        }
        else
        {
            const VansSerializedValue* parameters = FindObjectField(assetRoot, "parameters");
            if (!parameters && assetRoot.kind == VansSerializedValue::Kind::Object)
                parameters = &assetRoot;
            if (parameters && parameters->kind == VansSerializedValue::Kind::Object)
                for (const auto& [name, value] : parameters->objectFields)
                    if ((field.empty() || field == name) && (name != "leaf" || !leafParameters))
                        AppendMaterialPreviewParameter(change, name, value);
            // Group/full restoration resets an omitted leaf block to load defaults.
            if (field.empty()) appendLeaf();
        }
    }
    if (complete || group == "customParameters")
    {
        if (const auto* customParameters = FindObjectField(assetRoot, "customParameters"))
            if (customParameters->kind == VansSerializedValue::Kind::Object)
                for (const auto& [name, value] : customParameters->objectFields)
                    if (field.empty() || field == name)
                        AppendMaterialPreviewParameter(change, "customParameters/" + name, value);
    }
    if (complete || group == "textures")
    {
        if (const auto* textures = FindObjectField(assetRoot, "textures"))
        {
            AppendTextureObjectPreviewEdits(change, *textures, field);
            AppendTransparentTextureArrayPreviewEdits(change, *textures, tokens);
        }
    }
    if (complete || group == "customTextures")
    {
        if (const auto* customTextures = FindObjectField(assetRoot, "customTextures"))
            AppendTextureObjectPreviewEdits(change, *customTextures, field);
    }
    return change;
}
}
