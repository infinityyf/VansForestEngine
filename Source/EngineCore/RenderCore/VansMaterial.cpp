#include "VansMaterial.h"
#include "VansRenderSceneSnapshot.h"
#include "../RuntimeCore/VansThreadContract.h"
#include "../Util/VansLog.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_set>
#include <type_traits>
using namespace VansGraphics;

namespace
{
constexpr float kLegacyMoonDiskRadianceScale = 0.00008f;

std::string NormalizeSkinProfileName(std::string value)
{
	std::string normalized;
	normalized.reserve(value.size());
	for (char c : value)
	{
		const unsigned char uc = static_cast<unsigned char>(c);
		if (std::isalnum(uc))
			normalized.push_back(static_cast<char>(std::tolower(uc)));
	}

	if (normalized.empty() || normalized == "default" || normalized == "standard")
		return "neutral";
	if (normalized == "light" || normalized == "pale")
		return "fair";
	if (normalized == "brown" || normalized == "deep")
		return "dark";
	if (normalized == "ear" || normalized == "lip" || normalized == "thinarea")
		return "thin";
	if (normalized == "closeup" || normalized == "hero")
		return "cinematic";
	return normalized;
}

void AssignSkinProfile(
	VansBasePBRParam& legacy,
	VansSkinGPUParam& params,
	const glm::vec3& scatterColor,
	float scatterAmount,
	float roughness,
	float normalStrength,
	float specularScale,
	float transmissionScale,
	const glm::vec4& lobeIOR,
	const glm::vec4& profileControls,
	const glm::vec4& profileShape,
	const glm::vec4& profileLUT)
{
	legacy.m_albedo = scatterColor;
	legacy.m_roughness = roughness;
	legacy.m_metallic = normalStrength;
	legacy.m_ao = scatterAmount;
	legacy.padding = specularScale;
	params.scatterColorAmount = glm::vec4(scatterColor, scatterAmount);
	params.roughnessNormalSpecular = glm::vec4(roughness, normalStrength, specularScale, transmissionScale);
	params.lobeIOR = lobeIOR;
	params.profileControls = profileControls;
	params.profileShape = profileShape;
	params.profileLUT = profileLUT;
}

bool BuildSkinProfilePresetPayload(
	const std::string& normalizedProfile,
	VansBasePBRParam& legacy,
	VansSkinGPUParam& params)
{
	if (normalizedProfile == "custom")
		return true;

	if (normalizedProfile == "neutral")
	{
		AssignSkinProfile(
			legacy, params,
			glm::vec3(1.0f, 0.34f, 0.22f), 0.65f,
			0.62f, 0.35f, 1.0f, 1.0f,
			glm::vec4(0.75f, 1.75f, 1.4f, 0.72f),
			glm::vec4(1.0f, 1.0f, 1.0f, 0.35f),
			glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
			glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
		return true;
	}

	if (normalizedProfile == "fair")
	{
		AssignSkinProfile(
			legacy, params,
			glm::vec3(1.0f, 0.42f, 0.30f), 0.72f,
			0.58f, 0.38f, 1.05f, 1.15f,
			glm::vec4(0.70f, 1.85f, 1.4f, 0.74f),
			glm::vec4(1.20f, 1.12f, 0.90f, 0.42f),
			glm::vec4(1.15f, 1.05f, 0.90f, 1.10f),
			glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
		return true;
	}

	if (normalizedProfile == "medium")
	{
		AssignSkinProfile(
			legacy, params,
			glm::vec3(0.92f, 0.30f, 0.20f), 0.62f,
			0.64f, 0.35f, 0.95f, 0.95f,
			glm::vec4(0.78f, 1.75f, 1.4f, 0.70f),
			glm::vec4(1.00f, 0.95f, 1.05f, 0.32f),
			glm::vec4(1.00f, 0.90f, 0.75f, 1.00f),
			glm::vec4(2.0f, 0.0f, 0.0f, 0.0f));
		return true;
	}

	if (normalizedProfile == "dark")
	{
		AssignSkinProfile(
			legacy, params,
			glm::vec3(0.72f, 0.24f, 0.18f), 0.52f,
			0.68f, 0.32f, 0.90f, 0.75f,
			glm::vec4(0.82f, 1.90f, 1.4f, 0.68f),
			glm::vec4(0.85f, 0.85f, 1.20f, 0.24f),
			glm::vec4(0.85f, 0.75f, 0.62f, 0.80f),
			glm::vec4(3.0f, 0.0f, 0.0f, 0.0f));
		return true;
	}

	if (normalizedProfile == "thin")
	{
		AssignSkinProfile(
			legacy, params,
			glm::vec3(1.0f, 0.30f, 0.18f), 0.85f,
			0.54f, 0.42f, 1.10f, 1.60f,
			glm::vec4(0.66f, 1.65f, 1.4f, 0.76f),
			glm::vec4(1.45f, 1.65f, 0.65f, 0.50f),
			glm::vec4(1.35f, 1.15f, 0.90f, 1.25f),
			glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
		return true;
	}

	if (normalizedProfile == "cinematic")
	{
		AssignSkinProfile(
			legacy, params,
			glm::vec3(1.0f, 0.36f, 0.24f), 0.72f,
			0.58f, 0.45f, 1.15f, 1.20f,
			glm::vec4(0.68f, 1.95f, 1.42f, 0.78f),
			glm::vec4(1.30f, 1.15f, 0.90f, 0.45f),
			glm::vec4(1.20f, 1.00f, 0.82f, 1.20f),
			glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
		return true;
	}

	return false;
}

glm::vec3 NormalizeMaterialDirectionSafe(const glm::vec3& direction, const glm::vec3& fallbackDirection)
{
	if (std::isfinite(direction.x) && std::isfinite(direction.y) && std::isfinite(direction.z) &&
		glm::dot(direction, direction) > 1e-6f)
	{
		return glm::normalize(direction);
	}
	return glm::normalize(fallbackDirection);
}

bool ReadMaterialVec3(const VansMaterialParameterValue& value, glm::vec3& out)
{
	return std::visit([&](const auto& typedValue) -> bool
		{
			using T = std::decay_t<decltype(typedValue)>;
			if constexpr (std::is_same_v<T, glm::vec3>)
			{
				out = typedValue;
				return true;
			}
			else if constexpr (std::is_same_v<T, glm::vec4>)
			{
				out = glm::vec3(typedValue);
				return true;
			}
			else
			{
				return false;
			}
		}, value);
}

bool ReadMaterialFloat(const VansMaterialParameterValue& value, float& out)
{
	return std::visit([&](const auto& typedValue) -> bool
		{
			using T = std::decay_t<decltype(typedValue)>;
			if constexpr (std::is_same_v<T, float>)
			{
				out = typedValue;
				return true;
			}
			else if constexpr (std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t>)
			{
				out = static_cast<float>(typedValue);
				return true;
			}
			else
			{
				return false;
			}
		}, value);
}

bool ReadMaterialString(const VansMaterialParameterValue& value, std::string& out)
{
	if (const auto* text = std::get_if<std::string>(&value))
	{
		out = *text;
		return true;
	}
	return false;
}

glm::vec4 ReadMaterialVec4Value(const VansMaterialParameterValue& value)
{
	return std::visit([](const auto& typedValue) -> glm::vec4
		{
			using T = std::decay_t<decltype(typedValue)>;
			if constexpr (std::is_same_v<T, float>)
				return glm::vec4(typedValue, 0.0f, 0.0f, 0.0f);
			else if constexpr (std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t>)
				return glm::vec4(static_cast<float>(typedValue), 0.0f, 0.0f, 0.0f);
			else if constexpr (std::is_same_v<T, glm::vec2>)
				return glm::vec4(typedValue, 0.0f, 0.0f);
			else if constexpr (std::is_same_v<T, glm::vec3>)
				return glm::vec4(typedValue, 0.0f);
			else if constexpr (std::is_same_v<T, glm::vec4>)
				return typedValue;
			else
				return glm::vec4(0.0f);
		}, value);
}

float Saturate(float value)
{
	return std::clamp(value, 0.0f, 1.0f);
}

uint8_t FloatToUNorm8(float value)
{
	return static_cast<uint8_t>(std::lround(Saturate(value) * 255.0f));
}

float LerpFloat(float a, float b, float t)
{
	return a + (b - a) * t;
}

float SkinLUTChannelResponse(
	float ndotl,
	float curvature,
	float scatterAmount,
	float scatterTint,
	float scatterRadius,
	float boundaryColorBleed,
	float transmissionDepthScale)
{
	const float profileCurvature = Saturate(curvature);
	const float channelRadius = std::clamp(scatterRadius, 0.05f, 4.0f);
	const float radiusResponse = 1.0f - std::exp(-channelRadius * 0.65f);
	const float thinness = profileCurvature * profileCurvature;
	const float wrap = std::clamp(0.08f + radiusResponse * (0.22f + 0.48f * thinness), 0.05f, 0.92f);
	const float wrappedDiffuse = Saturate((ndotl + wrap) / (1.0f + wrap));
	const float lambert = Saturate(ndotl);
	const float terminator = std::exp(-std::max(ndotl, 0.0f) * (2.0f + channelRadius)) *
		Saturate(-ndotl + wrap) * thinness;
	const float backScatter = terminator *
		std::exp(-std::max(-ndotl, 0.0f) * std::clamp(transmissionDepthScale, 0.05f, 4.0f) /
			(0.20f + channelRadius * 0.18f));
	const float scatterWeight = Saturate(scatterAmount) * Saturate(0.35f + 0.65f * profileCurvature);
	const float boundaryTint = std::clamp(boundaryColorBleed, 0.0f, 2.0f) * scatterWeight * thinness;
	const float tinted = wrappedDiffuse * LerpFloat(1.0f, std::max(scatterTint, 0.0f), Saturate(boundaryTint));
	const float scattered = Saturate(tinted + backScatter * std::max(scatterTint, 0.0f) * 0.42f);
	return LerpFloat(lambert, scattered, scatterWeight);
}
}

// ============================================================
// VansMaterial pass shader accessors.
// ============================================================
bool VansGraphics::IsDynamicSkinProfileLUTLayer(int layer)
{
	return layer >= VANS_FIRST_DYNAMIC_SKIN_PROFILE_LUT_LAYER &&
		layer < VANS_SKIN_PROFILE_LUT_LAYER_COUNT;
}

VansGraphics::VansSkinProfileLUTFingerprint VansGraphics::BuildSkinProfileLUTFingerprint(
	const VansSkinGPUParam& profile)
{
	auto quantize = [](float value)
	{
		if (!std::isfinite(value))
			value = 0.0f;
		return static_cast<int32_t>(std::lround(std::clamp(value, -8.0f, 8.0f) * 2048.0f));
	};

	return {
		quantize(profile.scatterColorAmount.r),
		quantize(profile.scatterColorAmount.g),
		quantize(profile.scatterColorAmount.b),
		quantize(profile.scatterColorAmount.w),
		quantize(profile.profileControls.x),
		quantize(profile.profileControls.z),
		quantize(profile.profileShape.r),
		quantize(profile.profileShape.g),
		quantize(profile.profileShape.b),
		quantize(profile.profileShape.w)
	};
}

std::string VansGraphics::NormalizeSkinProfilePresetName(std::string profileName)
{
	return NormalizeSkinProfileName(std::move(profileName));
}

bool VansGraphics::ResolveSkinProfilePresetPayload(
	const std::string& profileName,
	VansBasePBRParam& legacy,
	VansSkinGPUParam& params)
{
	return BuildSkinProfilePresetPayload(NormalizeSkinProfileName(profileName), legacy, params);
}

const char* VansGraphics::GetBuiltInSkinProfileLUTLayerName(int layer)
{
	switch (layer)
	{
	case 0: return "neutral";
	case 1: return "fair";
	case 2: return "medium";
	case 3: return "dark";
	default: return nullptr;
	}
}

bool VansGraphics::GenerateSkinProfileLUTPixels(
	const VansSkinGPUParam& profile,
	int width,
	int height,
	std::vector<uint8_t>& outPixels)
{
	if (width <= 0 || height <= 0)
		return false;

	outPixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 255u);

	const glm::vec3 scatterTint = glm::max(glm::vec3(profile.scatterColorAmount), glm::vec3(0.0f));
	const float scatterAmount = std::clamp(profile.scatterColorAmount.w, 0.0f, 1.0f);
	const float diffusionRadius = std::clamp(profile.profileControls.x, 0.05f, 4.0f);
	const float transmissionDepthScale = std::clamp(profile.profileControls.z, 0.05f, 4.0f);
	const glm::vec3 scatterRadius = glm::clamp(
		glm::vec3(profile.profileShape) * diffusionRadius,
		glm::vec3(0.05f),
		glm::vec3(4.0f));
	const float boundaryColorBleed = std::clamp(profile.profileShape.w, 0.0f, 2.0f);

	for (int y = 0; y < height; ++y)
	{
		const float curvature = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
		for (int x = 0; x < width; ++x)
		{
			const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
			const float ndotl = u * 2.0f - 1.0f;
			const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(width) +
				static_cast<size_t>(x)) * 4u;

			outPixels[offset + 0u] = FloatToUNorm8(SkinLUTChannelResponse(
				ndotl, curvature, scatterAmount, scatterTint.r, scatterRadius.r,
				boundaryColorBleed, transmissionDepthScale));
			outPixels[offset + 1u] = FloatToUNorm8(SkinLUTChannelResponse(
				ndotl, curvature, scatterAmount, scatterTint.g, scatterRadius.g,
				boundaryColorBleed, transmissionDepthScale));
			outPixels[offset + 2u] = FloatToUNorm8(SkinLUTChannelResponse(
				ndotl, curvature, scatterAmount, scatterTint.b, scatterRadius.b,
				boundaryColorBleed, transmissionDepthScale));
			outPixels[offset + 3u] = 255u;
		}
	}

	return true;
}

bool VansGraphics::GenerateBuiltInSkinProfileLUTLayer(
	int layer,
	int width,
	int height,
	std::vector<uint8_t>& outPixels)
{
	const char* profileName = GetBuiltInSkinProfileLUTLayerName(layer);
	if (!profileName)
		return false;

	VansBasePBRParam legacy;
	VansSkinGPUParam params;
	if (!ResolveSkinProfilePresetPayload(profileName, legacy, params))
		return false;

	return GenerateSkinProfileLUTPixels(params, width, height, outPixels);
}

VansGraphicsShader* VansGraphics::VansMaterial::GetPassShader(const std::string& passName) const
{
	auto it = m_PassShaders.find(passName);
	return (it != m_PassShaders.end()) ? it->second : nullptr;
}

bool VansGraphics::VansMaterial::HasPass(const std::string& passName) const
{
	return m_PassShaders.count(passName) > 0;
}

// ============================================================
// Material subclass destructors release owned Vulkan resources.
// ============================================================

VansGraphics::VansTransparentMaterial::~VansTransparentMaterial()
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_TransparentOwnedDescSets);
	descMgr->DestroyDescriptorSetLayout(m_TransparentOwnedLayout);
}

