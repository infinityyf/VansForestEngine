#include "VansLightWindow.h"
#include "../VansEditorWindow.h"
#include "../../EngineAPILayer/Public/IEngineEditorAPI.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace
{
    enum class SceneSettingsGroup : uint32_t
    {
        None = 0,
		PhysicalAtmosphere = 1u << 0,
		HeightFog = 1u << 1,
		Clouds = 1u << 2
    };

    bool g_CommandMergeBoundaryReached = false;
    SceneSettingsGroup g_ActiveSceneSettingsGroup = SceneSettingsGroup::None;
    uint32_t g_SceneSettingsCommitMask = 0;

    void TrackCommandMergeBoundary()
    {
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            g_CommandMergeBoundaryReached = true;
            g_SceneSettingsCommitMask |= static_cast<uint32_t>(g_ActiveSceneSettingsGroup);
        }
    }

    bool ConsumeSceneSettingsCommit(SceneSettingsGroup group)
    {
        const uint32_t bit = static_cast<uint32_t>(group);
        if ((g_SceneSettingsCommitMask & bit) == 0)
            return false;
        g_SceneSettingsCommitMask &= ~bit;
        return true;
    }

    bool DragFloatTracked(
        const char* label,
        float* value,
        float speed,
        float minValue,
        float maxValue,
        const char* format)
    {
        const bool changed = ImGui::DragFloat(label, value, speed, minValue, maxValue, format);
        TrackCommandMergeBoundary();
        return changed;
    }

    bool DragFloat3Tracked(
        const char* label,
        float value[3],
        float speed,
        float minValue,
        float maxValue,
        const char* format)
    {
        const bool changed = ImGui::DragFloat3(label, value, speed, minValue, maxValue, format);
        TrackCommandMergeBoundary();
        return changed;
    }

    bool InputFloatTracked(const char* label, float* value)
    {
        const bool changed = ImGui::InputFloat(label, value);
        TrackCommandMergeBoundary();
        return changed;
    }

    float Dot(const Vans::EditorAPI::Vec3& a, const Vans::EditorAPI::Vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vans::EditorAPI::Vec3 Normalize(const Vans::EditorAPI::Vec3& value)
    {
        const float lengthSq = Dot(value, value);
        if (lengthSq <= 1e-6f)
        {
            return { 0.0f, -1.0f, 0.0f };
        }

        const float invLength = 1.0f / std::sqrt(lengthSq);
        return { value.x * invLength, value.y * invLength, value.z * invLength };
    }

    Vans::EditorAPI::Vec3 NormalizeLightDirection(
        const Vans::EditorAPI::Vec3& direction,
        const Vans::EditorAPI::Vec3& fallbackDirection)
    {
        constexpr float MIN_DIRECTION_LENGTH_SQ = 1e-6f;

        if (Dot(direction, direction) > MIN_DIRECTION_LENGTH_SQ)
        {
            return Normalize(direction);
        }

        if (Dot(fallbackDirection, fallbackDirection) > MIN_DIRECTION_LENGTH_SQ)
        {
            return Normalize(fallbackDirection);
        }

        return { 0.0f, -1.0f, 0.0f };
    }

    bool EditColor3(const char* label, Vans::EditorAPI::Vec3& color)
    {
        float colorValue[3] = { color.x, color.y, color.z };
        const bool changed = ImGui::ColorEdit3(label, colorValue);
        TrackCommandMergeBoundary();
        if (!changed)
        {
            return false;
        }

        color = { colorValue[0], colorValue[1], colorValue[2] };
        return true;
    }

    bool EditFloat3(
        const char* label,
        Vans::EditorAPI::Vec3& value,
        float speed,
        float minValue,
        float maxValue,
        const char* format)
    {
        float valueArray[3] = { value.x, value.y, value.z };
        if (!DragFloat3Tracked(label, valueArray, speed, minValue, maxValue, format))
        {
            return false;
        }

        value = { valueArray[0], valueArray[1], valueArray[2] };
        return true;
    }

    bool EditDirection3(const char* label, Vans::EditorAPI::Vec3& direction)
    {
        const Vans::EditorAPI::Vec3 previousDirection = direction;
        if (!EditFloat3(label, direction, 0.01f, -1.0f, 1.0f, "%.3f"))
        {
            return false;
        }

        direction = NormalizeLightDirection(direction, previousDirection);
        return true;
    }

    float ToDegrees(float radians)
    {
        constexpr float RAD_TO_DEG = 57.29577951308232f;
        return radians * RAD_TO_DEG;
    }

    float ToRadians(float degrees)
    {
        constexpr float DEG_TO_RAD = 0.017453292519943295f;
        return degrees * DEG_TO_RAD;
    }

    bool EditPunctualShadow(
        Vans::EditorAPI::PointLightSettings::PunctualShadowSettings& shadow,
        bool forceEveryFrame = false)
    {
        bool changed = false;
        changed |= ImGui::Checkbox("Cast Shadows", &shadow.castShadows);
        const char* policies[] = { "Disabled", "Auto", "Hero", "Distance Dynamic" };
        changed |= ImGui::Combo("Shadow Policy", &shadow.policy, policies, 4);
        if (shadow.policy == 3)
            ImGui::TextDisabled("Near lights win; Max Distance controls the smooth priority falloff.");
        changed |= ImGui::SliderInt("Shadow Priority", &shadow.priority, 0, 255);

        int resolutionIndex = shadow.resolution == 128 ? 1 : shadow.resolution == 256 ? 2 :
            shadow.resolution == 512 ? 3 : shadow.resolution == 1024 ? 4 : 0;
        const char* resolutions[] = { "Auto", "128", "256", "512", "1024" };
        if (ImGui::Combo("Shadow Resolution", &resolutionIndex, resolutions, 5))
        {
            const int values[] = { 0, 128, 256, 512, 1024 };
            shadow.resolution = values[resolutionIndex];
            changed = true;
        }
        const char* updateModes[] = { "Every Frame", "On Change", "Budgeted" };
        if (forceEveryFrame)
        {
            shadow.updateMode = 0;
            ImGui::BeginDisabled();
            ImGui::Combo("Shadow Update", &shadow.updateMode, updateModes, 3);
            ImGui::EndDisabled();
            ImGui::TextDisabled("Point shadows render all six faces every frame.");
        }
        else
        {
            changed |= ImGui::Combo("Shadow Update", &shadow.updateMode, updateModes, 3);
        }
        const char* fallbacks[] = { "None", "Screen Space" };
        changed |= ImGui::Combo("Shadow Fallback", &shadow.fallback, fallbacks, 2);
        changed |= DragFloatTracked("Shadow Max Distance", &shadow.maxDistance, 0.25f, 0.01f, 10000.0f, "%.2f");
        changed |= DragFloatTracked("Shadow Near Plane", &shadow.nearPlane, 0.005f, 0.0f, 10.0f, "%.3f");
        changed |= DragFloatTracked("Depth Bias (texels)", &shadow.depthBiasTexels, 0.02f, 0.0f, 16.0f, "%.2f");
        changed |= DragFloatTracked("Normal Bias (texels)", &shadow.normalBiasTexels, 0.02f, 0.0f, 16.0f, "%.2f");
        changed |= DragFloatTracked("Source Radius", &shadow.sourceRadius, 0.002f, 0.0f, 10.0f, "%.3f");
        changed |= ImGui::Checkbox("Affects Volumetric Fog", &shadow.affectsFog);
        changed |= ImGui::Checkbox("Affects GI", &shadow.affectsGI);
        changed |= ImGui::InputScalar("Shadow Caster Mask", ImGuiDataType_U32, &shadow.shadowCasterMask, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
        TrackCommandMergeBoundary();
        return changed;
    }

	bool DragDoubleTracked(
		const char* label,
		double* value,
		float speed,
		double minValue,
		double maxValue,
		const char* format)
	{
		const bool changed = ImGui::DragScalar(
			label, ImGuiDataType_Double, value, speed, &minValue, &maxValue, format);
		TrackCommandMergeBoundary();
		return changed;
	}

	bool DragDouble3Tracked(
		const char* label,
		double value[3],
		float speed,
		double minValue,
		double maxValue,
		const char* format)
	{
		const bool changed = ImGui::DragScalarN(
			label, ImGuiDataType_Double, value, 3, speed, &minValue, &maxValue, format);
		TrackCommandMergeBoundary();
		return changed;
	}

	bool CheckboxTracked(const char* label, bool* value)
	{
		const bool changed = ImGui::Checkbox(label, value);
		TrackCommandMergeBoundary();
		return changed;
	}

	bool EditCoefficientPerKilometer(
		const char* label,
		std::array<float, 3>& coefficientPerMeter)
	{
		float coefficientPerKilometer[3] = {
			coefficientPerMeter[0] * 1000.0f,
			coefficientPerMeter[1] * 1000.0f,
			coefficientPerMeter[2] * 1000.0f
		};
		if (!DragFloat3Tracked(
			label, coefficientPerKilometer, 0.0001f, 0.0f, 1.0f, "%.6f /km"))
		{
			return false;
		}
		for (std::size_t channel = 0; channel < 3; ++channel)
			coefficientPerMeter[channel] = coefficientPerKilometer[channel] * 0.001f;
		return true;
	}

	bool EditScalarCoefficientPerKilometer(
		const char* label,
		float& coefficientPerMeter)
	{
		float coefficientPerKilometer = coefficientPerMeter * 1000.0f;
		if (!DragFloatTracked(
			label, &coefficientPerKilometer,
			0.001f, 0.0f, 1000.0f, "%.6f /km"))
		{
			return false;
		}
		coefficientPerMeter = coefficientPerKilometer * 0.001f;
		return true;
	}

	bool EditColorArray3(const char* label, std::array<float, 3>& value)
	{
		const bool changed = ImGui::ColorEdit3(
			label, value.data(), ImGuiColorEditFlags_Float);
		TrackCommandMergeBoundary();
		return changed;
	}
}

void VansGraphics::VansLightWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
    if (!VansEditorWindow::m_LightWindowOpen)
        return;

    g_CommandMergeBoundaryReached = false;
    g_ActiveSceneSettingsGroup = SceneSettingsGroup::None;
    g_SceneSettingsCommitMask = 0;
    ImGui::Begin("Light Info");

    Vans::EditorAPI::LightingSettingsSnapshot lightingSettings = editorAPI.GetLightingSettings();

    bool lightChanged = false;
    lightChanged |= DrawDirectionalLights(lightingSettings.directionalLights);
    lightChanged |= DrawPointLights(lightingSettings.pointLights);
    lightChanged |= DrawSpotLights(lightingSettings.spotLights);
    lightChanged |= DrawRectLights(lightingSettings.rectLights);

    if (lightChanged)
    {
        editorAPI.ApplyLightingSettings(lightingSettings);
    }

	ImGui::Separator();
	DrawPhysicalAtmosphereParameters(editorAPI);
	DrawHeightFogParameters(editorAPI);
    DrawCloudParameters(editorAPI);

    if (g_CommandMergeBoundaryReached)
    {
        editorAPI.BreakCommandMergeGroup();
    }

    ImGui::End();
}

