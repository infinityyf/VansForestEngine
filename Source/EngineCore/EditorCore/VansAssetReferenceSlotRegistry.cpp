#include "VansAssetReferenceSlotRegistry.h"

#include <algorithm>
#include <cctype>

namespace Vans
{
namespace
{
std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
}

AssetReferenceSlotDescriptor VansAssetReferenceSlotRegistry::Resolve(
    const std::string& componentType,
    const std::string& parentKey,
    const std::string& fieldKey)
{
    const std::string field = Lower(fieldKey);
    const std::string parent = Lower(parentKey);
    const std::string component = Lower(componentType);

    AssetReferenceSlotDescriptor descriptor;
    if (field == "model" || field.find("mesh") != std::string::npos)
        descriptor.expectedType = EditorAPI::AssetType::Model;
    else if (field.find("material") != std::string::npos ||
        parent.find("materialoverride") != std::string::npos)
        descriptor.expectedType = EditorAPI::AssetType::Material;
    else if (field == "shader" || field.find("shader") != std::string::npos ||
        parent.find("shader") != std::string::npos)
        descriptor.expectedType = EditorAPI::AssetType::Shader;
    else if (field.find("texture") != std::string::npos || parent == "textures" ||
        parent.find("textures") != std::string::npos ||
        field == "basecolor" || field == "normal" || field == "metal" ||
        field == "roughness" || field == "ao")
        descriptor.expectedType = EditorAPI::AssetType::Texture;
    else if (field == "source" && component == "audio")
        descriptor.expectedType = EditorAPI::AssetType::Audio;
    else if (field == "source" && component == "video")
        descriptor.expectedType = EditorAPI::AssetType::Video;
    else if ((field == "asset" && component == "particle") ||
        field.find("particle") != std::string::npos)
        descriptor.expectedType = EditorAPI::AssetType::Particle;
    else if (field == "animator" || field.find("animator") != std::string::npos)
        descriptor.expectedType = EditorAPI::AssetType::AnimatorController;
    else if (field.find("clip") != std::string::npos && component == "animation")
        descriptor.expectedType = EditorAPI::AssetType::AnimationClip;
    else if (field == "profilepath" && component == "cloth")
        descriptor.expectedType = EditorAPI::AssetType::ClothProfile;
    else if (field.find("ragdoll") != std::string::npos)
        descriptor.expectedType = EditorAPI::AssetType::RagdollProfile;

    return descriptor;
}
}