VansGraphics::VansSkinMaterial::~VansSkinMaterial()
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_SkinOwnedDescSets);
	descMgr->DestroyDescriptorSetLayout(m_SkinOwnedLayout);
}

bool VansGraphics::VansSkinMaterial::ApplySkinProfilePreset(const std::string& profileName)
{
	const std::string normalizedProfile = NormalizeSkinProfileName(profileName);
	VansBasePBRParam legacy = m_BasePBRParam;
	VansSkinGPUParam params = m_SkinParams;
	if (!BuildSkinProfilePresetPayload(normalizedProfile, legacy, params))
		return false;

	m_SkinProfileName = normalizedProfile;
	m_BasePBRParam = legacy;
	m_SkinParams = params;
	m_UseExplicitSkinProfileLUTLayer = false;
	return true;
}

VansGraphics::VansSkinGPUParam VansGraphics::VansSkinMaterial::BuildGPUParam() const
{
	VansSkinGPUParam payload = m_SkinParams;
	payload.scatterColorAmount = glm::vec4(
		glm::max(m_BasePBRParam.m_albedo, glm::vec3(0.0f)),
		std::clamp(m_BasePBRParam.m_ao, 0.0f, 1.0f));
	payload.roughnessNormalSpecular = glm::vec4(
		std::clamp(m_BasePBRParam.m_roughness, 0.045f, 1.0f),
		std::clamp(m_BasePBRParam.m_metallic, 0.0f, 2.0f),
		std::clamp(m_BasePBRParam.padding, 0.0f, 4.0f),
		std::clamp(m_SkinParams.roughnessNormalSpecular.w, 0.0f, 4.0f));
	payload.lobeIOR = glm::vec4(
		std::clamp(m_SkinParams.lobeIOR.x, 0.1f, 4.0f),
		std::clamp(m_SkinParams.lobeIOR.y, 0.1f, 4.0f),
		std::clamp(m_SkinParams.lobeIOR.z, 1.0f, 2.5f),
		std::clamp(m_SkinParams.lobeIOR.w, 0.0f, 1.0f));
	payload.profileControls = glm::vec4(
		std::clamp(m_SkinParams.profileControls.x, 0.05f, 4.0f),
		std::clamp(m_SkinParams.profileControls.y, 0.10f, 4.0f),
		std::clamp(m_SkinParams.profileControls.z, 0.05f, 4.0f),
		std::clamp(m_SkinParams.profileControls.w, 0.0f, 1.0f));
	payload.profileShape = glm::vec4(
		std::clamp(m_SkinParams.profileShape.x, 0.05f, 4.0f),
		std::clamp(m_SkinParams.profileShape.y, 0.05f, 4.0f),
		std::clamp(m_SkinParams.profileShape.z, 0.05f, 4.0f),
		std::clamp(m_SkinParams.profileShape.w, 0.0f, 2.0f));
	payload.profileLUT = glm::vec4(
		std::clamp(
			m_SkinParams.profileLUT.x,
			-1.0f,
			static_cast<float>(VANS_SKIN_PROFILE_LUT_LAYER_COUNT - 1)),
		std::clamp(m_SkinParams.profileLUT.y, 0.0f, 1.0f),
		0.0f,
		0.0f);
	payload.debugControls = glm::vec4(
		std::clamp(m_SkinParams.debugControls.x, 0.0f, 16.0f),
		0.0f,
		0.0f,
		0.0f);
	return payload;
}

VansGraphics::VansClothMaterial::~VansClothMaterial()
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_ClothOwnedDescSets);
	descMgr->DestroyDescriptorSetLayout(m_ClothOwnedLayout);
}

VansGraphics::VansClothGPUParam VansGraphics::VansClothMaterial::BuildGPUParam() const
{
	VansClothGPUParam payload;
	payload.sheenColorWeight = glm::vec4(
		glm::max(m_SheenColor, glm::vec3(0.0f)),
		std::clamp(m_SheenStrength, 0.0f, 1.0f));
	payload.transmissionColorStrength = glm::vec4(
		glm::max(m_TransmissionColor, glm::vec3(0.0f)),
		std::clamp(m_Translucency, 0.0f, 1.0f));
	payload.controls = glm::vec4(
		static_cast<float>(m_ClothModel),
		std::clamp(m_Anisotropy, -0.95f, 0.95f),
		std::clamp(m_Thickness, 0.0f, 1.0f),
		static_cast<float>(m_ClothFlags));
	return payload;
}

VansGraphics::VansHairMaterial::~VansHairMaterial()
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_HairOwnedDescSets);
	descMgr->DestroyDescriptorSetLayout(m_HairOwnedLayout);
	if (m_ParamsDevice != VK_NULL_HANDLE && m_ParamsBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
	{
		m_ParamsBuffer.DestroyVulkanBuffer(m_ParamsDevice);
	}
}

VansGraphics::VansSubsurfaceMaterial::~VansSubsurfaceMaterial()
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_SubsurfaceOwnedDescSets);
	descMgr->DestroyDescriptorSetLayout(m_SubsurfaceOwnedLayout);
}

VansGraphics::VansGrassMaterial::~VansGrassMaterial()
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_GrassOwnedDescSets);
	descMgr->DestroyDescriptorSetLayout(m_GrassOwnedLayout);
}

VansGraphics::VansMaterialManager::VansMaterialManager()
{
}