bool VansGraphics::VansLightWindow::DrawDirectionalLights(std::vector<Vans::EditorAPI::DirectionalLightSettings>& directionLights)
{
    if (!ImGui::CollapsingHeader("Directional Lights", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return false;
    }

    bool changed = false;
    for (int lightIndex = 0; lightIndex < static_cast<int>(directionLights.size()); ++lightIndex)
    {
        ImGui::PushID(lightIndex);
        std::string treeLabel = "Directional Light " + std::to_string(lightIndex);
        if (ImGui::TreeNode(treeLabel.c_str()))
        {
            changed |= EditDirection3("Direction", directionLights[lightIndex].direction);
            changed |= EditColor3("Color", directionLights[lightIndex].color);
            changed |= DragFloatTracked("Intensity", &directionLights[lightIndex].intensity, 0.1f, 0.0f, 1000.0f, "%.2f");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    return changed;
}

bool VansGraphics::VansLightWindow::DrawPointLights(std::vector<Vans::EditorAPI::PointLightSettings>& pointLights)
{
    if (!ImGui::CollapsingHeader("Point Lights", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return false;
    }

    bool changed = false;
    for (int lightIndex = 0; lightIndex < static_cast<int>(pointLights.size()); ++lightIndex)
    {
        ImGui::PushID(lightIndex);
        std::string treeLabel = "Point Light " + std::to_string(lightIndex);
        if (ImGui::TreeNode(treeLabel.c_str()))
        {
            changed |= EditFloat3("Position", pointLights[lightIndex].position, 0.05f, -10000.0f, 10000.0f, "%.3f");
            changed |= EditColor3("Color", pointLights[lightIndex].color);
            changed |= DragFloatTracked("Intensity", &pointLights[lightIndex].intensity, 0.1f, 0.0f, 1000.0f, "%.2f");
            changed |= DragFloatTracked("Radius", &pointLights[lightIndex].radius, 0.1f, 0.01f, 10000.0f, "%.2f");
            pointLights[lightIndex].radius = std::max(pointLights[lightIndex].radius, 0.01f);
            changed |= EditPunctualShadow(pointLights[lightIndex].shadow, true);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    return changed;
}

bool VansGraphics::VansLightWindow::DrawSpotLights(std::vector<Vans::EditorAPI::SpotLightSettings>& spotLights)
{
    if (!ImGui::CollapsingHeader("Spot Lights", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return false;
    }

    bool changed = false;
    for (int lightIndex = 0; lightIndex < static_cast<int>(spotLights.size()); ++lightIndex)
    {
        ImGui::PushID(lightIndex);
        std::string treeLabel = "Spot Light " + std::to_string(lightIndex);
        if (ImGui::TreeNode(treeLabel.c_str()))
        {
            changed |= EditFloat3("Position", spotLights[lightIndex].position, 0.05f, -10000.0f, 10000.0f, "%.3f");
            changed |= EditDirection3("Direction", spotLights[lightIndex].direction);
            changed |= EditColor3("Color", spotLights[lightIndex].color);
            changed |= DragFloatTracked("Intensity", &spotLights[lightIndex].intensity, 0.1f, 0.0f, 1000.0f, "%.2f");
            changed |= DragFloatTracked("Radius", &spotLights[lightIndex].radius, 0.1f, 0.01f, 10000.0f, "%.2f");

            float innerCutoffDegrees = ToDegrees(spotLights[lightIndex].innerCutoffRadians);
            float outerCutoffDegrees = ToDegrees(spotLights[lightIndex].outerCutoffRadians);
            bool cutoffChanged = false;
            cutoffChanged |= DragFloatTracked("Inner Cutoff", &innerCutoffDegrees, 0.1f, 0.1f, 89.0f, "%.2f deg");
            cutoffChanged |= DragFloatTracked("Outer Cutoff", &outerCutoffDegrees, 0.1f, 0.1f, 89.5f, "%.2f deg");
            if (cutoffChanged)
            {
                innerCutoffDegrees = std::clamp(innerCutoffDegrees, 0.1f, 89.0f);
                outerCutoffDegrees = std::clamp(outerCutoffDegrees, innerCutoffDegrees, 89.5f);
                spotLights[lightIndex].innerCutoffRadians = ToRadians(innerCutoffDegrees);
                spotLights[lightIndex].outerCutoffRadians = ToRadians(outerCutoffDegrees);
                changed = true;
            }

            spotLights[lightIndex].radius = std::max(spotLights[lightIndex].radius, 0.01f);
            changed |= EditPunctualShadow(spotLights[lightIndex].shadow);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    return changed;
}

bool VansGraphics::VansLightWindow::DrawRectLights(std::vector<Vans::EditorAPI::RectLightSettings>& rectLights)
{
    if (!ImGui::CollapsingHeader("Rect Lights", ImGuiTreeNodeFlags_DefaultOpen))
        return false;

    bool changed = false;
    for (int lightIndex = 0; lightIndex < static_cast<int>(rectLights.size()); ++lightIndex)
    {
        ImGui::PushID(lightIndex);
        const std::string treeLabel = "Rect Light " + std::to_string(lightIndex);
        if (ImGui::TreeNode(treeLabel.c_str()))
        {
            auto& light = rectLights[lightIndex];
            changed |= EditFloat3("Position", light.position, 0.05f, -10000.0f, 10000.0f, "%.3f");
            changed |= EditDirection3("Normal", light.normal);
            changed |= EditColor3("Color", light.color);
            changed |= DragFloatTracked("Intensity", &light.intensity, 0.1f, 0.0f, 1000.0f, "%.2f");
            changed |= DragFloatTracked("Width", &light.width, 0.02f, 0.01f, 10000.0f, "%.2f");
            changed |= DragFloatTracked("Height", &light.height, 0.02f, 0.01f, 10000.0f, "%.2f");
            changed |= DragFloatTracked("Range", &light.range, 0.1f, 0.01f, 10000.0f, "%.2f");
            changed |= ImGui::Checkbox("Two Sided", &light.twoSided);
            changed |= EditPunctualShadow(light.shadow);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    return changed;
}

void VansGraphics::VansLightWindow::DrawPhysicalAtmosphereParameters(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!ImGui::CollapsingHeader("Physical Atmosphere", ImGuiTreeNodeFlags_DefaultOpen))
		return;
	ImGui::PushID("PhysicalAtmosphere");

	Vans::EditorAPI::EnvironmentSettings environment = editorAPI.GetEnvironmentSettings();
	auto& planet = environment.planet;
	auto& atmosphere = environment.physicalAtmosphere;
	bool changed = false;
	g_ActiveSceneSettingsGroup = SceneSettingsGroup::PhysicalAtmosphere;

	changed |= CheckboxTracked("Enabled", &atmosphere.enabled);
	if (ImGui::TreeNodeEx("Planet", ImGuiTreeNodeFlags_DefaultOpen))
	{
		changed |= DragDouble3Tracked("Center World", planet.centerWorldMeters.data(),
			10.0f, -1.0e9, 1.0e9, "%.3f m");
		changed |= DragDoubleTracked("Ground Radius", &planet.bottomRadiusMeters,
			100.0f, 1000.0, 1.0e9, "%.1f m");
		changed |= DragDoubleTracked("Atmosphere Height", &planet.atmosphereHeightMeters,
			100.0f, 100.0, 1.0e7, "%.1f m");
		changed |= EditColorArray3("Ground Albedo", atmosphere.groundAlbedo);
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Rayleigh", ImGuiTreeNodeFlags_DefaultOpen))
	{
		changed |= EditCoefficientPerKilometer("Scattering At Ground",
			atmosphere.rayleigh.scatteringPerMeterAtGround);
		changed |= DragFloatTracked("Density Scale Height",
			&atmosphere.rayleigh.densityScaleHeightMeters,
			50.0f, 1.0f, 200000.0f, "%.1f m");
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Mie / Aerosols", ImGuiTreeNodeFlags_DefaultOpen))
	{
		changed |= EditCoefficientPerKilometer("Scattering At Ground",
			atmosphere.mie.scatteringPerMeterAtGround);
		changed |= EditCoefficientPerKilometer("Absorption At Ground",
			atmosphere.mie.absorptionPerMeterAtGround);
		changed |= DragFloatTracked("Density Scale Height",
			&atmosphere.mie.densityScaleHeightMeters,
			20.0f, 1.0f, 100000.0f, "%.1f m");
		changed |= DragFloatTracked("Anisotropy", &atmosphere.mie.anisotropy,
			0.005f, -0.98f, 0.98f, "%.3f");
		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Ozone Absorption"))
	{
		changed |= EditCoefficientPerKilometer("Absorption",
			atmosphere.ozone.absorptionPerMeter);
		changed |= DragFloatTracked("Center Altitude",
			&atmosphere.ozone.centerAltitudeMeters,
			100.0f, 0.0f, 200000.0f, "%.1f m");
		changed |= DragFloatTracked("Half Width", &atmosphere.ozone.halfWidthMeters,
			100.0f, 1.0f, 200000.0f, "%.1f m");
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Aerial Perspective", ImGuiTreeNodeFlags_DefaultOpen))
	{
		changed |= DragFloatTracked("Distance Scale",
			&atmosphere.aerialPerspective.distanceScale,
			0.01f, 0.01f, 8.0f, "%.3f");
		ImGui::TextDisabled("Physical atmosphere is the only far-distance aerial medium.");
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Volumetric Lighting", ImGuiTreeNodeFlags_DefaultOpen))
	{
		changed |= DragFloatTracked("Main Light Scattering",
			&atmosphere.mainLightVolumetricScatteringScale,
			0.01f, 0.0f, 8.0f, "%.3f");
		ImGui::TextDisabled("Scales direct atmospheric in-scattering only; optical depth is unchanged.");
		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Celestial Disks"))
	{
		for (std::size_t bodyIndex = 0;
			bodyIndex < atmosphere.celestialBodies.size(); ++bodyIndex)
		{
			auto& body = atmosphere.celestialBodies[bodyIndex];
			ImGui::PushID(static_cast<int>(bodyIndex));
			const char* label = body.name.empty() ? "Celestial Body" : body.name.c_str();
			if (ImGui::TreeNode(label))
			{
				changed |= CheckboxTracked("Disk Enabled", &body.disk.enabled);
				float angularRadiusDegrees = ToDegrees(body.disk.angularRadiusRadians);
				float featherDegrees = ToDegrees(body.disk.featherRadians);
				bool angleChanged = false;
				angleChanged |= DragFloatTracked("Angular Radius", &angularRadiusDegrees,
					0.001f, 0.001f, 10.0f, "%.4f deg");
				angleChanged |= DragFloatTracked("Edge Feather", &featherDegrees,
					0.001f, 0.0f, 5.0f, "%.4f deg");
				if (angleChanged)
				{
					body.disk.angularRadiusRadians = ToRadians(angularRadiusDegrees);
					body.disk.featherRadians = ToRadians(featherDegrees);
					changed = true;
				}
				changed |= DragFloatTracked("Radiance Scale", &body.disk.radianceScale,
					0.01f, 0.0f, 100.0f, "%.3f");
				changed |= DragFloatTracked("Cloud Occlusion", &body.disk.occlusionStrength,
					0.05f, 0.0f, 32.0f, "%.3f");
				ImGui::TextDisabled("Light: %s", body.lightEntityId.c_str());
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		ImGui::TreePop();
	}

	ImGui::PopID();
	g_ActiveSceneSettingsGroup = SceneSettingsGroup::None;
	if (changed)
		editorAPI.ApplyEnvironmentSettings(environment);
	if (ConsumeSceneSettingsCommit(SceneSettingsGroup::PhysicalAtmosphere))
		editorAPI.CommitEnvironmentSettings();
}

void VansGraphics::VansLightWindow::DrawHeightFogParameters(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!ImGui::CollapsingHeader("Near-Ground Height Fog", ImGuiTreeNodeFlags_DefaultOpen))
		return;
	ImGui::PushID("HeightFog");

	Vans::EditorAPI::EnvironmentSettings environment = editorAPI.GetEnvironmentSettings();
	auto& fog = environment.heightFog;
	bool changed = false;
	g_ActiveSceneSettingsGroup = SceneSettingsGroup::HeightFog;

	changed |= CheckboxTracked("Enabled", &fog.enabled);
	changed |= DragFloatTracked("Ground Height (World Y)",
		&fog.groundHeightWorldMeters, 1.0f, -10000.0f, 100000.0f, "%.1f m");
	changed |= DragFloatTracked("Visibility At Ground",
		&fog.visibilityAtGroundMeters, 5.0f, 20.0f, 10000.0f, "%.1f m");
	changed |= DragFloatTracked("Density Falloff Height",
		&fog.densityFalloffHeightMeters, 2.0f, 5.0f, 2000.0f, "%.1f m");
	changed |= DragFloatTracked("Start Distance", &fog.startDistanceMeters,
		1.0f, 0.0f, (std::max)(fog.maximumDistanceMeters - 1.0f, 0.0f), "%.1f m");
	changed |= DragFloatTracked("Near Fade Distance", &fog.nearFadeDistanceMeters,
		1.0f, 0.0f, 500.0f, "%.1f m");
	changed |= DragFloatTracked("Maximum Distance", &fog.maximumDistanceMeters,
		5.0f, fog.startDistanceMeters + 1.0f, 10000.0f, "%.1f m");
	changed |= DragFloatTracked("Far Fade Distance", &fog.farFadeDistanceMeters,
		2.0f, 0.0f, fog.maximumDistanceMeters - fog.startDistanceMeters, "%.1f m");
	changed |= EditColorArray3("Single Scattering Albedo",
		fog.singleScatteringAlbedo);
	changed |= DragFloatTracked("Anisotropy", &fog.anisotropy,
		0.005f, -0.9f, 0.9f, "%.3f");
	changed |= EditColorArray3("Emissive Per Meter", fog.emissivePerMeter);
	changed |= DragFloatTracked("Sky Lighting Scale", &fog.skyLightingScale,
		0.01f, 0.0f, 4.0f, "%.3f");
	changed |= DragFloatTracked("Main Light Volumetric Scale",
		&fog.mainLightVolumetricScale, 0.01f, 0.0f, 4.0f, "%.3f");
	changed |= CheckboxTracked("Receive Cloud Shadows", &fog.receiveCloudShadows);

	fog.nearFadeDistanceMeters = std::min(fog.nearFadeDistanceMeters,
		fog.maximumDistanceMeters - fog.startDistanceMeters);
	fog.farFadeDistanceMeters = std::min(fog.farFadeDistanceMeters,
		fog.maximumDistanceMeters - fog.startDistanceMeters -
		fog.nearFadeDistanceMeters);
	const float derivedExtinction = 1.0f /
		(std::max)(fog.visibilityAtGroundMeters, 1.0f);
	ImGui::TextDisabled("Derived ground extinction: %.6f /m (T=e^-1 at visibility).",
		derivedExtinction);
	ImGui::TextDisabled("This medium is integrated only by the near volumetric grid.");

	ImGui::PopID();
	g_ActiveSceneSettingsGroup = SceneSettingsGroup::None;
	if (changed)
		editorAPI.ApplyEnvironmentSettings(environment);
	if (ConsumeSceneSettingsCommit(SceneSettingsGroup::HeightFog))
		editorAPI.CommitEnvironmentSettings();
}
void VansGraphics::VansLightWindow::DrawCloudParameters(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
    if (!ImGui::CollapsingHeader("Volumetric Clouds", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
	ImGui::PushID("VolumetricClouds");

    Vans::EditorAPI::EnvironmentSettings environment = editorAPI.GetEnvironmentSettings();
    Vans::EditorAPI::CloudSettings& cloudParams = environment.volumetricClouds;
    bool changed = false;
    g_ActiveSceneSettingsGroup = SceneSettingsGroup::Clouds;
	changed |= CheckboxTracked("Enabled", &cloudParams.enabled);

    float cloudBaseHeight = cloudParams.cloudMinHeight;
    float cloudThickness = std::max(cloudParams.cloudMaxHeight - cloudParams.cloudMinHeight, 100.0f);
    bool heightChanged = false;
    heightChanged |= DragFloatTracked("Cloud Base Height", &cloudBaseHeight, 10.0f, 0.0f, 20000.0f, "%.0f m");
    heightChanged |= DragFloatTracked("Cloud Thickness", &cloudThickness, 10.0f, 100.0f, 30000.0f, "%.0f m");
    if (heightChanged)
    {
        cloudBaseHeight = std::clamp(cloudBaseHeight, 0.0f, 20000.0f);
        cloudThickness = std::clamp(cloudThickness, 100.0f, 30000.0f);
        cloudParams.cloudMinHeight = cloudBaseHeight;
        cloudParams.cloudMaxHeight = cloudBaseHeight + cloudThickness;
        changed = true;
    }

    changed |= DragFloatTracked("Density", &cloudParams.density, 0.001f, 0.0f, 0.5f, "%.4f");
    changed |= DragFloatTracked("Coverage", &cloudParams.coverage, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("Sun Brightness", &cloudParams.sunBrightness, 0.01f, 0.0f, 10.0f, "%.3f");

    ImGui::Separator();
    changed |= DragFloatTracked("Main Tile", &cloudParams.mainTileMeters, 100.0f, 5000.0f, 200000.0f, "%.0f m");
    changed |= DragFloatTracked("Detail Tile", &cloudParams.detailTileMeters, 50.0f, 1000.0f, 50000.0f, "%.0f m");
    changed |= DragFloatTracked("Main Height Scale", &cloudParams.mainHeightScale, 0.01f, 0.0f, 8.0f, "%.2f");
    changed |= DragFloatTracked("Detail Height Scale", &cloudParams.detailHeightScale, 0.01f, 0.0f, 16.0f, "%.2f");

    ImGui::Separator();
    changed |= DragFloatTracked("Overcast Threshold", &cloudParams.thresholdLowCoverage, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("Clear Threshold", &cloudParams.thresholdHighCoverage, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("Density Smooth Low", &cloudParams.densityRemapLow, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("Density Smooth High", &cloudParams.densityRemapHigh, 0.005f, 0.01f, 1.0f, "%.3f");

    ImGui::Separator();
    changed |= DragFloatTracked("Main Erosion", &cloudParams.mainErosionStrength, 0.01f, 0.0f, 5.0f, "%.3f");
    changed |= DragFloatTracked("Detail Erosion", &cloudParams.detailErosionStrength, 0.01f, 0.0f, 3.0f, "%.3f");
    changed |= DragFloatTracked("Edge Erosion", &cloudParams.edgeErosionStrength, 0.01f, 0.0f, 5.0f, "%.3f");
    changed |= DragFloatTracked("Vertical Shape Power", &cloudParams.verticalShapePower, 0.01f, 0.1f, 4.0f, "%.3f");
    changed |= DragFloatTracked("Detail Erosion Low", &cloudParams.detailErosionLow, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("Detail Erosion High", &cloudParams.detailErosionHigh, 0.005f, 0.01f, 1.0f, "%.3f");
    changed |= DragFloatTracked("Detail Edge Strength", &cloudParams.detailEdgeStrength, 0.01f, 0.0f, 3.0f, "%.3f");

	ImGui::SeparatorText("Cloud Shadow");
	changed |= CheckboxTracked("Enabled##CloudShadow", &cloudParams.shadow.enabled);
	changed |= DragFloatTracked("Atmosphere Strength",
		&cloudParams.shadow.atmosphereStrength, 0.01f, 0.0f, 1.0f, "%.3f");
	changed |= DragFloatTracked("Ambient Occlusion Strength",
		&cloudParams.shadow.ambientOcclusionStrength, 0.01f, 0.0f, 1.0f, "%.3f");
	ImGui::TextDisabled(
		"Strength mixes physical cloud transmittance in irradiance space.");

    ImGui::SeparatorText("HP Optical");
    changed |= DragFloatTracked("Sigma T Ref", &cloudParams.sigmaTRef, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("View Absorption", &cloudParams.viewAbsorption, 0.01f, 0.0f, 5.0f, "%.3f");
    changed |= DragFloatTracked("Light Absorption", &cloudParams.lightAbsorption, 0.01f, 0.0f, 5.0f, "%.3f");
    changed |= DragFloatTracked("Single Scattering Albedo", &cloudParams.singleScatteringAlbedo, 0.0005f, 0.0f, 0.9999f, "%.4f");
    changed |= DragFloatTracked("Scattering Tint R", &cloudParams.scatteringTintR, 0.01f, 0.0f, 4.0f, "%.3f");
    changed |= DragFloatTracked("Scattering Tint G", &cloudParams.scatteringTintG, 0.01f, 0.0f, 4.0f, "%.3f");
    changed |= DragFloatTracked("Scattering Tint B", &cloudParams.scatteringTintB, 0.01f, 0.0f, 4.0f, "%.3f");

    ImGui::SeparatorText("HP Directional MS");
    changed |= DragFloatTracked("Forward Eccentricity", &cloudParams.forwardEccentricity, 0.005f, 0.0f, 0.95f, "%.3f");
    changed |= DragFloatTracked("Backward Eccentricity", &cloudParams.backwardEccentricity, 0.005f, 0.0f, 0.95f, "%.3f");
    changed |= DragFloatTracked("MS Attenuation", &cloudParams.msAttenuation, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("MS Contribution", &cloudParams.msContribution, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("MS Eccentricity", &cloudParams.msEccentricity, 0.005f, 0.0f, 1.0f, "%.3f");

    ImGui::SeparatorText("HP Diffuse Field");
    changed |= DragFloatTracked("Scatter Source OD Scale", &cloudParams.scatterSourceODScale, 0.005f, 0.001f, 2.0f, "%.3f");
    changed |= DragFloatTracked("Scatter Source Curve", &cloudParams.scatterSourceCurvePow, 0.01f, 0.01f, 4.0f, "%.3f");
    changed |= DragFloatTracked("Phi Fwd Intensity", &cloudParams.phiFwdIntensity, 0.01f, 0.0f, 5.0f, "%.3f");
    changed |= DragFloatTracked("Phi Fwd Depth Pow", &cloudParams.phiFwdDepthPow, 0.01f, 0.01f, 4.0f, "%.3f");
    changed |= DragFloatTracked("Phi Fwd Depth Bias", &cloudParams.phiFwdDepthBias, 0.005f, -1.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("Phi Fwd Build Scale", &cloudParams.phiFwdMSBuildScale, 0.01f, 0.0f, 5.0f, "%.3f");
    changed |= DragFloatTracked("Phi Fwd Compress", &cloudParams.phiFwdCompress, 0.01f, 0.0f, 5.0f, "%.3f");
    changed |= DragFloatTracked("Phi Fwd Max Distance", &cloudParams.phiFwdMaxDistance, 50.0f, 100.0f, 30000.0f, "%.0f m");
    changed |= DragFloatTracked("Phi Fwd Cone Ratio", &cloudParams.phiFwdConeRatio, 0.01f, 1.01f, 3.0f, "%.3f");
    changed |= DragFloatTracked("Phi Fwd Min Step", &cloudParams.phiFwdMinStep, 5.0f, 1.0f, 1000.0f, "%.0f m");
    changed |= DragFloatTracked("Light Step Count", &cloudParams.lightStepCount, 1.0f, 1.0f, 16.0f, "%.0f");

    ImGui::SeparatorText("HP Boundary / AO");
    changed |= DragFloatTracked("AO Upward Scale", &cloudParams.aoUpwardScale, 0.01f, 0.0f, 5.0f, "%.3f");
    changed |= DragFloatTracked("Ambient Bottom", &cloudParams.ambientBottomStrength, 0.01f, 0.0f, 2.0f, "%.3f");
    changed |= DragFloatTracked("Ambient Top", &cloudParams.ambientTopStrength, 0.01f, 0.0f, 2.0f, "%.3f");
    changed |= DragFloatTracked("Ambient Dusk Warmth", &cloudParams.ambientDuskWarmth, 0.01f, 0.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("Boundary Confidence", &cloudParams.boundaryConfidence, 0.01f, 0.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("Boundary Wrap", &cloudParams.boundaryWrap, 0.01f, 0.0f, 1.0f, "%.3f");
    changed |= DragFloatTracked("Boundary Gradient Step", &cloudParams.boundaryGradientStep, 10.0f, 50.0f, 2000.0f, "%.0f m");
    changed |= DragFloatTracked("Boundary Gradient Strength", &cloudParams.boundaryGradientStrength, 0.01f, 0.0f, 1.0f, "%.3f");

    ImGui::SeparatorText("HP Debug");
    changed |= DragFloatTracked("Shading Debug Mode", &cloudParams.shadingDebugMode, 1.0f, 0.0f, 8.0f, "%.0f");
	ImGui::PopID();
    g_ActiveSceneSettingsGroup = SceneSettingsGroup::None;

    if (ImGui::Button("Reset Cloud Defaults"))
    {
        environment.volumetricClouds = {};
        editorAPI.ApplyEnvironmentSettings(environment);
        editorAPI.CommitEnvironmentSettings();
        editorAPI.BreakCommandMergeGroup();
        return;
    }

    cloudParams.mainTileMeters = std::max(cloudParams.mainTileMeters, 1000.0f);
    cloudParams.detailTileMeters = std::max(cloudParams.detailTileMeters, 500.0f);
    cloudParams.densityRemapHigh = std::max(cloudParams.densityRemapHigh, cloudParams.densityRemapLow + 0.01f);
    cloudParams.detailErosionHigh = std::max(cloudParams.detailErosionHigh, cloudParams.detailErosionLow + 0.01f);

    if (changed)
    {
        editorAPI.ApplyEnvironmentSettings(environment);
    }
    if (ConsumeSceneSettingsCommit(SceneSettingsGroup::Clouds))
        editorAPI.CommitEnvironmentSettings();
}
