#include "VansEditorRuntimePreviewProjector.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../SceneCore/VansSceneRuntimeProjection.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#include <../../GLM/gtx/quaternion.hpp>

#include <cstdint>
#include <limits>
#include <utility>
#include <variant>
#include <vector>

namespace Vans
{
namespace
{
const VansSerializedValue* FindSerializedComponent(
    const VansSerializedValue& entity,
    const std::string& type)
{
    const VansSerializedValue* components = FindObjectField(entity, "components");
    if (!components || components->kind != VansSerializedValue::Kind::Array)
        return nullptr;
    for (const VansSerializedValue& component : components->arrayItems)
        if (ReadSerializedStringField(component, "type") == type)
            return &component;
    return nullptr;
}

bool ReadPreviewVec3(const VansSerializedValue& value, EditorAPI::Vec3& out)
{
    if (value.kind != VansSerializedValue::Kind::Array || value.arrayItems.size() < 3)
        return false;
    out = {
        static_cast<float>(ReadSerializedNumber(value.arrayItems[0])),
        static_cast<float>(ReadSerializedNumber(value.arrayItems[1])),
        static_cast<float>(ReadSerializedNumber(value.arrayItems[2]))
    };
    return true;
}

bool ReadPreviewRotationEuler(
	const VansSerializedValue& value,
	bool yawOnly,
	EditorAPI::Vec3& out)
{
    if (value.kind != VansSerializedValue::Kind::Array)
        return false;

    if (value.arrayItems.size() == 4)
    {
		const std::array<float, 3> euler = ProjectSceneQuaternionToEulerDegrees({
			static_cast<float>(ReadSerializedNumber(value.arrayItems[0])),
			static_cast<float>(ReadSerializedNumber(value.arrayItems[1])),
			static_cast<float>(ReadSerializedNumber(value.arrayItems[2])),
			static_cast<float>(ReadSerializedNumber(value.arrayItems[3])) }, yawOnly);
		out = { euler[0], euler[1], euler[2] };
        return true;
    }

    return ReadPreviewVec3(value, out);
}

bool BuildRuntimeTransformPreview(
    const VansSerializedValue& entity,
    EditorAPI::RuntimeTransformEdit& edit)
{
    const std::string entityGuid = ReadSerializedStringField(entity, "id");
    if (entityGuid.empty())
        return false;

    const VansSerializedValue* transformComponent = FindSerializedComponent(entity, "Transform");
    if (!transformComponent || !ReadSerializedBoolField(*transformComponent, "enabled", true))
        return false;

	const VansSerializedValue* data = FindObjectField(*transformComponent, "data");
	if (!data)
		return false;
	bool yawOnly = false;
	if (const VansSerializedValue* aiComponent = FindSerializedComponent(entity, "AIAgent"))
	{
		if (const VansSerializedValue* aiData = FindObjectField(*aiComponent, "data");
			aiData && aiData->kind == VansSerializedValue::Kind::Object)
		{
			if (const VansSerializedValue* facing = FindObjectField(*aiData, "facing");
				facing && facing->kind == VansSerializedValue::Kind::Object)
				yawOnly = ReadSerializedBoolField(*facing, "yawOnly", false);
		}
	}

    edit = {};
    edit.entityGuid = entityGuid;
	edit.space = EditorAPI::RuntimeTransformSpace::Local;

    if (const VansSerializedValue* position = FindObjectField(*data, "position");
        position && ReadPreviewVec3(*position, edit.position))
    {
        edit.writePosition = true;
    }
	if (const VansSerializedValue* rotation = FindObjectField(*data, "rotation");
		rotation && ReadPreviewRotationEuler(*rotation, yawOnly, edit.rotationDegrees))
    {
        edit.writeRotation = true;
    }
    if (const VansSerializedValue* scale = FindObjectField(*data, "scale");
        scale && ReadPreviewVec3(*scale, edit.scale))
    {
        edit.writeScale = true;
    }

    return edit.writePosition || edit.writeRotation || edit.writeScale;
}

bool ReadPreviewColor(const VansSerializedValue& data, EditorAPI::Vec3& out)
{
    const VansSerializedValue* color = FindObjectField(data, "color");
    return color && ReadPreviewVec3(*color, out);
}

bool AppendRuntimeLightPreview(
    const VansSerializedValue& entity,
    const char* componentType,
    EditorAPI::RuntimePreviewLightType lightType,
    std::vector<EditorAPI::RuntimeLightEdit>& edits)
{
    const VansSerializedValue* component = FindSerializedComponent(entity, componentType);
    if (!component || !ReadSerializedBoolField(*component, "enabled", true))
        return false;

    const VansSerializedValue* data = FindObjectField(*component, "data");
    if (!data)
        return false;

    EditorAPI::RuntimeLightEdit edit;
    edit.type = lightType;
    edit.entityGuid = ReadSerializedStringField(entity, "id");
    if (edit.entityGuid.empty())
        return false;

    if (ReadPreviewColor(*data, edit.color))
        edit.writeColor = true;
    if (const VansSerializedValue* intensity = FindObjectField(*data, "intensity"))
    {
        edit.intensity = static_cast<float>(ReadSerializedNumber(*intensity));
        edit.writeIntensity = true;
    }

    if (lightType == EditorAPI::RuntimePreviewLightType::Point ||
        lightType == EditorAPI::RuntimePreviewLightType::Spot)
    {
        if (const VansSerializedValue* radius = FindObjectField(*data, "radius"))
        {
            edit.radius = static_cast<float>(ReadSerializedNumber(*radius));
            edit.writeRadius = true;
        }
    }

    if (lightType == EditorAPI::RuntimePreviewLightType::Spot)
    {
        if (const VansSerializedValue* innerCutoff = FindObjectField(*data, "innercutoff"))
        {
            edit.innerCutoffRadians = glm::radians(static_cast<float>(ReadSerializedNumber(*innerCutoff)));
            edit.writeInnerCutoff = true;
        }
        if (const VansSerializedValue* outerCutoff = FindObjectField(*data, "outerCutoff"))
        {
            edit.outerCutoffRadians = glm::radians(static_cast<float>(ReadSerializedNumber(*outerCutoff)));
            edit.writeOuterCutoff = true;
        }
    }

    if (lightType == EditorAPI::RuntimePreviewLightType::Rect)
    {
        if (const VansSerializedValue* width = FindObjectField(*data, "width"))
        {
            edit.rectWidth = static_cast<float>(ReadSerializedNumber(*width));
            edit.writeRectWidth = true;
        }
        if (const VansSerializedValue* height = FindObjectField(*data, "height"))
        {
            edit.rectHeight = static_cast<float>(ReadSerializedNumber(*height));
            edit.writeRectHeight = true;
        }
        if (const VansSerializedValue* range = FindObjectField(*data, "range"))
        {
            edit.rectRange = static_cast<float>(ReadSerializedNumber(*range));
            edit.writeRectRange = true;
        }
        if (const VansSerializedValue* twoSided = FindObjectField(*data, "two_sided"))
        {
            edit.rectTwoSided = ReadSerializedBool(*twoSided) ? 1.0f : 0.0f;
            edit.writeRectTwoSided = true;
        }
        if (const VansSerializedValue* shadow = FindObjectField(*data, "shadow"))
        {
            edit.rectShadowIndex = ReadSerializedBool(*shadow) ? 0.0f : -1.0f;
            edit.writeRectShadow = true;
        }
    }

    if (edit.writeColor || edit.writeIntensity || edit.writeRadius ||
        edit.writeInnerCutoff || edit.writeOuterCutoff ||
        edit.writeRectWidth || edit.writeRectHeight || edit.writeRectRange ||
        edit.writeRectTwoSided || edit.writeRectShadow)
    {
        edits.push_back(edit);
        return true;
    }
    return false;
}

std::string ReadPreviewAssetGuid(const VansSerializedValue& reference)
{
    if (reference.kind == VansSerializedValue::Kind::String)
        return reference.stringValue;
    if (reference.kind == VansSerializedValue::Kind::Object)
        return ReadSerializedStringField(reference, "guid");
    return {};
}

void AppendRuntimeMaterialOverridePreviews(
    const VansSerializedValue& entity,
    std::vector<EditorAPI::RuntimeRendererMaterialOverrideEdit>& edits)
{
    const std::string entityGuid = ReadSerializedStringField(entity, "id");
    if (entityGuid.empty())
        return;

    const VansSerializedValue* renderer = FindSerializedComponent(entity, "ModelRenderer");
    if (!renderer || !ReadSerializedBoolField(*renderer, "enabled", true))
        return;

    const VansSerializedValue* data = FindObjectField(*renderer, "data");
    if (!data || data->kind != VansSerializedValue::Kind::Object)
        return;

    const auto appendOverrides = [&](const VansSerializedValue* overrides)
    {
        if (!overrides || overrides->kind != VansSerializedValue::Kind::Object)
            return;
        for (const auto& [slot, reference] : overrides->objectFields)
        {
            const std::string materialGuid = ReadPreviewAssetGuid(reference);
            if (materialGuid.empty())
                continue;
            edits.push_back({ entityGuid, slot, materialGuid });
        }
    };

    appendOverrides(FindObjectField(*data, "materialOverrides"));
    appendOverrides(FindObjectField(*data, "submeshMaterialOverrides"));
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
}

EditorAPI::RuntimeEntityPreviewChange BuildRuntimeEntityPreviewChange(
    const VansSerializedValue& entity)
{
    EditorAPI::RuntimeEntityPreviewChange change;
    const std::string entityGuid = ReadSerializedStringField(entity, "id");
    const std::string entityName = ReadSerializedStringField(entity, "name");
    if (!entityGuid.empty())
    {
        change.nameEdits.push_back({ entityGuid, entityName });
        change.activeEdits.push_back({ entityGuid, ReadSerializedBoolField(entity, "active", true) });
    }
    const VansSerializedValue* components = FindObjectField(entity, "components");
    if (components && components->kind == VansSerializedValue::Kind::Array)
    {
        for (const VansSerializedValue& component : components->arrayItems)
        {
            const std::string componentGuid = ReadSerializedStringField(component, "id");
            const std::string componentType = ReadSerializedStringField(component, "type");
            if (entityGuid.empty() || componentGuid.empty())
                continue;
            change.componentEnabled.push_back({
                entityGuid,
                componentGuid,
                componentType,
                ReadSerializedBoolField(component, "enabled", true)
            });
        }
    }

    change.hasTransform = BuildRuntimeTransformPreview(entity, change.transform);
    AppendRuntimeLightPreview(entity, "DirectionalLight",
        EditorAPI::RuntimePreviewLightType::Directional, change.lights);
    AppendRuntimeLightPreview(entity, "PointLight",
        EditorAPI::RuntimePreviewLightType::Point, change.lights);
    AppendRuntimeLightPreview(entity, "SpotLight",
        EditorAPI::RuntimePreviewLightType::Spot, change.lights);
    AppendRuntimeLightPreview(entity, "RectLight",
        EditorAPI::RuntimePreviewLightType::Rect, change.lights);
    AppendRuntimeMaterialOverridePreviews(entity, change.materialOverrides);
    return change;
}

EditorAPI::RuntimeEntityPreviewChange BuildRuntimeEntityPreviewChangeFromSceneRoot(
    const VansSerializedValue& sceneRoot,
    const std::string& entityGuid)
{
    if (entityGuid.empty())
        return {};
    const VansSerializedValue* entities = FindObjectField(sceneRoot, "entities");
    if (!entities || entities->kind != VansSerializedValue::Kind::Array)
        return {};
    for (const VansSerializedValue& entity : entities->arrayItems)
    {
        if (ReadSerializedStringField(entity, "id") == entityGuid)
            return BuildRuntimeEntityPreviewChange(entity);
    }
    return {};
}

EditorAPI::RuntimeMaterialPreviewChange BuildRuntimeMaterialPreviewChange(
    const std::filesystem::path& assetPath,
    const VansSerializedValue& assetRoot,
    const std::string& changedPointer)
{
    EditorAPI::RuntimeMaterialPreviewChange change;
    change.assetPath = assetPath.string();
    const std::vector<std::string> tokens = SplitSerializedPointer(changedPointer);
    if (tokens.size() >= 3 && tokens[0] == "asset")
    {
        if (tokens[1] == "parameters")
        {
            if (const VansSerializedValue* parameters = FindObjectField(assetRoot, "parameters"))
            {
                if (const VansSerializedValue* parameter = FindObjectField(*parameters, tokens[2]))
                {
                    AppendMaterialPreviewParameter(change, tokens[2], *parameter);
                    return change;
                }
            }
        }
        if (tokens[1] == "customParameters")
        {
            if (const VansSerializedValue* customParameters = FindObjectField(assetRoot, "customParameters"))
            {
                if (const VansSerializedValue* parameter = FindObjectField(*customParameters, tokens[2]))
                {
                    AppendMaterialPreviewParameter(
                        change,
                        "customParameters/" + tokens[2],
                        *parameter);
                    return change;
                }
            }
        }
        if (tokens[1] == "textures")
        {
            if (const VansSerializedValue* textures = FindObjectField(assetRoot, "textures"))
            {
                if (textures->kind == VansSerializedValue::Kind::Object && tokens.size() >= 3)
                {
                    AppendTextureObjectPreviewEdits(change, *textures, tokens[2]);
                    return change;
                }
                if (textures->kind == VansSerializedValue::Kind::Array)
                {
                    AppendTransparentTextureArrayPreviewEdits(change, *textures, tokens);
                    return change;
                }
            }
        }
        if (tokens[1] == "customTextures")
        {
            if (const VansSerializedValue* customTextures = FindObjectField(assetRoot, "customTextures"))
            {
                if (tokens.size() >= 3)
                {
                    AppendTextureObjectPreviewEdits(change, *customTextures, tokens[2]);
                    return change;
                }
            }
        }
    }

    const VansSerializedValue* parameters = FindObjectField(assetRoot, "parameters");
    if (!parameters && assetRoot.kind == VansSerializedValue::Kind::Object)
        parameters = &assetRoot;

    if (parameters && parameters->kind == VansSerializedValue::Kind::Object)
        for (const auto& [name, value] : parameters->objectFields)
            AppendMaterialPreviewParameter(change, name, value);

    if (const VansSerializedValue* customParameters = FindObjectField(assetRoot, "customParameters"))
    {
        if (customParameters->kind == VansSerializedValue::Kind::Object)
        {
            for (const auto& [name, value] : customParameters->objectFields)
            {
                AppendMaterialPreviewParameter(
                    change,
                    "customParameters/" + name,
                    value);
            }
        }
    }
    if (const VansSerializedValue* textures = FindObjectField(assetRoot, "textures"))
    {
        AppendTextureObjectPreviewEdits(change, *textures);
        AppendTransparentTextureArrayPreviewEdits(change, *textures, {});
    }
    if (const VansSerializedValue* customTextures = FindObjectField(assetRoot, "customTextures"))
    {
        AppendTextureObjectPreviewEdits(change, *customTextures);
    }
    return change;
}
}