void VansGraphics::VansMaterialManager::UploadCloudParamsToGPU()
{
	if (m_CloudParamsCBBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
	{
		return;
	}

	m_CloudParamsCBBuffer.SetBufferData(&m_CloudParams, 0, sizeof(VansCloudParamsGPU));
}

VansGraphics::VansMaterialManager::~VansMaterialManager()
{
	ClearRuntimeMaterialInstances();
	ClearRuntimeRenderTextures();
}

namespace
{
std::unique_ptr<VansGraphics::VansMaterial> CloneRuntimeMaterial(
	const VansGraphics::VansMaterial& source)
{
	using namespace VansGraphics;
	std::unique_ptr<VansMaterial> result;
	if (const auto* material = dynamic_cast<const VansPBRMaterial*>(&source))
		result = std::make_unique<VansPBRMaterial>(*material);
	else if (const auto* material = dynamic_cast<const VansEmissiveMaterial*>(&source))
		result = std::make_unique<VansEmissiveMaterial>(*material);
	else if (const auto* material = dynamic_cast<const VansDecalMaterial*>(&source))
		result = std::make_unique<VansDecalMaterial>(*material);
	else if (const auto* material = dynamic_cast<const VansSkinMaterial*>(&source))
	{
		auto clone = std::make_unique<VansSkinMaterial>(*material);
		clone->m_SkinOwnedLayout = VK_NULL_HANDLE;
		clone->m_SkinOwnedDescSets.clear();
		result = std::move(clone);
	}
	else if (const auto* material = dynamic_cast<const VansClothMaterial*>(&source))
	{
		auto clone = std::make_unique<VansClothMaterial>(*material);
		clone->m_ClothOwnedLayout = VK_NULL_HANDLE;
		clone->m_ClothOwnedDescSets.clear();
		result = std::move(clone);
	}
	else if (const auto* material = dynamic_cast<const VansSubsurfaceMaterial*>(&source))
	{
		auto clone = std::make_unique<VansSubsurfaceMaterial>(*material);
		clone->m_SubsurfaceOwnedLayout = VK_NULL_HANDLE;
		clone->m_SubsurfaceOwnedDescSets.clear();
		result = std::move(clone);
	}
	else if (const auto* material = dynamic_cast<const VansTransmissionMaterial*>(&source))
		result = std::make_unique<VansTransmissionMaterial>(*material);
	else if (source.m_MaterialType == VansMaterialType::VAN_CUSTOM_SHADER)
		result = std::make_unique<VansMaterial>(source);
	return result;
}

int RuntimePBRIndex(VansGraphics::VansMaterial& material)
{
	return material.GetGlobalMaterialIndex();
}

void SetRuntimePBRIndex(VansGraphics::VansMaterial& material, int index)
{
	material.SetGlobalMaterialIndex(index);
}
}

void VansGraphics::VansMaterialManager::InitializeRuntimeMaterialPools(
	std::size_t pbrInstanceCount,
	std::size_t customInstanceCount,
	VansVKImage* fallbackTexture)
{
	ClearRuntimeMaterialInstances();
	m_FreeRuntimePBRIndices.clear();
	m_FreeRuntimeCustomIndices.clear();
	const VansClothGPUParam defaultCloth{};
	const VansTreeLeafParamsGPU defaultTreeLeaf{};
	const VansSkinGPUParam defaultSkin{};
	for (std::size_t count = 0; count < pbrInstanceCount; ++count)
	{
		const int index = static_cast<int>(m_GlobalPBRParamData.size());
		m_GlobalPBRParamData.emplace_back();
		m_GlobalClothParamData.push_back(defaultCloth);
		m_GlobalTreeLeafParamData.push_back(defaultTreeLeaf);
		m_GlobalSkinParamData.push_back(defaultSkin);
		for (int texture = 0; texture < 5; ++texture)
			m_GlobalPBRTextures.push_back(fallbackTexture);
		m_FreeRuntimePBRIndices.push_back(index);
	}
	for (std::size_t count = 0; count < customInstanceCount; ++count)
	{
		const int index = static_cast<int>(m_GlobalCustomMaterialParamData.size());
		m_GlobalCustomMaterialParamData.emplace_back();
		m_FreeRuntimeCustomIndices.push_back(index);
	}
	std::reverse(m_FreeRuntimePBRIndices.begin(), m_FreeRuntimePBRIndices.end());
	std::reverse(m_FreeRuntimeCustomIndices.begin(), m_FreeRuntimeCustomIndices.end());
	m_GlobalBindlessDescriptorsDirty = true;
}

VansGraphics::VansMaterial* VansGraphics::VansMaterialManager::AcquireRuntimeMaterialInstance(
	const std::string& instanceKey,
	const VansMaterial& source,
	VkDescriptorSet sceneGlobalDescriptorSet)
{
	if (instanceKey.empty()) return nullptr;
	if (const auto found = m_RuntimeMaterialInstances.find(instanceKey);
		found != m_RuntimeMaterialInstances.end()) return found->second.material.get();

	std::unique_ptr<VansMaterial> clone = CloneRuntimeMaterial(source);
	if (!clone) return nullptr;
	const bool customPayload = source.m_MaterialType == VansMaterialType::VAN_CUSTOM_SHADER ||
		source.m_MaterialType == VansMaterialType::VAN_PBR_TRANSMISSION;
	std::vector<int>& freeIndices = customPayload ? m_FreeRuntimeCustomIndices : m_FreeRuntimePBRIndices;
	if (freeIndices.empty()) return nullptr;
	const int poolIndex = freeIndices.back();
	freeIndices.pop_back();
	clone->m_AssetName = "RuntimeTimelineMaterial:" + instanceKey;

	if (customPayload)
	{
		const int sourceIndex = source.m_MaterialIndex;
		if (sourceIndex < 0 || sourceIndex >= static_cast<int>(m_GlobalCustomMaterialParamData.size()))
		{
			freeIndices.push_back(poolIndex);
			return nullptr;
		}
		clone->m_MaterialIndex = poolIndex;
		m_GlobalCustomMaterialParamData[poolIndex] = m_GlobalCustomMaterialParamData[sourceIndex];
	}
	else
	{
		const int sourceIndex = RuntimePBRIndex(const_cast<VansMaterial&>(source));
		if (sourceIndex < 0 || sourceIndex >= static_cast<int>(m_GlobalPBRParamData.size()))
		{
			freeIndices.push_back(poolIndex);
			return nullptr;
		}
		SetRuntimePBRIndex(*clone, poolIndex);
		m_GlobalPBRParamData[poolIndex] = m_GlobalPBRParamData[sourceIndex];
		m_GlobalClothParamData[poolIndex] = m_GlobalClothParamData[sourceIndex];
		m_GlobalTreeLeafParamData[poolIndex] = m_GlobalTreeLeafParamData[sourceIndex];
		m_GlobalSkinParamData[poolIndex] = m_GlobalSkinParamData[sourceIndex];
		if (auto* skinClone = dynamic_cast<VansSkinMaterial*>(clone.get()))
			ResolveSkinProfileLUTForMaterial(*skinClone, m_GlobalSkinParamData[poolIndex], nullptr);
		{
			std::lock_guard<std::mutex> lock(m_GlobalPBRTexturesMutex);
			for (int texture = 0; texture < 5; ++texture)
				m_GlobalPBRTextures[poolIndex * 5 + texture] =
					m_GlobalPBRTextures[sourceIndex * 5 + texture];
		}
		m_GlobalBindlessDescriptorsDirty = true;
	}

	VansMaterial* result = clone.get();
	m_RuntimeMaterialInstances.emplace(instanceKey,
		RuntimeMaterialInstance{ std::move(clone), poolIndex, customPayload });
	return result;
}

bool VansGraphics::VansMaterialManager::ReleaseRuntimeMaterialInstance(const std::string& instanceKey)
{
	const auto found = m_RuntimeMaterialInstances.find(instanceKey);
	if (found == m_RuntimeMaterialInstances.end()) return false;
	if (!found->second.customPayload)
		ReleaseSkinProfileLUTLayerForMaterial(found->second.poolIndex);
	(found->second.customPayload ? m_FreeRuntimeCustomIndices : m_FreeRuntimePBRIndices)
		.push_back(found->second.poolIndex);
	m_RuntimeMaterialInstances.erase(found);
	return true;
}

void VansGraphics::VansMaterialManager::ClearRuntimeMaterialInstances()
{
	for (const auto& [_, instance] : m_RuntimeMaterialInstances)
	{
		if (!instance.customPayload)
			ReleaseSkinProfileLUTLayerForMaterial(instance.poolIndex);
	}
	m_RuntimeMaterialInstances.clear();
}

bool VansGraphics::VansMaterialManager::RegisterRuntimeRenderTexture(const std::string& name, VansTexture* texture, bool replaceExisting)
{
	return m_RuntimeRenderTextureManager.Add(name, texture, replaceExisting);
}

VansTexture* VansGraphics::VansMaterialManager::GetRuntimeRenderTexture(const std::string& name) const
{
	return m_RuntimeRenderTextureManager.Get(name);
}

bool VansGraphics::VansMaterialManager::HasRuntimeRenderTexture(const std::string& name) const
{
	return m_RuntimeRenderTextureManager.Has(name);
}

bool VansGraphics::VansMaterialManager::RemoveRuntimeRenderTexture(const std::string& name)
{
	return m_RuntimeRenderTextureManager.Remove(name);
}

bool VansGraphics::VansMaterialManager::UnregisterRuntimeRenderTexture(const std::string& name)
{
	return m_RuntimeRenderTextureManager.Unregister(name);
}

void VansGraphics::VansMaterialManager::ClearRuntimeRenderTextures()
{
	m_RuntimeRenderTextureManager.Clear();
	m_SSGITemporalFrame = 0;
}

void VansGraphics::VansMaterialManager::ClearResolutionDependentRenderData(VkDevice device)
{
	const char* transientTextures[] =
	{
		RT_SSAO_RESULT, RT_SSAO_FILTER_RESULT,
		RT_SSGI_RESULT, RT_SSGI_FILTER_RESULT,
		RT_SSGI_PROBE_CACHE_RADIANCE, RT_SSGI_PROBE_CACHE_SURFACE,
		RT_SSGI_TEMPORAL_A, RT_SSGI_TEMPORAL_B,
		RT_SSGI_MOMENTS_A, RT_SSGI_MOMENTS_B,
		RT_SSGI_SURFACE_HISTORY_A, RT_SSGI_SURFACE_HISTORY_B,
		RT_SSGI_ATROUS_A,
		RT_HZB_RESULT, RT_HZB_OCCLUSION_RESULT,
		RT_SCREEN_SPACE_SHADOW_RESULT, RT_CASCADE_SHADOW_MIN_MAX,
		RT_SSR_HIT_INFO, RT_SSR_RAY_PDF, RT_SSR_RESULT,
		RT_SSRAA_RESULT_A, RT_SSRAA_RESULT_B, RT_SSRAA_RESULT,
		RT_VOLUMETRIC_FOG_RESULT,
		RT_FOG_VOXEL_INJECTION, RT_FOG_VOXEL_INJECTION_HISTORY,
		RT_FOG_VOXEL_RAYMARCH,
		RT_CLOUD_BUFFER, RT_CLOUD_MAIN_NOISE, RT_CLOUD_DETAIL_NOISE,
		RT_EXPOSURE_LUMINANCE, RT_EXPOSURE_CURRENT, RT_UPSCALER_EXPOSURE,
		RT_DOF_RESULT, RT_BLOOM_PREFILTER,
		RT_BLOOM_MIP0, RT_BLOOM_MIP1, RT_BLOOM_MIP2, RT_BLOOM_MIP3,
		RT_BLOOM_UP_MIP2, RT_BLOOM_UP_MIP1, RT_BLOOM_UP_MIP0,
		RT_BLOOM_BASE, RT_BLOOM_RESULT,
	};
	for (const char* name : transientTextures)
		RemoveRuntimeRenderTexture(name);

	m_ScreenSpaceShadowParamsCBBuffer.DestroyVulkanBuffer(device);
	m_FogParamsCBBuffer.DestroyVulkanBuffer(device);
	m_FogVolumeParamsCBBuffer.DestroyVulkanBuffer(device);
	m_SSGITemporalCBBuffer.DestroyVulkanBuffer(device);
	m_SSGICBBuffer.DestroyVulkanBuffer(device);
	m_CloudParamsCBBuffer.DestroyVulkanBuffer(device);
	m_TileLightHeaderBuffer.DestroyVulkanBuffer(device);
	m_TileLightIndexBuffer.DestroyVulkanBuffer(device);
	m_TileLightBuildParamsCBBuffer.DestroyVulkanBuffer(device);
	m_SSRRayListBuffer.DestroyVulkanBuffer(device);
	m_SSRTraceControlBuffer.DestroyVulkanBuffer(device);
	m_PostProcessParamsCBBuffer.DestroyVulkanBuffer(device);
	m_ExposureAdaptParamsCBBuffer.DestroyVulkanBuffer(device);
	m_BloomParamsCBBuffer.DestroyVulkanBuffer(device);
	m_BloomShapeParamsCBBuffer.DestroyVulkanBuffer(device);
	m_DepthOfFieldParamsCBBuffer.DestroyVulkanBuffer(device);

	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_SSGIDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_SSGITexSetLayout);
	descMgr->DestroyDescriptorSet(m_SSGIProbeCacheDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_SSGIProbeCacheSetLayout);
	for (VkDescriptorSetLayout& layout : m_HZBTexSetLayouts)
		descMgr->DestroyDescriptorSetLayout(layout);
	m_HZBTexSetLayouts.clear();
	descMgr->DestroyDescriptorSet(m_HZBDescriptorSets);
	for (VkDescriptorSetLayout& layout : m_OcclusionHZBTexSetLayouts)
		descMgr->DestroyDescriptorSetLayout(layout);
	m_OcclusionHZBTexSetLayouts.clear();
	descMgr->DestroyDescriptorSet(m_OcclusionHZBDescriptorSets);
	descMgr->DestroyDescriptorSet(m_SSRTraceDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_SSRTraceSetLayout);
	descMgr->DestroyDescriptorSet(m_ScreenSpaceShadowDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_ScreenSpaceShadowSetLayout);
	descMgr->DestroyDescriptorSet(m_CascadeShadowMinMaxDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_CascadeShadowMinMaxSetLayout);
	m_CascadeShadowMinMaxMipCount = 0;
	descMgr->DestroyDescriptorSet(m_MainCameraHiZCullDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_MainCameraHiZCullSetLayout);
	descMgr->DestroyDescriptorSet(m_SSRResolveDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_SSRResolveSetLayout);
	descMgr->DestroyDescriptorSet(m_SSRAADescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_SSRAASetLayout);
	descMgr->DestroyDescriptorSet(m_BilateralFilterDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_BilateralFilterSetLayout);
	descMgr->DestroyDescriptorSet(m_VolumetricFogDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_VolumetricFogSetLayout);
	descMgr->DestroyDescriptorSet(m_FogLightInjectionDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_FogLightInjectionSetLayout);
	descMgr->DestroyDescriptorSet(m_FogRayMarchDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_FogRayMarchSetLayout);
	descMgr->DestroyDescriptorSet(m_SSGITemporalDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_SSGITemporalSetLayout);
	descMgr->DestroyDescriptorSet(m_SSGIAtrousDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_SSGIAtrousSetLayout);
	descMgr->DestroyDescriptorSet(m_HIZSeedDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_HIZSeedSetLayout);
	descMgr->DestroyDescriptorSet(m_OcclusionHIZSeedDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_OcclusionHIZSeedSetLayout);
	descMgr->DestroyDescriptorSet(m_CloudRayMarchDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_CloudRayMarchSetLayout);
	descMgr->DestroyDescriptorSet(m_TileLightBuildDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_TileLightBuildSetLayout);
	descMgr->DestroyDescriptorSet(m_ExposureLuminanceDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_ExposureLuminanceSetLayout);
	descMgr->DestroyDescriptorSet(m_ExposureAdaptDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_ExposureAdaptSetLayout);
	descMgr->DestroyDescriptorSet(m_BloomPrefilterDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_BloomPrefilterSetLayout);
	descMgr->DestroyDescriptorSet(m_BloomDownsampleDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_BloomDownsampleSetLayout);
	descMgr->DestroyDescriptorSet(m_BloomUpsampleDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_BloomUpsampleSetLayout);
	descMgr->DestroyDescriptorSet(m_BloomShapeDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_BloomShapeSetLayout);
	descMgr->DestroyDescriptorSet(m_DepthOfFieldDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_DepthOfFieldSetLayout);

	m_HIZMipCount = 0;
	m_SSGITemporalFrame = 0;
}

