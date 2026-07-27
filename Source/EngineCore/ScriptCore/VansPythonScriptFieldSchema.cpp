#include "VansPythonScriptFieldSchema.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace Vans
{
namespace
{
bool MatchesAlias(const std::string& normalized, std::initializer_list<const char*> aliases)
{
    for (const char* alias : aliases)
    {
        if (normalized == NormalizePythonInspectorIdentifier(alias))
            return true;
    }
    return false;
}
}

std::string NormalizePythonInspectorIdentifier(std::string value)
{
    std::string normalized;
    normalized.reserve(value.size());
    for (unsigned char c : value)
    {
        if (std::isalnum(c))
            normalized.push_back(static_cast<char>(std::tolower(c)));
    }
    return normalized;
}

std::string CanonicalPythonInspectorAssetTypeName(const std::string& text)
{
    const std::string type = NormalizePythonInspectorIdentifier(text);
    if (MatchesAlias(type, { "model", "modelasset", "modelref", "modelassetref", "vansmodelasset", "vansmodelassetref", "vansmodelref", "vansassetmodelref" }))
        return "Model";
    if (MatchesAlias(type, { "texture", "textureasset", "textureref", "textureassetref", "vanstextureasset", "vanstextureassetref", "vanstextureref", "vansassettextureref" }))
        return "Texture";
    if (MatchesAlias(type, { "material", "materialasset", "materialref", "materialassetref", "vansmaterialasset", "vansmaterialassetref", "vansmaterialref", "vansassetmaterialref" }))
        return "Material";
    if (MatchesAlias(type, { "shader", "shaderasset", "shaderref", "shaderassetref", "vansshaderasset", "vansshaderassetref", "vansshaderref", "vansassetshaderref" }))
        return "Shader";
    if (MatchesAlias(type, { "audio", "audioasset", "audioref", "audioassetref", "vansaudioasset", "vansaudioassetref", "vansaudioref", "vansassetaudioref" }))
        return "Audio";
    if (MatchesAlias(type, { "video", "videoasset", "videoref", "videoassetref", "vansvideoasset", "vansvideoassetref", "vansvideoref", "vansassetvideoref" }))
        return "Video";
    if (MatchesAlias(type, { "scene", "sceneasset", "sceneref", "sceneassetref", "vanssceneasset", "vanssceneassetref", "vanssceneref", "vansassetsceneref" }))
        return "Scene";
    if (MatchesAlias(type, { "particle", "particleasset", "particleref", "particleassetref", "vansparticleasset", "vansparticleassetref", "vansparticleref", "vansassetparticleref" }))
        return "Particle";
    if (MatchesAlias(type, { "animationclip", "animationclipasset", "animationclipref", "animationclipassetref", "vansanimationclipasset", "vansanimationclipassetref", "vansanimationclipref", "vansassetanimationclipref" }))
        return "AnimationClip";
    if (MatchesAlias(type, { "animator", "animatorcontroller", "animatorcontrollerasset", "animatorcontrollerref", "animatorcontrollerassetref", "vansanimatorcontrollerasset", "vansanimatorcontrollerassetref", "vansanimatorcontrollerref", "vansassetanimatorcontrollerref" }))
        return "AnimatorController";
    if (MatchesAlias(type, { "clothprofile", "clothprofileasset", "clothprofileref", "clothprofileassetref", "vansclothprofileasset", "vansclothprofileassetref", "vansclothprofileref", "vansassetclothprofileref" }))
        return "ClothProfile";
    if (MatchesAlias(type, { "postprocessprofile", "postprocessprofileasset", "postprocessprofileref", "postprocessprofileassetref", "vanspostprocessprofileasset", "vanspostprocessprofileassetref", "vanspostprocessprofileref", "vansassetpostprocessprofileref" }))
        return "PostProcessProfile";
    if (MatchesAlias(type, { "ragdollprofile", "ragdollprofileasset", "ragdollprofileref", "ragdollprofileassetref", "vansragdollprofileasset", "vansragdollprofileassetref", "vansragdollprofileref", "vansassetragdollprofileref" }))
        return "RagdollProfile";
    return {};
}

std::string CanonicalPythonInspectorComponentTypeName(const std::string& text)
{
    const std::string type = NormalizePythonInspectorIdentifier(text);
    if (MatchesAlias(type, { "modelrenderer", "modelrenderercomponent", "rendercomponent", "vansrendercomp", "vansmodelrenderercomp" }))
        return "ModelRenderer";
    if (MatchesAlias(type, { "animation", "animationcomponent", "vansanimcomp", "vansanimationcomp" }))
        return "Animation";
    if (MatchesAlias(type, { "ragdoll", "ragdollcomponent", "vansragdollcomp" }))
        return "Ragdoll";
    if (MatchesAlias(type, { "charactercontroller", "charactercontrollercomponent", "cct", "cctcomponent", "vanscctcomp" }))
        return "CharacterController";
    if (MatchesAlias(type, { "vehicle", "vehiclecomponent", "vansvehiclecomp" }))
        return "Vehicle";
    if (MatchesAlias(type, { "directionallight", "directionallightcomponent", "dirlight", "dirlightcomponent", "vansdirlightcomp" }))
        return "DirectionalLight";
    if (MatchesAlias(type, { "pointlight", "pointlightcomponent", "vanspointlightcomp" }))
        return "PointLight";
    if (MatchesAlias(type, { "spotlight", "spotlightcomponent", "vansspotlightcomp" }))
        return "SpotLight";
    if (MatchesAlias(type, { "camera", "cameracomponent", "vanscameracomp" }))
        return "Camera";
    if (MatchesAlias(type, { "rectlight", "rectlightcomponent", "vansrectlightcomp" }))
        return "RectLight";
    if (MatchesAlias(type, { "audio", "audiocomponent", "vansaudiocomp" }))
        return "Audio";
    if (MatchesAlias(type, { "particle", "particlecomponent", "vansparticlecomp" }))
        return "Particle";
    if (MatchesAlias(type, { "video", "videocomponent", "vansvideocomp" }))
        return "Video";
    if (MatchesAlias(type, { "component", "componentref", "scenecomponent", "scenecomponentref", "vanscomponent", "vanscomponentref", "vansscenecomponent", "vansscenecomponentref" }))
        return "";
    return {};
}

bool IsPythonSceneEntityReferenceAnnotation(const std::string& text)
{
    return MatchesAlias(NormalizePythonInspectorIdentifier(text), {
        "entity",
        "entityref",
        "sceneentity",
        "sceneentityref",
        "vansobject",
        "vansobjectref",
        "vansentity",
        "vansentityref",
        "vanssceneentity",
        "vanssceneentityref"
    });
}

bool IsPythonSceneComponentReferenceAnnotation(const std::string& text)
{
    return MatchesAlias(NormalizePythonInspectorIdentifier(text), {
        "component",
        "componentref",
        "scenecomponent",
        "scenecomponentref",
        "vanscomponent",
        "vanscomponentref",
        "vansscenecomponent",
        "vansscenecomponentref"
    });
}

bool IsPythonProjectAssetReferenceAnnotation(const std::string& text)
{
    return MatchesAlias(NormalizePythonInspectorIdentifier(text), {
        "asset",
        "assetref",
        "projectasset",
        "projectassetref",
        "vansasset",
        "vansassetref",
        "vansprojectasset",
        "vansprojectassetref"
    });
}
}