void VansGraphics::VansMaterialManager::ReleaseSkinProfileLUTLayerForMaterial(int materialIndex)
{
	const auto found = m_SkinMaterialDynamicLUTLayers.find(materialIndex);
	if (found == m_SkinMaterialDynamicLUTLayers.end())
		return;

	const int layer = found->second;
	m_SkinMaterialDynamicLUTLayers.erase(found);
	for (auto& entry : m_SkinProfileDynamicLUTCache)
	{
		if (entry.layer == layer && entry.refCount > 0)
		{
			--entry.refCount;
			break;
		}
	}
}

void VansGraphics::VansMaterialManager::ResetSkinProfileLUTCache()
{
	m_SkinProfileDynamicLUTCache.clear();
	m_PendingSkinProfileLUTUploads.clear();
	m_SkinMaterialDynamicLUTLayers.clear();
}

bool VansGraphics::VansMaterialManager::ResolveSkinProfileLUTForMaterial(
	VansSkinMaterial& skin,
	VansSkinGPUParam& payload,
	VansVKCommandBuffer* immediateCommandBuffer)
{
	const int materialIndex = skin.m_MaterialIndex;
	if (materialIndex < 0)
		return false;

	const int requestedLayer = static_cast<int>(std::lround(payload.profileLUT.x));
	if (skin.m_UseExplicitSkinProfileLUTLayer && requestedLayer >= 0)
	{
		ReleaseSkinProfileLUTLayerForMaterial(materialIndex);
		payload.profileLUT.x = static_cast<float>(std::clamp(requestedLayer, 0, VANS_SKIN_PROFILE_LUT_LAYER_COUNT - 1));
		skin.m_SkinParams.profileLUT.x = payload.profileLUT.x;
		return true;
	}

	if (requestedLayer >= 0 && !IsDynamicSkinProfileLUTLayer(requestedLayer))
	{
		ReleaseSkinProfileLUTLayerForMaterial(materialIndex);
		payload.profileLUT.x = static_cast<float>(std::clamp(requestedLayer, 0, VANS_SKIN_PROFILE_LUT_LAYER_COUNT - 1));
		skin.m_SkinParams.profileLUT.x = payload.profileLUT.x;
		return true;
	}

	if (!m_SkinProfileLUTArray)
	{
		ReleaseSkinProfileLUTLayerForMaterial(materialIndex);
		payload.profileLUT.x = -1.0f;
		skin.m_SkinParams.profileLUT.x = -1.0f;
		return false;
	}

	auto findEntryByLayer = [&](int layer) -> SkinProfileLUTCacheEntry*
	{
		for (auto& entry : m_SkinProfileDynamicLUTCache)
		{
			if (entry.layer == layer)
				return &entry;
		}
		return nullptr;
	};
	auto findEntryByFingerprint = [&](const VansSkinProfileLUTFingerprint& fingerprint) -> SkinProfileLUTCacheEntry*
	{
		for (auto& entry : m_SkinProfileDynamicLUTCache)
		{
			if (entry.layer >= 0 && entry.fingerprint == fingerprint)
				return &entry;
		}
		return nullptr;
	};
	auto allocateEntry = [&](const VansSkinProfileLUTFingerprint& fingerprint) -> SkinProfileLUTCacheEntry*
	{
		for (auto& entry : m_SkinProfileDynamicLUTCache)
		{
			if (entry.refCount <= 0)
			{
				entry.fingerprint = fingerprint;
				entry.refCount = 0;
				return &entry;
			}
		}

		for (int layer = VANS_FIRST_DYNAMIC_SKIN_PROFILE_LUT_LAYER;
			layer < VANS_SKIN_PROFILE_LUT_LAYER_COUNT;
			++layer)
		{
			if (!findEntryByLayer(layer))
			{
				m_SkinProfileDynamicLUTCache.push_back({ fingerprint, layer, 0 });
				return &m_SkinProfileDynamicLUTCache.back();
			}
		}
		return nullptr;
	};
	auto uploadLayer = [&](int layer) -> bool
	{
		std::vector<uint8_t> pixels;
		if (!GenerateSkinProfileLUTPixels(
				payload,
				VANS_SKIN_PROFILE_LUT_SIZE,
				VANS_SKIN_PROFILE_LUT_SIZE,
				pixels))
		{
			return false;
		}

		if (immediateCommandBuffer)
		{
			return m_SkinProfileLUTArray->UpdateArrayLayerFromPixels(
				*immediateCommandBuffer,
				pixels.data(),
				VANS_SKIN_PROFILE_LUT_SIZE,
				VANS_SKIN_PROFILE_LUT_SIZE,
				layer);
		}

		for (auto& pending : m_PendingSkinProfileLUTUploads)
		{
			if (pending.layer == layer)
			{
				pending.fingerprint = BuildSkinProfileLUTFingerprint(payload);
				pending.pixels = std::move(pixels);
				return true;
			}
		}
		m_PendingSkinProfileLUTUploads.push_back({
			BuildSkinProfileLUTFingerprint(payload),
			layer,
			std::move(pixels)
		});
		return true;
	};
	auto assignLayer = [&](SkinProfileLUTCacheEntry& entry)
	{
		++entry.refCount;
		m_SkinMaterialDynamicLUTLayers[materialIndex] = entry.layer;
		payload.profileLUT.x = static_cast<float>(entry.layer);
		skin.m_SkinParams.profileLUT.x = payload.profileLUT.x;
		return true;
	};
	auto fallbackToLegacyLUT = [&]()
	{
		ReleaseSkinProfileLUTLayerForMaterial(materialIndex);
		payload.profileLUT.x = -1.0f;
		skin.m_SkinParams.profileLUT.x = -1.0f;
		return false;
	};

	const VansSkinProfileLUTFingerprint fingerprint = BuildSkinProfileLUTFingerprint(payload);
	if (const auto current = m_SkinMaterialDynamicLUTLayers.find(materialIndex);
		current != m_SkinMaterialDynamicLUTLayers.end())
	{
		if (SkinProfileLUTCacheEntry* entry = findEntryByLayer(current->second))
		{
			if (entry->fingerprint == fingerprint)
			{
				payload.profileLUT.x = static_cast<float>(entry->layer);
				skin.m_SkinParams.profileLUT.x = payload.profileLUT.x;
				return true;
			}

			if (entry->refCount <= 1)
			{
				if (!uploadLayer(entry->layer))
					return fallbackToLegacyLUT();
				entry->fingerprint = fingerprint;
				entry->refCount = 1;
				payload.profileLUT.x = static_cast<float>(entry->layer);
				skin.m_SkinParams.profileLUT.x = payload.profileLUT.x;
				return true;
			}
		}

		ReleaseSkinProfileLUTLayerForMaterial(materialIndex);
	}

	if (requestedLayer >= 0 && IsDynamicSkinProfileLUTLayer(requestedLayer))
	{
		SkinProfileLUTCacheEntry* inheritedEntry = findEntryByLayer(requestedLayer);
		if (!inheritedEntry)
		{
			m_SkinProfileDynamicLUTCache.push_back({ fingerprint, requestedLayer, 0 });
			inheritedEntry = &m_SkinProfileDynamicLUTCache.back();
		}
		else if (inheritedEntry->refCount <= 0)
		{
			inheritedEntry->fingerprint = fingerprint;
		}
		return assignLayer(*inheritedEntry);
	}

	if (SkinProfileLUTCacheEntry* entry = findEntryByFingerprint(fingerprint))
		return assignLayer(*entry);

	SkinProfileLUTCacheEntry* entry = allocateEntry(fingerprint);
	if (!entry)
	{
		if (immediateCommandBuffer)
		{
			VANS_LOG_WARN("[SkinProfileLUT] Dynamic Skin profile LUT cache is full; material '"
				<< skin.m_AssetName << "' keeps legacy 2D LUT fallback.");
		}
		return fallbackToLegacyLUT();
	}

	if (!uploadLayer(entry->layer))
	{
		if (immediateCommandBuffer)
		{
			VANS_LOG_WARN("[SkinProfileLUT] Dynamic Skin profile LUT generation failed; material '"
				<< skin.m_AssetName << "' keeps legacy 2D LUT fallback.");
		}
		return fallbackToLegacyLUT();
	}

	return assignLayer(*entry);
}

bool VansGraphics::VansMaterialManager::RecordPendingSkinProfileLUTUploads(
	VansVKCommandBuffer& commandBuffer)
{
	if (m_PendingSkinProfileLUTUploads.empty())
		return true;
	if (!m_SkinProfileLUTArray)
	{
		m_PendingSkinProfileLUTUploads.clear();
		return false;
	}

	bool uploadedAll = true;
	for (const auto& pending : m_PendingSkinProfileLUTUploads)
	{
		uploadedAll &= m_SkinProfileLUTArray->RecordArrayLayerUploadFromPixels(
			commandBuffer,
			pending.pixels.data(),
			VANS_SKIN_PROFILE_LUT_SIZE,
			VANS_SKIN_PROFILE_LUT_SIZE,
			pending.layer);
	}
	m_PendingSkinProfileLUTUploads.clear();
	return uploadedAll;
}

bool VansGraphics::VansMaterialManager::RewriteGlobalBindlessTextureDescriptors(
	VkDescriptorSet sceneGlobalDescriptorSet)
{
	std::lock_guard<std::mutex> lock(m_GlobalPBRTexturesMutex);
	if (m_GlobalPBRTextures.empty())
		return false;

	std::vector<VkDescriptorImageInfo> infos;
	infos.reserve(m_GlobalPBRTextures.size());
	for (VansVKImage* image : m_GlobalPBRTextures)
	{
		if (!image)
			return false;
		infos.push_back({
			image->GetSampler(),
			image->GetImageView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		});
	}

	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->BeginDescriptorUpdate();
	if (sceneGlobalDescriptorSet != VK_NULL_HANDLE)
	{
		descMgr->WriteImageDescriptor(
			sceneGlobalDescriptorSet,
			GLOBAL_BINDING_BINDLESS_TEXTURES,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			infos);
	}
	if (!m_GlobalPBRTexDescriptorSets.empty() &&
		m_GlobalPBRTexDescriptorSets[0] != VK_NULL_HANDLE)
	{
		descMgr->WriteImageDescriptor(
			m_GlobalPBRTexDescriptorSets[0],
			GLOBAL_BINDING_BINDLESS_TEXTURES,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			infos);
	}
	descMgr->CommitDescriptorUpdates();
	return true;
}

bool VansGraphics::VansMaterialManager::ReplaceGlobalBindlessTexture(
	std::size_t textureIndex,
	VansTexture* texture,
	VkDescriptorSet sceneGlobalDescriptorSet)
{
	std::lock_guard<std::mutex> lock(m_GlobalPBRTexturesMutex);
	if (!texture || textureIndex >= m_GlobalPBRTextures.size())
		return false;

	m_GlobalPBRTextures[textureIndex] = &texture->GetImage();
	m_GlobalBindlessDescriptorsDirty = true;
	return true;
}

void VansGraphics::VansMaterialManager::ClearScenePBRData(VkDevice device)
{
	ClearRuntimeMaterialInstances();
	ResetSkinProfileLUTCache();
	m_FreeRuntimePBRIndices.clear();
	m_FreeRuntimeCustomIndices.clear();
	auto deleteTexture = [](VansTexture*& texture)
	{
		delete texture;
		texture = nullptr;
	};

	// Clear CPU PBR arrays. Material objects are owned by VansScene.
	m_GlobalPBRMaterial.clear();
	m_GlobalPBRParamData.clear();
	m_GlobalClothParamData.clear();
	m_GlobalTreeLeafParamData.clear();
	m_GlobalSkinParamData.clear();
	m_GlobalCustomMaterialParamData.clear();
	m_GlobalBindlessDescriptorsDirty = false;
	m_GlobalPBRTextures.clear();
	ClearResolutionDependentRenderData(device);
	ClearRuntimeRenderTextures();
	m_RectLightEmissiveArray = nullptr;
	deleteTexture(m_PreConvDiffuse);
	deleteTexture(m_EnvironmentRadiance);
	deleteTexture(m_PreConvSpecular);
	deleteTexture(m_BRDFIntegralLUT);
	deleteTexture(m_SkinBSDFLUT);
	deleteTexture(m_SkinProfileLUTArray);
	deleteTexture(m_ClothBRDFLUT);
	deleteTexture(m_MoonAlbedoTexture);
	deleteTexture(m_LTC1);
	deleteTexture(m_LTC2);

	// Destroy GPU buffers.
	m_GlobalPBRDataBuffer.DestroyVulkanBuffer(device);
	m_GlobalClothDataBuffer.DestroyVulkanBuffer(device);
	m_GlobalTreeLeafDataBuffer.DestroyVulkanBuffer(device);
	m_GlobalSkinDataBuffer.DestroyVulkanBuffer(device);
	m_GlobalCustomMaterialDataBuffer.DestroyVulkanBuffer(device);
	m_SkySHResultBuffer.DestroyVulkanBuffer(device);
	m_AtmospherePBRDataBuffer.DestroyVulkanBuffer(device);

	// Release descriptor sets and layouts.
	auto descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_GlobalPBRDataDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GlobalPBRDataSetLayout);
	descMgr->DestroyDescriptorSet(m_GlobalPBRTexDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GlobalPBRTexSetLayout);
	descMgr->DestroyDescriptorSet(m_MaterialAtmosphereDataDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_MaterialAtmosphereDataLayout);
	descMgr->DestroyDescriptorSet(m_BRDFInterationTextDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_BRDFInterationTexSetLayout);
	descMgr->DestroyDescriptorSet(m_PunctualShadowDebugDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_PunctualShadowDebugSetLayout);
}

bool VansGraphics::VansMaterialManager::FlushMaterialPayload(VansMaterial& material)
{
	const int index = material.GetGlobalMaterialIndex();
	if (index < 0)
		return false;

	auto stagePbrPayload = [&](const VansBasePBRParam& payload) -> bool
	{
		if (index >= static_cast<int>(m_GlobalPBRParamData.size()))
			return false;
		m_GlobalPBRParamData[index] = payload;
		return true;
	};

	if (auto* pbr = dynamic_cast<VansPBRMaterial*>(&material))
		return stagePbrPayload(pbr->m_BasePBRParam);
	if (auto* emissive = dynamic_cast<VansEmissiveMaterial*>(&material))
		return stagePbrPayload(emissive->m_BasePBRParam);
	if (auto* decal = dynamic_cast<VansDecalMaterial*>(&material))
		return stagePbrPayload(decal->m_BasePBRParam);
	if (auto* sss = dynamic_cast<VansSubsurfaceMaterial*>(&material))
		return stagePbrPayload(sss->m_BasePBRParam);
	if (auto* skin = dynamic_cast<VansSkinMaterial*>(&material))
	{
		const bool pbrUpdated = stagePbrPayload(skin->m_BasePBRParam);
		VansSkinGPUParam skinPayload = skin->BuildGPUParam();
		ResolveSkinProfileLUTForMaterial(*skin, skinPayload, nullptr);
		if (index >= static_cast<int>(m_GlobalSkinParamData.size()))
			return false;
		m_GlobalSkinParamData[index] = skinPayload;
		return pbrUpdated;
	}
	if (auto* cloth = dynamic_cast<VansClothMaterial*>(&material))
	{
		const bool pbrUpdated = stagePbrPayload(cloth->m_BasePBRParam);
		if (index >= static_cast<int>(m_GlobalClothParamData.size()))
			return false;
		const VansClothGPUParam clothPayload = cloth->BuildGPUParam();
		m_GlobalClothParamData[index] = clothPayload;
		return pbrUpdated;
	}

	if (index >= static_cast<int>(m_GlobalCustomMaterialParamData.size()))
		return false;
	for (int valueIndex = 0; valueIndex < VANS_CUSTOM_MATERIAL_VEC4_COUNT; ++valueIndex)
		m_GlobalCustomMaterialParamData[index].values[valueIndex] =
			material.m_CustomMaterialPayload.values[valueIndex];
	return true;
}

VansGraphics::VansRenderMaterialFrameData
VansGraphics::VansMaterialManager::CaptureRenderMaterialFrameData(
	const std::vector<VansMaterial*>& activeMaterials)
{
	VANS_ASSERT_MAIN_THREAD();
	std::unordered_set<VansMaterial*> uniqueMaterials;
	uniqueMaterials.reserve(activeMaterials.size());
	for (VansMaterial* material : activeMaterials)
	{
		if (material && uniqueMaterials.insert(material).second)
			FlushMaterialPayload(*material);
	}

	const auto copyBytes = [](const auto& source, VansRenderMaterialBufferSnapshot& target)
	{
		using Element = typename std::decay_t<decltype(source)>::value_type;
		target.elementStride = static_cast<std::uint32_t>(sizeof(Element));
		target.bytes.resize(source.size() * sizeof(Element));
		if (!target.bytes.empty())
			std::memcpy(target.bytes.data(), source.data(), target.bytes.size());
	};

	VansRenderMaterialFrameData frameData;
	copyBytes(m_GlobalPBRParamData, frameData.pbr);
	copyBytes(m_GlobalClothParamData, frameData.cloth);
	copyBytes(m_GlobalTreeLeafParamData, frameData.treeLeaf);
	copyBytes(m_GlobalSkinParamData, frameData.skin);
	copyBytes(m_GlobalCustomMaterialParamData, frameData.custom);
	frameData.rewriteBindlessTextures = m_GlobalBindlessDescriptorsDirty;
	m_GlobalBindlessDescriptorsDirty = false;
	frameData.prepared = true;
	return frameData;
}

bool VansGraphics::VansMaterialManager::UploadRenderMaterialFrameData(
	const VansRenderMaterialFrameData& frameData)
{
	VANS_ASSERT_NOT_MAIN_THREAD();
	if (!frameData.prepared)
		return false;

	const auto upload = [](const VansRenderMaterialBufferSnapshot& source,
		VansVKBuffer& target, std::uint32_t expectedStride)
	{
		if (source.elementStride != expectedStride ||
			source.bytes.size() > static_cast<std::size_t>(target.GetBufferSize()) ||
			(!source.bytes.empty() && target.GetNativeBuffer() == VK_NULL_HANDLE))
		{
			return false;
		}
		if (!source.bytes.empty())
			target.UpdateMapped(source.bytes.data(), 0, source.bytes.size());
		return true;
	};

	if (!upload(frameData.pbr, m_GlobalPBRDataBuffer, sizeof(VansBasePBRParam)) ||
		!upload(frameData.cloth, m_GlobalClothDataBuffer, sizeof(VansClothGPUParam)) ||
		!upload(frameData.treeLeaf, m_GlobalTreeLeafDataBuffer, sizeof(VansTreeLeafParamsGPU)) ||
		!upload(frameData.skin, m_GlobalSkinDataBuffer, sizeof(VansSkinGPUParam)) ||
		!upload(frameData.custom, m_GlobalCustomMaterialDataBuffer, sizeof(VansCustomMaterialPayload)))
	{
		return false;
	}
	return !frameData.rewriteBindlessTextures ||
		RewriteGlobalBindlessTextureDescriptors();
}

bool VansGraphics::VansMaterialManager::UploadAtmosphereFrameData(
	const VansAtmospherePBRParam& payload)
{
	VANS_ASSERT_NOT_MAIN_THREAD();
	if (m_AtmospherePBRDataBuffer.GetNativeBuffer() == VK_NULL_HANDLE ||
		m_AtmospherePBRDataBuffer.GetBufferSize() < sizeof(payload))
	{
		return false;
	}
	m_AtmospherePBRDataBuffer.SetBufferData(
		&payload, 0, sizeof(VansAtmospherePBRParam));
	return true;
}

bool VansGraphics::VansMaterialManager::ApplyMaterialParameter(
	VansMaterial& material,
	const std::string& parameterPath,
	const VansMaterialParameterValue& value)
{
	const std::string key = parameterPath;
	if (key.rfind("customParameters/", 0) == 0)
	{
		const std::string customName = key.substr(std::string("customParameters/").size());
		auto slot = material.m_CustomParameterSlots.find(customName);
		if (slot != material.m_CustomParameterSlots.end() &&
			slot->second >= 0 && slot->second < VANS_CUSTOM_MATERIAL_VEC4_COUNT)
		{
			material.m_CustomMaterialPayload.values[slot->second] = ReadMaterialVec4Value(value);
			FlushMaterialPayload(material);
			return true;
		}
	}
	if (material.m_MaterialType == VansMaterialType::VAN_CUSTOM_SHADER)
	{
		auto slot = material.m_CustomParameterSlots.find(key);
		if (slot != material.m_CustomParameterSlots.end() &&
			slot->second >= 0 && slot->second < VANS_CUSTOM_MATERIAL_VEC4_COUNT)
		{
			material.m_CustomMaterialPayload.values[slot->second] = ReadMaterialVec4Value(value);
			FlushMaterialPayload(material);
			return true;
		}
	}
	if (material.m_MaterialType == VansMaterialType::VAN_PBR_TRANSMISSION)
	{
		glm::vec3 color;
		float scalar = 0.0f;
		bool changed = false;

		if ((key == "color" || key == "albedo" || key == "baseColor" || key == "basecolor") && ReadMaterialVec3(value, color))
		{
			material.m_CustomMaterialPayload.values[0].x = color.x;
			material.m_CustomMaterialPayload.values[0].y = color.y;
			material.m_CustomMaterialPayload.values[0].z = color.z;
			changed = true;
		}
		else if (key == "alphaCoverage" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[0].w = std::clamp(scalar, 0.0f, 1.0f);
			changed = true;
		}
		else if (key == "roughness" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[1].x = std::clamp(scalar, 0.0f, 1.0f);
			changed = true;
		}
		else if (key == "transmission" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[1].y = std::clamp(scalar, 0.0f, 1.0f);
			changed = true;
		}
		else if (key == "ior" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[1].z = std::max(scalar, 1.0001f);
			changed = true;
		}
		else if (key == "thickness" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[1].w = std::max(scalar, 0.0f);
			changed = true;
		}
		else if (key == "attenuationColor" && ReadMaterialVec3(value, color))
		{
			material.m_CustomMaterialPayload.values[2].x = color.x;
			material.m_CustomMaterialPayload.values[2].y = color.y;
			material.m_CustomMaterialPayload.values[2].z = color.z;
			changed = true;
		}
		else if (key == "attenuationDistance" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[2].w = std::max(scalar, 0.0f);
			changed = true;
		}
		else if (key == "normalScale" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[3].x = scalar;
			changed = true;
		}
		else if (key == "refractionStrength" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[3].y = std::clamp(scalar, 0.0f, 1.0f);
			changed = true;
		}
		else if (key == "reflectionStrength" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[3].z = std::clamp(scalar, 0.0f, 1.0f);
			changed = true;
		}
		else if (key == "refractionMode" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[3].w = std::clamp(scalar, 0.0f, 2.0f);
			changed = true;
		}
		else if (key == "scatteringColor" && ReadMaterialVec3(value, color))
		{
			material.m_CustomMaterialPayload.values[4].x = std::max(color.x, 0.0f);
			material.m_CustomMaterialPayload.values[4].y = std::max(color.y, 0.0f);
			material.m_CustomMaterialPayload.values[4].z = std::max(color.z, 0.0f);
			changed = true;
		}
		else if (key == "scatteringStrength" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[4].w = std::max(scalar, 0.0f);
			changed = true;
		}

		if (changed)
		{
			FlushMaterialPayload(material);
			return true;
		}
	}
	if (auto* pbr = dynamic_cast<VansPBRMaterial*>(&material))
	{
		glm::vec3 color;
		if ((key == "albedo" || key == "baseColor" || key == "basecolor") && ReadMaterialVec3(value, color))
		{
			pbr->m_BasePBRParam.m_albedo = color;
			FlushMaterialPayload(material);
			return true;
		}
		float scalar = 0.0f;
		if (key == "roughness" && ReadMaterialFloat(value, scalar))
		{
			pbr->m_BasePBRParam.m_roughness = scalar;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "metallic" && ReadMaterialFloat(value, scalar))
		{
			pbr->m_BasePBRParam.m_metallic = scalar;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "ao" && ReadMaterialFloat(value, scalar))
		{
			pbr->m_BasePBRParam.m_ao = scalar;
			FlushMaterialPayload(material);
			return true;
		}
	}
	else if (auto* skin = dynamic_cast<VansSkinMaterial*>(&material))
	{
		std::string profileName;
		if ((key == "skinProfile" || key == "skinProfileName" || key == "profile") &&
			ReadMaterialString(value, profileName))
		{
			if (!skin->ApplySkinProfilePreset(profileName))
				return false;
			skin->m_UseExplicitSkinProfileLUTLayer = false;
			FlushMaterialPayload(material);
			return true;
		}
		glm::vec3 color;
		auto markSkinProfileLUTDirty = [&]()
		{
			if (!skin->m_UseExplicitSkinProfileLUTLayer)
				skin->m_SkinParams.profileLUT.x = -1.0f;
		};
		if ((key == "scatterColor" || key == "subsurfaceColor" || key == "sssColor") && ReadMaterialVec3(value, color))
		{
			skin->m_BasePBRParam.m_albedo = glm::max(color, glm::vec3(0.0f));
			markSkinProfileLUTDirty();
			FlushMaterialPayload(material);
			return true;
		}
		float scalar = 0.0f;
		if (key == "roughness" && ReadMaterialFloat(value, scalar))
		{
			skin->m_BasePBRParam.m_roughness = std::clamp(scalar, 0.045f, 1.0f);
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "normalStrength" && ReadMaterialFloat(value, scalar))
		{
			skin->m_BasePBRParam.m_metallic = std::clamp(scalar, 0.0f, 2.0f);
			FlushMaterialPayload(material);
			return true;
		}
		if ((key == "scatterAmount" || key == "subsurfaceAmount" || key == "sssAmount") && ReadMaterialFloat(value, scalar))
		{
			skin->m_BasePBRParam.m_ao = std::clamp(scalar, 0.0f, 1.0f);
			markSkinProfileLUTDirty();
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "specularScale" && ReadMaterialFloat(value, scalar))
		{
			skin->m_BasePBRParam.padding = std::clamp(scalar, 0.0f, 4.0f);
			FlushMaterialPayload(material);
			return true;
		}
		if ((key == "transmissionScale" || key == "backTransmissionScale") && ReadMaterialFloat(value, scalar))
		{
			skin->m_SkinParams.roughnessNormalSpecular.w = std::clamp(scalar, 0.0f, 4.0f);
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "primaryRoughnessScale" && ReadMaterialFloat(value, scalar))
		{
			skin->m_SkinParams.lobeIOR.x = std::clamp(scalar, 0.1f, 4.0f);
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "secondaryRoughnessScale" && ReadMaterialFloat(value, scalar))
		{
			skin->m_SkinParams.lobeIOR.y = std::clamp(scalar, 0.1f, 4.0f);
			FlushMaterialPayload(material);
			return true;
		}
		if ((key == "ior" || key == "skinIor") && ReadMaterialFloat(value, scalar))
		{
			skin->m_SkinParams.lobeIOR.z = std::clamp(scalar, 1.0f, 2.5f);
			FlushMaterialPayload(material);
			return true;
		}
		if ((key == "specularLobeMix" || key == "primaryLobeWeight") && ReadMaterialFloat(value, scalar))
		{
			skin->m_SkinParams.lobeIOR.w = std::clamp(scalar, 0.0f, 1.0f);
			FlushMaterialPayload(material);
			return true;
		}
		if ((key == "diffusionRadiusScale" || key == "scatterRadiusScale" || key == "skinScatterRadius") &&
			ReadMaterialFloat(value, scalar))
		{
			skin->m_SkinParams.profileControls.x = std::clamp(scalar, 0.05f, 4.0f);
			markSkinProfileLUTDirty();
			FlushMaterialPayload(material);
			return true;
		}
		if ((key == "thinnessScale" || key == "skinThinnessScale") && ReadMaterialFloat(value, scalar))
		{
			skin->m_SkinParams.profileControls.y = std::clamp(scalar, 0.10f, 4.0f);
			FlushMaterialPayload(material);
			return true;
		}
		if ((key == "transmissionDepthScale" || key == "opticalDepthScale" || key == "skinOpticalDepthScale") &&
			ReadMaterialFloat(value, scalar))
		{
			skin->m_SkinParams.profileControls.z = std::clamp(scalar, 0.05f, 4.0f);
			markSkinProfileLUTDirty();
			FlushMaterialPayload(material);
			return true;
		}
		if ((key == "ambientScatterScale" || key == "skinAmbientScatterScale") && ReadMaterialFloat(value, scalar))
		{
			skin->m_SkinParams.profileControls.w = std::clamp(scalar, 0.0f, 1.0f);
			FlushMaterialPayload(material);
			return true;
		}
		if ((key == "scatterRadiusRGB" || key == "profileScatterRadius") &&
			ReadMaterialVec3(value, color))
		{
			skin->m_SkinParams.profileShape.x = std::clamp(color.x, 0.05f, 4.0f);
			skin->m_SkinParams.profileShape.y = std::clamp(color.y, 0.05f, 4.0f);
			skin->m_SkinParams.profileShape.z = std::clamp(color.z, 0.05f, 4.0f);
			markSkinProfileLUTDirty();
			FlushMaterialPayload(material);
			return true;
		}
		if ((key == "boundaryColorBleed" || key == "skinBoundaryBleed") && ReadMaterialFloat(value, scalar))
		{
			skin->m_SkinParams.profileShape.w = std::clamp(scalar, 0.0f, 2.0f);
			markSkinProfileLUTDirty();
			FlushMaterialPayload(material);
			return true;
		}
		if ((key == "skinProfileLutLayer" || key == "profileLutLayer" || key == "skinLutLayer") &&
			ReadMaterialFloat(value, scalar))
		{
			skin->m_SkinParams.profileLUT.x = std::clamp(
				scalar,
				-1.0f,
				static_cast<float>(VANS_SKIN_PROFILE_LUT_LAYER_COUNT - 1));
			skin->m_UseExplicitSkinProfileLUTLayer = skin->m_SkinParams.profileLUT.x >= 0.0f;
			FlushMaterialPayload(material);
			return true;
		}
		if ((key == "skinDebugView" || key == "debugView") && ReadMaterialFloat(value, scalar))
		{
			skin->m_SkinParams.debugControls.x = std::clamp(scalar, 0.0f, 16.0f);
			FlushMaterialPayload(material);
			return true;
		}
	}
	else if (auto* sss = dynamic_cast<VansSubsurfaceMaterial*>(&material))
	{
		glm::vec3 color;
		if ((key == "subsurfaceColor" || key == "color" || key == "albedo" || key == "baseColor" || key == "basecolor") &&
			ReadMaterialVec3(value, color))
		{
			sss->m_SubsurfaceColor = color;
			sss->m_BasePBRParam.m_albedo = color;
			FlushMaterialPayload(material);
			return true;
		}
		float scalar = 0.0f;
		if ((key == "subsurfacePower" || key == "scatteringDistance") && ReadMaterialFloat(value, scalar))
		{
			sss->m_SubsurfacePower = std::max(scalar, 0.01f);
			sss->m_BasePBRParam.m_roughness = sss->m_SubsurfacePower;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "thickness" && ReadMaterialFloat(value, scalar))
		{
			sss->m_Thickness = std::max(scalar, 0.0f);
			sss->m_BasePBRParam.m_metallic = sss->m_Thickness;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "subsurfaceAmount" && ReadMaterialFloat(value, scalar))
		{
			sss->m_SubsurfaceAmount = std::clamp(scalar, 0.0f, 1.0f);
			sss->m_BasePBRParam.m_ao = sss->m_SubsurfaceAmount;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "curvatureInfluence" && ReadMaterialFloat(value, scalar))
		{
			sss->m_CurvatureInfluence = scalar;
			// Retained as a no-op legacy parameter. Curvature is not thickness
			// and must not alter the physical diffusion profile or IOR payload.
			return true;
		}
		if (key == "ior" && ReadMaterialFloat(value, scalar))
		{
			sss->m_IOR = std::clamp(scalar, 1.0f, 2.5f);
			sss->m_BasePBRParam.padding = sss->m_IOR;
			FlushMaterialPayload(material);
			return true;
		}
	}
	else if (auto* decal = dynamic_cast<VansDecalMaterial*>(&material))
	{
		glm::vec3 color;
		if ((key == "albedo" || key == "baseColor" || key == "basecolor" || key == "color") && ReadMaterialVec3(value, color))
		{
			decal->m_BasePBRParam.m_albedo = color;
			FlushMaterialPayload(material);
			return true;
		}
		float scalar = 0.0f;
		if (key == "roughness" && ReadMaterialFloat(value, scalar))
		{
			decal->m_BasePBRParam.m_roughness = scalar;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "metallic" && ReadMaterialFloat(value, scalar))
		{
			decal->m_BasePBRParam.m_metallic = scalar;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "ao" && ReadMaterialFloat(value, scalar))
		{
			decal->m_BasePBRParam.m_ao = scalar;
			FlushMaterialPayload(material);
			return true;
		}
	}
	else if (auto* emissive = dynamic_cast<VansEmissiveMaterial*>(&material))
	{
		glm::vec3 color;
		if ((key == "albedo" || key == "color") && ReadMaterialVec3(value, color))
		{
			emissive->m_BasePBRParam.m_albedo = color;
			FlushMaterialPayload(material);
			return true;
		}
		if ((key == "emissive" || key == "emissive_color") && ReadMaterialVec3(value, color) &&
			material.m_MaterialType == VansMaterialType::VAN_EMISSIVE)
		{
			emissive->m_BasePBRParam.m_albedo = color;
			FlushMaterialPayload(material);
			return true;
		}
		float scalar = 0.0f;
		if ((key == "intensity" || key == "emissiveIntensity" || key == "emissive_intensity") &&
			ReadMaterialFloat(value, scalar))
		{
			if (material.m_MaterialType == VansMaterialType::VAN_PBR_EMISSIVE)
				emissive->m_BasePBRParam.padding = std::max(scalar, 0.0f);
			else
			{
				emissive->m_BasePBRParam.m_roughness = scalar;
				emissive->m_BasePBRParam.padding = -1.0f - std::max(scalar, 0.0f);
			}
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "roughness" && ReadMaterialFloat(value, scalar))
		{
			emissive->m_BasePBRParam.m_roughness = scalar;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "metallic" && ReadMaterialFloat(value, scalar))
		{
			emissive->m_BasePBRParam.m_metallic = scalar;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "ao" && ReadMaterialFloat(value, scalar))
		{
			emissive->m_BasePBRParam.m_ao = scalar;
			FlushMaterialPayload(material);
			return true;
		}
	}
	else if (auto* cloth = dynamic_cast<VansClothMaterial*>(&material))
	{
		glm::vec3 color;
		if ((key == "albedo" || key == "baseColor" || key == "basecolor" || key == "color") && ReadMaterialVec3(value, color))
		{
			cloth->m_BasePBRParam.m_albedo = color;
			FlushMaterialPayload(material);
			return true;
		}
		float scalar = 0.0f;
		if ((key == "sheenRoughness" || key == "roughness") && ReadMaterialFloat(value, scalar))
		{
			cloth->m_SheenRoughness = std::clamp(scalar, 0.045f, 1.0f);
			cloth->m_BasePBRParam.m_roughness = cloth->m_SheenRoughness;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "sheenStrength" && ReadMaterialFloat(value, scalar))
		{
			cloth->m_SheenStrength = std::clamp(scalar, 0.0f, 1.0f);
			cloth->m_BasePBRParam.padding = cloth->m_SheenStrength;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "translucency" && ReadMaterialFloat(value, scalar))
		{
			cloth->m_Translucency = std::clamp(scalar, 0.0f, 1.0f);
			cloth->m_BasePBRParam.m_metallic = cloth->m_Translucency;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "anisotropy" && ReadMaterialFloat(value, scalar))
		{
			cloth->m_Anisotropy = std::clamp(scalar, -0.95f, 0.95f);
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "thickness" && ReadMaterialFloat(value, scalar))
		{
			cloth->m_Thickness = std::clamp(scalar, 0.0f, 1.0f);
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "sheenColor" && ReadMaterialVec3(value, color))
		{
			cloth->m_SheenColor = glm::max(color, glm::vec3(0.0f));
			cloth->m_ClothFlags &= ~VANS_CLOTH_FLAG_ALBEDO_SHEEN_TINT;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "transmissionColor" && ReadMaterialVec3(value, color))
		{
			cloth->m_TransmissionColor = glm::max(color, glm::vec3(0.0f));
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "clothModel")
		{
			std::string model;
			if (ReadMaterialString(value, model))
			{
				if (model == "silk" || model == "satin") cloth->m_ClothModel = VansClothModel::Silk;
				else if (model == "thin") cloth->m_ClothModel = VansClothModel::Thin;
				else cloth->m_ClothModel = VansClothModel::Fuzz;
				FlushMaterialPayload(material);
				return true;
			}
		}
		if (key == "ao" && ReadMaterialFloat(value, scalar))
		{
			cloth->m_BasePBRParam.m_ao = std::clamp(scalar, 0.0f, 1.0f);
			FlushMaterialPayload(material);
			return true;
		}
	}

	if (auto* grass = dynamic_cast<VansGrassMaterial*>(&material))
	{
		float scalar = 0.0f;
		if (key == "aoStrength" && ReadMaterialFloat(value, scalar))
		{
			grass->m_GrassParams.aoStrength = std::clamp(scalar, 0.0f, 1.0f);
			return true;
		}
		if (key == "rootAOIntensity" && ReadMaterialFloat(value, scalar))
		{
			grass->m_GrassParams.rootAOIntensity = std::clamp(scalar, 0.0f, 0.85f);
			return true;
		}
		if (key == "rootAOHeight" && ReadMaterialFloat(value, scalar))
		{
			grass->m_GrassParams.rootAOHeight = std::clamp(scalar, 0.01f, 1.0f);
			return true;
		}
	}

	return false;
}

void VansGraphics::VansMaterialManager::ApplyFogSettings(const VansFogSettings& settings)
{
	m_FogSettings = settings;
	if (m_FogParamsCBBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
		return;

	m_FogParamsCBBuffer.SetBufferData(
		&m_FogSettings,
		0,
		sizeof(VansFogSettings));
}

void VansGraphics::VansMaterialManager::ApplyFogVolumeSettings(const VansFogVolumeSettings& settings)
{
	m_FogVolumeSettings = settings;
	if (m_FogVolumeParamsCBBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
		return;

	m_FogVolumeParamsCBBuffer.SetBufferData(
		&m_FogVolumeSettings,
		0,
		sizeof(VansFogVolumeSettings));
}

VansGraphics::VansScreenSpacePunctualShadowSettings
VansGraphics::VansMaterialManager::GetScreenSpacePunctualShadowSettings() const
{
	VansScreenSpacePunctualShadowSettings settings;
	settings.maxTraceDistance = m_ScreenSpaceShadowParams.punctualRayParams.x;
	settings.thickness = m_ScreenSpaceShadowParams.punctualRayParams.y;
	settings.normalBias = m_ScreenSpaceShadowParams.punctualRayParams.z;
	settings.maxSteps = static_cast<uint32_t>((std::max)(m_ScreenSpaceShadowParams.punctualRayParams.w, 1.0f));
	settings.strength = m_ScreenSpaceShadowParams.fadeParams.w;
	return settings;
}

void VansGraphics::VansMaterialManager::ApplyScreenSpacePunctualShadowSettings(
	const VansScreenSpacePunctualShadowSettings& settings)
{
	m_ScreenSpaceShadowParams.punctualRayParams = glm::vec4(
		glm::clamp(settings.maxTraceDistance, 0.25f, 50.0f),
		glm::clamp(settings.thickness, 0.005f, 1.0f),
		glm::clamp(settings.normalBias, 0.001f, 0.25f),
		static_cast<float>(glm::clamp(settings.maxSteps, 8u, 128u)));
	m_ScreenSpaceShadowParams.fadeParams.w = glm::clamp(settings.strength, 0.0f, 1.0f);

	if (m_ScreenSpaceShadowParamsCBBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
	{
		m_ScreenSpaceShadowParamsCBBuffer.SetBufferData(
			&m_ScreenSpaceShadowParams,
			0,
			sizeof(m_ScreenSpaceShadowParams));
	}
}

void VansGraphics::VansMaterialManager::SetScreenSpaceShadowExtent(uint32_t width, uint32_t height)
{
	width = (std::max)(width, 1u);
	height = (std::max)(height, 1u);
	m_ScreenSpaceShadowParams.screenSize = glm::vec4(
		static_cast<float>(width),
		static_cast<float>(height),
		1.0f / static_cast<float>(width),
		1.0f / static_cast<float>(height));

	if (m_ScreenSpaceShadowParamsCBBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
	{
		m_ScreenSpaceShadowParamsCBBuffer.SetBufferData(
			&m_ScreenSpaceShadowParams,
			0,
			sizeof(m_ScreenSpaceShadowParams));
	}
}

void VansGraphics::VansMaterialManager::UpdatePBRLutDescriptorSets()
{
	//update descriptor
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->BeginDescriptorUpdate();
	descMgr->WriteBufferDescriptor(
		m_BRDFInterationTextDescriptorSets[0],
		PassBinding::BUFFER_3,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{ { m_SkySHResultBuffer.GetNativeBuffer(), 0, m_SkySHResultBuffer.GetBufferSize() } });

	descMgr->WriteImageDescriptor(
		m_BRDFInterationTextDescriptorSets[0],
		PassBinding::TEXTURE_0,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { m_BRDFIntegralLUT->GetImage().GetSampler(), m_BRDFIntegralLUT->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(
		m_BRDFInterationTextDescriptorSets[0],
		PassBinding::TEXTURE_1,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { m_PreConvDiffuse->GetImage().GetSampler(), m_PreConvDiffuse->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(
		m_BRDFInterationTextDescriptorSets[0],
		PassBinding::TEXTURE_2,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { m_PreConvSpecular->GetImage().GetSampler(), m_PreConvSpecular->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(
		m_BRDFInterationTextDescriptorSets[0],
		PassBinding::TEXTURE_4,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { m_SkinBSDFLUT->GetImage().GetSampler(), m_SkinBSDFLUT->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });

	// Binding 8: cloth BRDF LUT (split-sum .rg and sheen tint .b).
	if (m_ClothBRDFLUT)
	{
		descMgr->WriteImageDescriptor(
			m_BRDFInterationTextDescriptorSets[0],
			PassBinding::TEXTURE_5,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{ { m_ClothBRDFLUT->GetImage().GetSampler(), m_ClothBRDFLUT->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	}

	descMgr->CommitDescriptorUpdates();
}

void VansGraphics::VansClothMaterial::BuildClothTextureDescriptors()
{
	VansDescriptorSetLayoutFactory::CreateAndAllocate_ClothTexture(m_ClothOwnedLayout, m_ClothOwnedDescSets);

	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->BeginDescriptorUpdate();

	if (m_BaseColorTexture)
	{
		descManager->WriteImageDescriptor(
			m_ClothOwnedDescSets[0],
			CLOTH_TEXTURE_BINDING_ALBEDO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_BaseColorTexture->GetImage().GetSampler(),
				m_BaseColorTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_NormalTexture)
	{
		descManager->WriteImageDescriptor(
			m_ClothOwnedDescSets[0],
			CLOTH_TEXTURE_BINDING_NORMAL,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_NormalTexture->GetImage().GetSampler(),
				m_NormalTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_RoughnessTexture)
	{
		descManager->WriteImageDescriptor(
			m_ClothOwnedDescSets[0],
			CLOTH_TEXTURE_BINDING_ROUGHNESS,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_RoughnessTexture->GetImage().GetSampler(),
				m_RoughnessTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_AoTexture)
	{
		descManager->WriteImageDescriptor(
			m_ClothOwnedDescSets[0],
			CLOTH_TEXTURE_BINDING_AO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AoTexture->GetImage().GetSampler(),
				m_AoTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	descManager->CommitDescriptorUpdates();
}

void VansGraphics::VansMaterialManager::UpdateAtmosphereDescriptorSets()
{
	//update descriptor
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->BeginDescriptorUpdate();
	descMgr->WriteBufferDescriptor(
		m_MaterialAtmosphereDataDescriptorSets[0],
		SKYBOX_BINDING_ATMOSPHERE_UBO,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		{ { m_AtmospherePBRDataBuffer.GetNativeBuffer(), 0, m_AtmospherePBRDataBuffer.GetBufferSize() } });

	VansTexture* volumetricFogResult = GetRuntimeRenderTexture(RT_VOLUMETRIC_FOG_RESULT);
	if (volumetricFogResult != nullptr)
	{
		descMgr->WriteImageDescriptor(
			m_MaterialAtmosphereDataDescriptorSets[0],
			SKYBOX_BINDING_FOG,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{ { volumetricFogResult->GetImage().GetSampler(), volumetricFogResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	}

	// Bind the quarter-resolution volumetric-cloud result to the skybox set.
	VansTexture* cloudBuffer = GetRuntimeRenderTexture(RT_CLOUD_BUFFER);
	if (cloudBuffer != nullptr)
	{
		descMgr->WriteImageDescriptor(
			m_MaterialAtmosphereDataDescriptorSets[0],
			SKYBOX_BINDING_CLOUD,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{ { cloudBuffer->GetImage().GetSampler(), cloudBuffer->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	}
	if (m_MoonAlbedoTexture != nullptr)
	{
		descMgr->WriteImageDescriptor(
			m_MaterialAtmosphereDataDescriptorSets[0],
			SKYBOX_BINDING_MOON_ALBEDO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{ { m_MoonAlbedoTexture->GetImage().GetSampler(), m_MoonAlbedoTexture->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	}
	descMgr->CommitDescriptorUpdates();
}

//void VansGraphics::VansMaterial::CreatePBRMaterialDataBuffer(VkDevice& logic_device)
//{
//	VkDeviceSize bufferSize = sizeof(m_BasePBRParam);
//	m_BasePBRDataBuffer.CreatVulkanBuffer(
//		logic_device, bufferSize, VK_FORMAT_R32_SFLOAT,
//		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
//		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
//	);
//}
//
//void VansGraphics::VansMaterial::UpdatePBRUniformData()
//{
//	uint32_t offset = 0;
//	uint32_t size = sizeof(VansBasePBRParam);
//	m_BasePBRDataBuffer.SetBufferData(&m_BasePBRParam, offset, size);
//}

VansGraphics::VansAtmospherePBRParam
VansGraphics::VansSkyBoxMaterial::BuildAtmosphereFrameData(
	const VansDirectionalLight* directionalLight) const
{
	VansAtmospherePBRParam payload = m_AtmospherePBRParam;
	// Preserve m_SunDirection when the scene has no directional light.
	if (directionalLight == nullptr)
		return payload;

	const auto& dirLight = *directionalLight;
	const VansCelestialLightingState celestialState = VansLightManager::ComputeCelestialLightingState(dirLight);
	const glm::vec3 sunDirection = NormalizeMaterialDirectionSafe(celestialState.sunDirection, glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::vec3 moonDirection = NormalizeMaterialDirectionSafe(celestialState.moonDirection, -sunDirection);
	const glm::vec3 mainCelestialDirection = NormalizeMaterialDirectionSafe(celestialState.direction, sunDirection);
	const float moonBlend = glm::clamp(celestialState.moonBlend, 0.0f, 1.0f);
	const float sunDiskVisibility = m_SunDiskEnabled ? (1.0f - moonBlend) : 0.0f;
	const float moonDiskVisibility = m_MoonDiskEnabled ? moonBlend : 0.0f;
	payload.m_SunDirection = sunDirection;
	// CPU 预计算大气衰减后的太阳颜色，写入 AtmosphereUBO
	// Used by shaders such as VolumeCloud.frag that cannot include LightsData.glsl.
	payload.m_EffectiveSunColor = celestialState.color;
	const float moonPhase = 1.0f;
	const glm::vec3 sunRadiance = glm::max(
		VansLightManager::ComputeAtmosphereSunColor(sunDirection, dirLight.m_Color) *
			dirLight.m_Intensity * payload.m_SunLuminance,
		glm::vec3(0.0f));
	const float moonRadianceScale = (std::max)(m_MoonDiskRadianceScale / kLegacyMoonDiskRadianceScale, 0.0f);
	const glm::vec3 moonRadiance = glm::max(
		celestialState.color * celestialState.intensity * payload.m_SunLuminance,
		glm::vec3(0.0f)) * moonRadianceScale * glm::vec3(0.82f, 0.86f, 1.0f);

	payload.m_SunDiskDirectionAngularRadius = glm::vec4(sunDirection, m_SunDiskAngularRadius);
	payload.m_SunDiskRadianceEnabled = glm::vec4(sunRadiance * m_SunDiskRadianceScale, sunDiskVisibility);
	payload.m_SunDiskParams = glm::vec4(m_SunDiskFeather, 1.0f, m_SunDiskOcclusionStrength, 0.0f);
	payload.m_MoonDiskDirectionAngularRadius = glm::vec4(moonDirection, m_MoonDiskAngularRadius);
	payload.m_MoonDiskRadianceEnabled = glm::vec4(moonRadiance, moonDiskVisibility);
	payload.m_MoonDiskParams = glm::vec4(m_MoonDiskFeather, moonPhase, m_MoonDiskOcclusionStrength, 0.0f);
	payload.m_MainCelestialLightInfo = glm::vec4(mainCelestialDirection, moonBlend);
	return payload;
}

void VansGraphics::VansTransparentMaterial::CreateTransparentDescriptorLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings)
{
	VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(
		bindings,
		m_TransparentOwnedLayout,
		m_TransparentOwnedDescSets);
}

void VansGraphics::VansTransparentMaterial::BuildTransparentTextureDescriptors()
{
	// Build one COMBINED_IMAGE_SAMPLER binding per texture slot (in JSON order).
	const uint32_t slotCount = static_cast<uint32_t>(m_TransparentTextures.size());
	if (slotCount == 0)
	{
		// Create an empty layout when there are no textures so Set 1 remains valid.
		CreateTransparentDescriptorLayout();
		return;
	}

	// 1. Build layout bindings
	std::vector<VkDescriptorSetLayoutBinding> bindings(slotCount);
	for (uint32_t i = 0; i < slotCount; ++i)
	{
		bindings[i] = {};
		bindings[i].binding            = i;  // binding index == slot order in JSON
		bindings[i].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[i].descriptorCount    = 1;
		bindings[i].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
		bindings[i].pImmutableSamplers = nullptr;
	}

	// 2. Create layout & allocate descriptor set
	CreateTransparentDescriptorLayout(bindings);

	// 3. Write texture descriptors
	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->BeginDescriptorUpdate();
	for (uint32_t i = 0; i < slotCount; ++i)
	{
		VansTexture* tex = m_TransparentTextures[i];
		if (tex == nullptr)
			continue; // skip unresolved slots

		descManager->WriteImageDescriptor(
			m_TransparentOwnedDescSets[0],
			i,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{ { tex->GetImage().GetSampler(), tex->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	}
	descManager->CommitDescriptorUpdates();
}

void VansGraphics::VansSkinMaterial::BuildSkinTextureDescriptors()
{
	// Allocate the skin texture descriptor set.
	VansDescriptorSetLayoutFactory::CreateAndAllocate_SkinTexture(m_SkinOwnedLayout, m_SkinOwnedDescSets);

	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->BeginDescriptorUpdate();

	auto writeTexture = [&](uint32_t binding, VansTexture* texture)
	{
		if (!texture)
			return;
		descManager->WriteImageDescriptor(
			m_SkinOwnedDescSets[0],
			binding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				texture->GetImage().GetSampler(),
				texture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	};

	writeTexture(SKIN_TEXTURE_BINDING_ALBEDO,    m_BaseColorTexture);
	writeTexture(SKIN_TEXTURE_BINDING_NORMAL,    m_NormalTexture);
	writeTexture(SKIN_TEXTURE_BINDING_ROUGHNESS, m_RoughnessTexture);
	writeTexture(SKIN_TEXTURE_BINDING_CAVITY,    m_CavityTexture);
	writeTexture(SKIN_TEXTURE_BINDING_SCATTER_MASK, m_ScatterMaskTexture);
	writeTexture(SKIN_TEXTURE_BINDING_THICKNESS, m_ThicknessTexture);

	descManager->CommitDescriptorUpdates();
}

void VansGraphics::VansHairMaterial::BuildHairDescriptors(VkDevice& device)
{
	VansDescriptorSetLayoutFactory::CreateAndAllocate_HairTexture(m_HairOwnedLayout, m_HairOwnedDescSets);
	m_ParamsDevice = device;
	if (m_ParamsBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
	{
		m_ParamsBuffer.CreatVulkanBuffer(
			device,
			sizeof(VansHairParamsGPU),
			VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}
	m_ParamsBuffer.SetBufferData(&m_Params, 0, sizeof(VansHairParamsGPU));

	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->BeginDescriptorUpdate();

	if (m_AlbedoTexture)
	{
		descManager->WriteImageDescriptor(
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_ALBEDO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AlbedoTexture->GetImage().GetSampler(),
				m_AlbedoTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_AlphaTexture)
	{
		descManager->WriteImageDescriptor(
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_ALPHA,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AlphaTexture->GetImage().GetSampler(),
				m_AlphaTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_NormalTexture)
	{
		descManager->WriteImageDescriptor(
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_NORMAL,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_NormalTexture->GetImage().GetSampler(),
				m_NormalTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_RoughnessTexture)
	{
		descManager->WriteImageDescriptor(
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_ROUGHNESS,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_RoughnessTexture->GetImage().GetSampler(),
				m_RoughnessTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_AOTexture)
	{
		descManager->WriteImageDescriptor(
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_AO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AOTexture->GetImage().GetSampler(),
				m_AOTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_ShiftTexture)
	{
		descManager->WriteImageDescriptor(
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_SHIFT,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_ShiftTexture->GetImage().GetSampler(),
				m_ShiftTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_FlowTexture)
	{
		descManager->WriteImageDescriptor(
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_FLOW,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_FlowTexture->GetImage().GetSampler(),
				m_FlowTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_IDTexture)
	{
		descManager->WriteImageDescriptor(
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_ID,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_IDTexture->GetImage().GetSampler(),
				m_IDTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	descManager->WriteBufferDescriptor(
		m_HairOwnedDescSets[0],
		HAIR_TEXTURE_BINDING_PARAMS,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		{{
			m_ParamsBuffer.GetNativeBuffer(),
			0,
			m_ParamsBuffer.GetBufferSize()
		}});

	descManager->CommitDescriptorUpdates();
}

void VansGraphics::VansSubsurfaceMaterial::BuildSubsurfaceTextureDescriptors()
{
	VansDescriptorSetLayoutFactory::CreateAndAllocate_SubsurfaceTexture(m_SubsurfaceOwnedLayout, m_SubsurfaceOwnedDescSets);

	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->BeginDescriptorUpdate();

	if (m_BaseColorTexture)
	{
		descManager->WriteImageDescriptor(
			m_SubsurfaceOwnedDescSets[0],
			SUBSURFACE_TEXTURE_BINDING_ALBEDO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_BaseColorTexture->GetImage().GetSampler(),
				m_BaseColorTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_NormalTexture)
	{
		descManager->WriteImageDescriptor(
			m_SubsurfaceOwnedDescSets[0],
			SUBSURFACE_TEXTURE_BINDING_NORMAL,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_NormalTexture->GetImage().GetSampler(),
				m_NormalTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_ThicknessTexture)
	{
		descManager->WriteImageDescriptor(
			m_SubsurfaceOwnedDescSets[0],
			SUBSURFACE_TEXTURE_BINDING_THICKNESS,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_ThicknessTexture->GetImage().GetSampler(),
				m_ThicknessTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_RoughnessTexture)
	{
		descManager->WriteImageDescriptor(
			m_SubsurfaceOwnedDescSets[0],
			SUBSURFACE_TEXTURE_BINDING_ROUGHNESS,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_RoughnessTexture->GetImage().GetSampler(),
				m_RoughnessTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	descManager->CommitDescriptorUpdates();
}

void VansGraphics::VansGrassMaterial::BuildGrassTextureDescriptors()
{
	VansDescriptorSetLayoutFactory::CreateAndAllocate_GrassTexture(m_GrassOwnedLayout, m_GrassOwnedDescSets);

	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->BeginDescriptorUpdate();

	if (m_AlbedoTexture)
	{
		descManager->WriteImageDescriptor(
			m_GrassOwnedDescSets[0],
			GRASS_TEXTURE_BINDING_ALBEDO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AlbedoTexture->GetImage().GetSampler(),
				m_AlbedoTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_NormalTexture)
	{
		descManager->WriteImageDescriptor(
			m_GrassOwnedDescSets[0],
			GRASS_TEXTURE_BINDING_NORMAL,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_NormalTexture->GetImage().GetSampler(),
				m_NormalTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_RoughnessTexture)
	{
		descManager->WriteImageDescriptor(
			m_GrassOwnedDescSets[0],
			GRASS_TEXTURE_BINDING_ROUGHNESS,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_RoughnessTexture->GetImage().GetSampler(),
				m_RoughnessTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_TranslucencyTexture)
	{
		descManager->WriteImageDescriptor(
			m_GrassOwnedDescSets[0],
			GRASS_TEXTURE_BINDING_TRANSLUCENCY,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_TranslucencyTexture->GetImage().GetSampler(),
				m_TranslucencyTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_AOTexture)
	{
		descManager->WriteImageDescriptor(
			m_GrassOwnedDescSets[0],
			GRASS_TEXTURE_BINDING_AO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AOTexture->GetImage().GetSampler(),
				m_AOTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	descManager->CommitDescriptorUpdates();
}
