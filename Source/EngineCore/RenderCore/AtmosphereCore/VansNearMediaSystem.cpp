#include "VansNearMediaSystem.h"

#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "../VansCamera.h"
#include "../VansRenderFrame.h"
#include "../VansScene.h"
#include "../VansShaderManager.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansVKSampler.h"
#include "../VulkanCore/VansTexture.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../ScriptCore/VansTransform.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace VansGraphics
{
namespace
{
	constexpr VkImageUsageFlags NearMediaUsage =
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	std::uint32_t DivideRoundUp(std::uint32_t value, std::uint32_t divisor)
	{
		return (value + divisor - 1u) / divisor;
	}

	std::vector<VkDescriptorSetLayoutBinding> BuildNearMediaPassBindings(
		bool includeVolumetricParticles)
	{
		std::vector<VkDescriptorSetLayoutBinding> bindings = {
			{ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			{ 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			{ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VANS_PUNCTUAL_SHADOW_ATLAS_COUNT, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			{ 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			{ 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			{ 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			{ 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			{ 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			{ 10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				MaxLocalFogFieldTextureDescriptors, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			{ 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			{ 12, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
		};
		if (includeVolumetricParticles)
		{
			bindings.insert(bindings.end(), {
				{ 13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
				{ 14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
				{ 15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
				{ 16, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
				{ 17, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
				{ 18, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
			});
		}
		bindings.insert(bindings.end(), {
			{ 19, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			{ 20, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			{ 21, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
			{ 22, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
		});
		return bindings;
	}

	std::vector<VkDescriptorBindingFlags> BuildNearMediaPassBindingFlags(
		const std::vector<VkDescriptorSetLayoutBinding>& bindings)
	{
		std::vector<VkDescriptorBindingFlags> flags(bindings.size(), 0u);
		for (std::size_t index = 0; index < bindings.size(); ++index)
		{
			if (bindings[index].binding == 10u)
				flags[index] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
		}
		return flags;
	}

	glm::vec3 ToVec3(const std::array<float, 3>& value)
	{
		return { value[0], value[1], value[2] };
	}

	bool IsSrgbFormat(VkFormat format)
	{
		switch (format)
		{
		case VK_FORMAT_R8_SRGB:
		case VK_FORMAT_R8G8_SRGB:
		case VK_FORMAT_R8G8B8_SRGB:
		case VK_FORMAT_B8G8R8_SRGB:
		case VK_FORMAT_R8G8B8A8_SRGB:
		case VK_FORMAT_B8G8R8A8_SRGB:
		case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
		case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
		case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
		case VK_FORMAT_BC2_SRGB_BLOCK:
		case VK_FORMAT_BC3_SRGB_BLOCK:
		case VK_FORMAT_BC7_SRGB_BLOCK:
			return true;
		default:
			return false;
		}
	}

	bool IsCompressedFieldFormat(VkFormat format)
	{
		switch (format)
		{
		case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
		case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
		case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
		case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
		case VK_FORMAT_BC2_UNORM_BLOCK:
		case VK_FORMAT_BC2_SRGB_BLOCK:
		case VK_FORMAT_BC3_UNORM_BLOCK:
		case VK_FORMAT_BC3_SRGB_BLOCK:
		case VK_FORMAT_BC4_UNORM_BLOCK:
		case VK_FORMAT_BC4_SNORM_BLOCK:
		case VK_FORMAT_BC5_UNORM_BLOCK:
		case VK_FORMAT_BC5_SNORM_BLOCK:
		case VK_FORMAT_BC6H_UFLOAT_BLOCK:
		case VK_FORMAT_BC6H_SFLOAT_BLOCK:
		case VK_FORMAT_BC7_UNORM_BLOCK:
		case VK_FORMAT_BC7_SRGB_BLOCK:
			return true;
		default:
			return false;
		}
	}

	int LocalFogFieldFormatChannelCount(VkFormat format)
	{
		switch (format)
		{
		case VK_FORMAT_R8_UNORM: return 1;
		case VK_FORMAT_R8G8_UNORM: return 2;
		case VK_FORMAT_R8G8B8A8_UNORM: return 4;
		default: return 0;
		}
	}

	float FlowDecodeZeroThreshold(VkFormat format)
	{
		switch (format)
		{
		case VK_FORMAT_R16_UNORM:
		case VK_FORMAT_R16G16_UNORM:
		case VK_FORMAT_R16G16B16A16_UNORM:
			return 1.5f / 65535.0f;
		default:
			return 1.5f / 255.0f;
		}
	}

	void HashBytes(std::uint64_t& hash, const void* data, std::size_t size)
	{
		const auto* bytes = static_cast<const std::uint8_t*>(data);
		for (std::size_t index = 0; index < size; ++index)
		{
			hash ^= bytes[index];
			hash *= 1099511628211ull;
		}
	}

	constexpr std::uint32_t LocalFogShapeEnabled = 1u << 0u;
	constexpr std::uint32_t LocalFogDetailEnabled = 1u << 1u;
	constexpr std::uint32_t LocalFogFlowEnabled = 1u << 2u;
	constexpr std::uint32_t LocalFogFlowHasTexture = 1u << 3u;
	constexpr std::uint32_t LocalFogShapeInvert = 1u << 4u;
	constexpr std::uint32_t LocalFogDetailInvert = 1u << 5u;
}

bool VansLocalFogFieldResourceTable::Initialize(
	VansVKDevice& device, VansScene& scene)
{
	Shutdown();
	m_Device = &device;
	m_Scene = &scene;
	VkDevice& logicalDevice = device.GetLogicDevice();
	if (!VansVKSampler::CreateSampler(logicalDevice, m_RepeatSampler,
		VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT,
		VK_SAMPLER_ADDRESS_MODE_REPEAT, 0.0f, VK_FALSE, 1.0f, VK_FALSE,
		VK_COMPARE_OP_ALWAYS, 0.0f, 16.0f) ||
		!VansVKSampler::CreateSampler(logicalDevice, m_ClampToEdgeSampler,
			VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.0f, VK_FALSE, 1.0f, VK_FALSE,
			VK_COMPARE_OP_ALWAYS, 0.0f, 16.0f) ||
		!VansVKSampler::CreateSampler(logicalDevice, m_ClampToBorderZeroSampler,
			VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 0.0f, VK_FALSE, 1.0f, VK_FALSE,
			VK_COMPARE_OP_ALWAYS, 0.0f, 16.0f,
			VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK))
	{
		Shutdown();
		return false;
	}

	m_NeutralTexture = new VansTexture();
	m_NeutralTexture->m_TextureType = TEXTURE_2D;
	m_NeutralTexture->SetName("__LocalFogNeutralField");
	const std::array<std::uint8_t, 4> neutralPixel{ 255u, 255u, 128u, 128u };
	m_NeutralTexture->LoadFromMemory(
		device.GetCommandBuffer(), neutralPixel.data(), neutralPixel.size(),
		1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
	if (m_NeutralTexture->GetImage().GetImageView() == VK_NULL_HANDLE)
	{
		VANS_LOG_ERROR("[LocalFogField] Cannot create the neutral field texture");
		Shutdown();
		return false;
	}

	m_MetadataBufferCreated = m_MetadataBuffer.CreatVulkanBuffer(
		logicalDevice,
		sizeof(VansLocalFogFieldSampleMetadataGPU) * MaxLocalFogFieldSampleHandles,
		VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (!m_MetadataBufferCreated)
	{
		Shutdown();
		return false;
	}
	BeginBuild();
	EndBuild();
	return true;
}

void VansLocalFogFieldResourceTable::BeginBuild()
{
	m_BuildDescriptors.clear();
	m_BuildSamples.clear();
	m_BuildMetadata.clear();
	m_BuildDescriptors.push_back({ "__neutral", Vans::VansLocalFogTextureAddressMode::ClampToEdge,
		m_NeutralTexture });
	VansLocalFogFieldSampleMetadataGPU scalarNeutral{};
	scalarNeutral.descriptorChannelsAndKind = glm::uvec4(0u, 0u, 0u, 0u);
	scalarNeutral.resolutionMipAndZeroThreshold = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
	scalarNeutral.decodeScaleBiasPadding = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
	m_BuildMetadata.push_back(scalarNeutral);
	VansLocalFogFieldSampleMetadataGPU vectorNeutral{};
	vectorNeutral.descriptorChannelsAndKind = glm::uvec4(0u, 2u, 3u, 1u);
	vectorNeutral.resolutionMipAndZeroThreshold = glm::vec4(
		1.0f, 1.0f, 1.0f, 1.5f / 255.0f);
	vectorNeutral.decodeScaleBiasPadding = glm::vec4(2.0f, -1.0f, 0.0f, 0.0f);
	m_BuildMetadata.push_back(vectorNeutral);
}

std::uint32_t VansLocalFogFieldResourceTable::RegisterScalar(
	const Vans::VansLocalFogScalarTextureSourceConfig& source,
	const Vans::VansLocalFogTextureMapping2DConfig& mapping)
{
	return RegisterSample(source.assetGuid, mapping.addressMode,
		source.channel, source.channel, false);
}

std::uint32_t VansLocalFogFieldResourceTable::RegisterVector2(
	const Vans::VansLocalFogVector2TextureSourceConfig& source,
	const Vans::VansLocalFogTextureMapping2DConfig& mapping)
{
	return RegisterSample(source.assetGuid, mapping.addressMode,
		source.xChannel, source.zChannel, true);
}

std::uint32_t VansLocalFogFieldResourceTable::RegisterSample(
	const std::string& assetGuid,
	Vans::VansLocalFogTextureAddressMode addressMode,
	Vans::VansLocalFogTextureChannel channel0,
	Vans::VansLocalFogTextureChannel channel1,
	bool vector2)
{
	if (assetGuid.empty())
		return vector2 ? 1u : 0u;
	for (const SampleEntry& entry : m_BuildSamples)
	{
		if (entry.assetGuid == assetGuid && entry.addressMode == addressMode &&
			entry.channel0 == channel0 && entry.channel1 == channel1 &&
			entry.vector2 == vector2)
			return entry.handle;
	}
	if (m_BuildMetadata.size() >= MaxLocalFogFieldSampleHandles)
	{
		ReportInvalidSourceOnce("sample-capacity", "Local Fog field sample handle capacity exceeded");
		return vector2 ? 1u : 0u;
	}

	VansTexture* texture = ResolveTexture(assetGuid);
	if (!texture)
		return vector2 ? 1u : 0u;
	const int availableChannels = LocalFogFieldFormatChannelCount(
		texture->GetImage().GetImageCreateInfo().format);
	const int requiredChannels = vector2
		? (std::max)(Vans::RequiredLocalFogFieldChannelCount(channel0),
			Vans::RequiredLocalFogFieldChannelCount(channel1))
		: Vans::RequiredLocalFogFieldChannelCount(channel0);
	if (availableChannels < requiredChannels)
	{
		ReportInvalidSourceOnce(assetGuid + ":channels",
			"Local Fog field texture does not contain every selected channel: " + assetGuid);
		return vector2 ? 1u : 0u;
	}
	std::uint32_t descriptorIndex = 0u;
	for (std::uint32_t index = 1u; index < m_BuildDescriptors.size(); ++index)
	{
		const DescriptorEntry& entry = m_BuildDescriptors[index];
		if (entry.assetGuid == assetGuid && entry.addressMode == addressMode)
		{
			descriptorIndex = index;
			break;
		}
	}
	if (descriptorIndex == 0u)
	{
		if (m_BuildDescriptors.size() >= MaxLocalFogFieldTextureDescriptors)
		{
			ReportInvalidSourceOnce("descriptor-capacity",
				"Local Fog field texture descriptor capacity exceeded");
			return vector2 ? 1u : 0u;
		}
		descriptorIndex = static_cast<std::uint32_t>(m_BuildDescriptors.size());
		m_BuildDescriptors.push_back({ assetGuid, addressMode, texture });
	}

	const VkImageCreateInfo& imageInfo = texture->GetImage().GetImageCreateInfo();
	VansLocalFogFieldSampleMetadataGPU metadata{};
	metadata.descriptorChannelsAndKind = glm::uvec4(
		descriptorIndex,
		static_cast<std::uint32_t>(Vans::LocalFogTextureChannelIndex(channel0)),
		static_cast<std::uint32_t>(Vans::LocalFogTextureChannelIndex(channel1)),
		vector2 ? 1u : 0u);
	metadata.resolutionMipAndZeroThreshold = glm::vec4(
		static_cast<float>((std::max)(texture->GetWidth(), 1)),
		static_cast<float>((std::max)(texture->GetHeight(), 1)),
		static_cast<float>((std::max)(imageInfo.mipLevels, 1u)),
		vector2 ? FlowDecodeZeroThreshold(imageInfo.format) : 0.0f);
	metadata.decodeScaleBiasPadding = vector2
		? glm::vec4(2.0f, -1.0f, 0.0f, 0.0f)
		: glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
	const std::uint32_t handle = static_cast<std::uint32_t>(m_BuildMetadata.size());
	m_BuildMetadata.push_back(metadata);
	m_BuildSamples.push_back({ assetGuid, addressMode, channel0, channel1, vector2, handle });
	return handle;
}

VansTexture* VansLocalFogFieldResourceTable::ResolveTexture(const std::string& assetGuid)
{
	if (!m_Scene)
		return nullptr;
	auto* texture = static_cast<VansTexture*>(m_Scene->GetTextureAsset(assetGuid));
	if (!texture)
	{
		ReportInvalidSourceOnce(assetGuid + ":missing",
			"Local Fog field texture cannot be resolved: " + assetGuid);
		return nullptr;
	}
	if (texture->m_TextureType != TEXTURE_2D ||
		texture->GetImage().GetImageView() == VK_NULL_HANDLE)
	{
		ReportInvalidSourceOnce(assetGuid + ":not-2d",
			"Local Fog field source is not a ready Texture2D: " + assetGuid);
		return nullptr;
	}
	const VkImageCreateInfo& imageInfo = texture->GetImage().GetImageCreateInfo();
	if (IsSrgbFormat(imageInfo.format))
	{
		ReportInvalidSourceOnce(assetGuid + ":srgb",
			"Local Fog field texture must use linear color space: " + assetGuid);
		return nullptr;
	}
	if (IsCompressedFieldFormat(imageInfo.format))
	{
		ReportInvalidSourceOnce(assetGuid + ":compressed",
			"Local Fog field texture compression is disabled for the current implementation: " + assetGuid);
		return nullptr;
	}
	if (LocalFogFieldFormatChannelCount(imageInfo.format) == 0)
	{
		ReportInvalidSourceOnce(assetGuid + ":format",
			"Local Fog field texture must use R8, RG8, or RGBA8 UNORM: " + assetGuid);
		return nullptr;
	}
	if (imageInfo.mipLevels <= 1u && (texture->GetWidth() > 1 || texture->GetHeight() > 1))
	{
		ReportInvalidSourceOnce(assetGuid + ":mips",
			"Local Fog field texture requires mipmaps: " + assetGuid);
		return nullptr;
	}
	return texture;
}

VkSampler VansLocalFogFieldResourceTable::SamplerFor(
	Vans::VansLocalFogTextureAddressMode addressMode) const
{
	switch (addressMode)
	{
	case Vans::VansLocalFogTextureAddressMode::ClampToBorderZero:
		return m_ClampToBorderZeroSampler;
	case Vans::VansLocalFogTextureAddressMode::ClampToEdge:
		return m_ClampToEdgeSampler;
	case Vans::VansLocalFogTextureAddressMode::Repeat:
		return m_RepeatSampler;
	}
	return m_RepeatSampler;
}

void VansLocalFogFieldResourceTable::ReportInvalidSourceOnce(
	const std::string& key, const std::string& message)
{
	if (m_ReportedInvalidSources.insert(key).second)
		VANS_LOG_ERROR("[LocalFogField] " << message);
}

bool VansLocalFogFieldResourceTable::EndBuild()
{
	if (!m_NeutralTexture || !m_MetadataBufferCreated)
		return false;
	const VkDescriptorImageInfo neutralInfo{
		m_ClampToEdgeSampler,
		m_NeutralTexture->GetImage().GetImageView(),
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};
	m_DescriptorInfos.assign(MaxLocalFogFieldTextureDescriptors, neutralInfo);
	for (std::uint32_t index = 1u; index < m_BuildDescriptors.size(); ++index)
	{
		const DescriptorEntry& entry = m_BuildDescriptors[index];
		m_DescriptorInfos[index] = {
			SamplerFor(entry.addressMode), entry.texture->GetImage().GetImageView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		};
	}
	std::vector<VansLocalFogFieldSampleMetadataGPU> upload(
		MaxLocalFogFieldSampleHandles, m_BuildMetadata.front());
	std::copy(m_BuildMetadata.begin(), m_BuildMetadata.end(), upload.begin());

	std::uint64_t signature = 1469598103934665603ull;
	for (const VkDescriptorImageInfo& info : m_DescriptorInfos)
	{
		HashBytes(signature, &info.sampler, sizeof(info.sampler));
		HashBytes(signature, &info.imageView, sizeof(info.imageView));
		HashBytes(signature, &info.imageLayout, sizeof(info.imageLayout));
	}
	HashBytes(signature, upload.data(), upload.size() * sizeof(upload.front()));
	const bool changed = !m_HasSignature || signature != m_Signature;
	if (changed)
	{
		m_MetadataBuffer.SetBufferData(upload.data(), 0,
			upload.size() * sizeof(upload.front()));
		m_Signature = signature;
		m_HasSignature = true;
		VANS_LOG("[LocalFogField] active descriptors="
			<< (m_BuildDescriptors.size() - 1u) << "/"
			<< (MaxLocalFogFieldTextureDescriptors - 1u)
			<< ", sample handles=" << (m_BuildMetadata.size() - 2u) << "/"
			<< (MaxLocalFogFieldSampleHandles - 2u));
	}
	return changed;
}

VkDescriptorBufferInfo VansLocalFogFieldResourceTable::GetMetadataDescriptor() const
{
	return {
		m_MetadataBuffer.GetNativeBuffer(), 0,
		sizeof(VansLocalFogFieldSampleMetadataGPU) * MaxLocalFogFieldSampleHandles
	};
}

void VansLocalFogFieldResourceTable::Shutdown()
{
	if (m_Device)
	{
		VkDevice& logicalDevice = m_Device->GetLogicDevice();
		if (m_MetadataBufferCreated)
			m_MetadataBuffer.DestroyVulkanBuffer(logicalDevice);
		delete m_NeutralTexture;
		VansVKSampler::DestroySampler(logicalDevice, m_RepeatSampler);
		VansVKSampler::DestroySampler(logicalDevice, m_ClampToEdgeSampler);
		VansVKSampler::DestroySampler(logicalDevice, m_ClampToBorderZeroSampler);
	}
	m_Device = nullptr;
	m_Scene = nullptr;
	m_NeutralTexture = nullptr;
	m_MetadataBufferCreated = false;
	m_BuildDescriptors.clear();
	m_BuildSamples.clear();
	m_BuildMetadata.clear();
	m_DescriptorInfos.clear();
	m_ReportedInvalidSources.clear();
	m_Signature = 0u;
	m_HasSignature = false;
}

bool VansNearMediaSystem::Initialize(VansVKDevice& device, VansScene& scene,
	const VansNearMediaQualityConfig& quality,
	std::uint32_t renderWidth, std::uint32_t renderHeight)
{
	Shutdown();
	m_Device = &device;
	m_Scene = &scene;
	m_Quality = quality;
	m_RenderWidth = renderWidth;
	m_RenderHeight = renderHeight;
	m_InjectionShader = VansShaderManager::Get().FindComputeShader("LocalMediaInjection");
	m_LightingShader = VansShaderManager::Get().FindComputeShader("NearMediaLighting");
	m_TemporalResolveShader =
		VansShaderManager::Get().FindComputeShader("LocalMediaTemporalResolve");
	m_IntegrationShader = VansShaderManager::Get().FindComputeShader("LocalMediaIntegration");
	m_NearMediaUnifiedInjectionShader =
		VansShaderManager::Get().FindComputeShader("NearMediaUnifiedInjection");
	m_VolumetricParticleTemporalResolveShader =
		VansShaderManager::Get().FindComputeShader("VolumetricParticleTemporalResolve");
	if (!m_InjectionShader || !m_LightingShader || !m_TemporalResolveShader ||
		!m_IntegrationShader ||
		!ValidateLocalFogFieldDescriptorSupport() ||
		!CreateResources() || !CreateDescriptors())
	{
		VANS_LOG_ERROR("[NearMedia] Initialization failed");
		Shutdown();
		return false;
	}
	m_Initialized = true;
	return true;
}

bool VansNearMediaSystem::CreateResources()
{
	if (!m_Device || m_Quality.tileSize == 0 || m_Quality.slices == 0)
		return false;
	m_GridWidth = DivideRoundUp(m_RenderWidth, m_Quality.tileSize);
	m_GridHeight = DivideRoundUp(m_RenderHeight, m_Quality.tileSize);
	const std::size_t tileCount = static_cast<std::size_t>(m_GridWidth) * m_GridHeight;
	m_LocalFogVolumeScratch.reserve(MaxLocalFogVolumes);
	m_LocalFogTileHeaderScratch.reserve(tileCount);
	m_LocalFogTileIndexScratch.reserve(
		tileCount * MaxLocalFogCandidatesPerTile);
	VkDevice device = m_Device->GetLogicDevice();
	m_ParamsBufferCreated = m_ParamsBuffer.CreatVulkanBuffer(
		device, sizeof(VansNearMediaParamsGPU), VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_LocalFogVolumesBufferCreated = m_LocalFogVolumesBuffer.CreatVulkanBuffer(
		device, sizeof(VansLocalFogVolumeGPU) * MaxLocalFogVolumes,
		VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_LocalFogTileHeadersBufferCreated = m_LocalFogTileHeadersBuffer.CreatVulkanBuffer(
		device, sizeof(glm::uvec2) * tileCount, VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_LocalFogTileIndicesBufferCreated = m_LocalFogTileIndicesBuffer.CreatVulkanBuffer(
		device, sizeof(std::uint32_t) * tileCount * MaxLocalFogCandidatesPerTile,
		VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (!m_ParamsBufferCreated || !m_LocalFogVolumesBufferCreated ||
		!m_LocalFogTileHeadersBufferCreated || !m_LocalFogTileIndicesBufferCreated)
		return false;
	if (!m_LocalFogFieldResources.Initialize(*m_Device, *m_Scene))
		return false;
	const VkExtent3D extent{ m_GridWidth, m_GridHeight, m_Quality.slices };
	if (!m_RawInjection.CreateVulkanImage(device, extent,
		VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, VK_IMAGE_TYPE_3D,
		NearMediaUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		return false;
	if (!m_MaterialScatteringExtinction.CreateVulkanImage(device, extent,
		VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, VK_IMAGE_TYPE_3D,
		NearMediaUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE) ||
		!m_MaterialLightingPhaseCloud.CreateVulkanImage(device, extent,
			VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, VK_IMAGE_TYPE_3D,
			NearMediaUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE) ||
		!m_MaterialEmissiveWeight.CreateVulkanImage(device, extent,
			VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, VK_IMAGE_TYPE_3D,
			NearMediaUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
	{
		return false;
	}
	for (auto& injection : m_Injection)
	{
		if (!injection.CreateVulkanImage(device, extent,
			VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, VK_IMAGE_TYPE_3D,
			NearMediaUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
			return false;
	}
	if (!m_Scattering.CreateVulkanImage(device, extent,
		VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, VK_IMAGE_TYPE_3D,
		NearMediaUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		return false;
	return m_OpticalDepth.CreateVulkanImage(device, extent,
		VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, VK_IMAGE_TYPE_3D,
		NearMediaUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
}

bool VansNearMediaSystem::CreateVolumetricParticleResources()
{
	if (m_VolumetricParticleResourcesCreated)
		return true;
	if (!m_Device || !m_NearMediaUnifiedInjectionShader ||
		!m_VolumetricParticleTemporalResolveShader)
		return false;

	const std::size_t tileCount =
		static_cast<std::size_t>(m_GridWidth) * m_GridHeight;
	VkDevice device = m_Device->GetLogicDevice();
	m_VolumetricParticlesBufferCreated =
		m_VolumetricParticlesBuffer.CreatVulkanBuffer(
			device,
			sizeof(VansVolumetricParticleInstanceData) * MaxVolumetricParticles,
			VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_VolumetricParticleTileHeadersBufferCreated =
		m_VolumetricParticleTileHeadersBuffer.CreatVulkanBuffer(
			device, sizeof(glm::uvec2) * tileCount, VK_FORMAT_R32_UINT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_VolumetricParticleTileIndicesBufferCreated =
		m_VolumetricParticleTileIndicesBuffer.CreatVulkanBuffer(
			device,
			sizeof(std::uint32_t) * tileCount *
				MaxVolumetricParticleCandidatesPerTile,
			VK_FORMAT_R32_UINT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_VolumetricParticleParamsBufferCreated =
		m_VolumetricParticleParamsBuffer.CreatVulkanBuffer(
			device, sizeof(VansVolumetricParticleParamsGPU), VK_FORMAT_R32_UINT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	const VkExtent3D extent{ m_GridWidth, m_GridHeight, m_Quality.slices };
	const bool imagesCreated =
		m_VolumetricParticleActivity[0].CreateVulkanImage(
			device, extent, VK_FORMAT_R16_SFLOAT, 1, 1,
			VK_IMAGE_TYPE_3D, NearMediaUsage, VK_SAMPLE_COUNT_1_BIT,
			false, false, true, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE) &&
		m_VolumetricParticleActivity[1].CreateVulkanImage(
			device, extent, VK_FORMAT_R16_SFLOAT, 1, 1,
			VK_IMAGE_TYPE_3D, NearMediaUsage, VK_SAMPLE_COUNT_1_BIT,
			false, false, true, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
	if (!m_VolumetricParticlesBufferCreated ||
		!m_VolumetricParticleTileHeadersBufferCreated ||
		!m_VolumetricParticleTileIndicesBufferCreated ||
		!m_VolumetricParticleParamsBufferCreated || !imagesCreated ||
		!CreateVolumetricParticleDescriptors())
	{
		DestroyVolumetricParticleDescriptors();
		DestroyVolumetricParticleResources();
		return false;
	}
	m_VolumetricParticleScratch.reserve(MaxVolumetricParticles);
	m_VolumetricParticleTileHeaderScratch.reserve(tileCount);
	m_VolumetricParticleTileIndexScratch.reserve(
		tileCount * MaxVolumetricParticleCandidatesPerTile);
	m_VolumetricParticleResourcesCreated = true;
	VANS_LOG("[NearMedia] Optional volumetric-particle resources created: maxParticles="
		<< MaxVolumetricParticles << ", candidatesPerTile="
		<< MaxVolumetricParticleCandidatesPerTile << ", grid="
		<< m_GridWidth << "x" << m_GridHeight << "x" << m_Quality.slices);
	return true;
}

bool VansNearMediaSystem::ValidateLocalFogFieldDescriptorSupport() const
{
	if (!m_Device)
		return false;
	VkPhysicalDeviceVulkan12Features features12{};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	VkPhysicalDeviceFeatures2 features{};
	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features.pNext = &features12;
	VansGraphics::vkGetPhysicalDeviceFeatures2(m_Device->GetPhysicalDevice(), &features);
	if (!features12.shaderSampledImageArrayNonUniformIndexing ||
		!features12.descriptorBindingSampledImageUpdateAfterBind)
	{
		VANS_LOG_ERROR("[NearMedia] Local Fog fields require non-uniform sampled-image indexing and update-after-bind descriptors");
		return false;
	}
	const VkPhysicalDeviceLimits& limits = m_Device->GetDeviceProperties().limits;
	const std::uint32_t sampledImages =
		MaxLocalFogFieldTextureDescriptors +
		VANS_PUNCTUAL_SHADOW_ATLAS_COUNT + 4u;
	VkPhysicalDeviceDescriptorIndexingProperties descriptorIndexing{};
	descriptorIndexing.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
	VkPhysicalDeviceProperties2 properties{};
	properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	properties.pNext = &descriptorIndexing;
	VansGraphics::vkGetPhysicalDeviceProperties2(
		m_Device->GetPhysicalDevice(), &properties);
	if (limits.maxPerStageDescriptorSamplers < sampledImages ||
		limits.maxPerStageDescriptorSampledImages < sampledImages ||
		limits.maxDescriptorSetSamplers < sampledImages ||
		limits.maxDescriptorSetSampledImages < sampledImages ||
		descriptorIndexing.maxPerStageDescriptorUpdateAfterBindSamplers < sampledImages ||
		descriptorIndexing.maxPerStageDescriptorUpdateAfterBindSampledImages < sampledImages ||
		descriptorIndexing.maxDescriptorSetUpdateAfterBindSamplers < sampledImages ||
		descriptorIndexing.maxDescriptorSetUpdateAfterBindSampledImages < sampledImages)
	{
		VANS_LOG_ERROR("[NearMedia] Device descriptor limits cannot hold "
			<< sampledImages << " LocalMedia sampled images, including update-after-bind limits");
		return false;
	}
	return true;
}

bool VansNearMediaSystem::CreateDescriptors()
{
	auto* descriptors = VansVKDescriptorManager::GetInstance();
	const std::vector<VkDescriptorSetLayoutBinding> bindings =
		BuildNearMediaPassBindings(false);
	const std::vector<VkDescriptorBindingFlags> bindingFlags =
		BuildNearMediaPassBindingFlags(bindings);
	if (!descriptors->CreateDesciptorSetLayoutWithFlags(
		bindings, bindingFlags,
		VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		m_PassLayout))
		return false;
	std::vector<VkDescriptorSet> sets;
	if (!descriptors->AllocateDescriptorSet(
		{ m_PassLayout, m_PassLayout }, sets,
		VansDescriptorLifetimeRole::ScenePersistent) || sets.size() != 2)
		return false;
	m_PassSets[0] = sets[0];
	m_PassSets[1] = sets[1];
	auto* renderPasses = VansRenderPassManager::GetInstance();
	for (std::uint32_t target = 0; target < 2; ++target)
	{
		descriptors->BeginDescriptorUpdate();
		descriptors->WriteImageDescriptor(m_PassSets[target], 0,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_RawInjection.GetSampler(), m_RawInjection.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(m_PassSets[target], 1,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ m_Injection[target ^ 1u].GetSampler(), m_Injection[target ^ 1u].GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteBufferDescriptor(m_PassSets[target], 2,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{{ m_ParamsBuffer.GetNativeBuffer(), 0, sizeof(VansNearMediaParamsGPU) }});
		descriptors->WriteImageDescriptor(m_PassSets[target], 3,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ renderPasses->GetCascadeShadowSampler(), renderPasses->GetCascadeShadowArrayView(),
			   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }});
		descriptors->WriteImageDescriptor(m_PassSets[target], 4,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			renderPasses->GetPunctualShadowDescriptorInfos());
		descriptors->WriteImageDescriptor(m_PassSets[target], 5,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_Scattering.GetSampler(), m_Scattering.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(m_PassSets[target], 6,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_OpticalDepth.GetSampler(), m_OpticalDepth.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteBufferDescriptor(m_PassSets[target], 7,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ m_LocalFogVolumesBuffer.GetNativeBuffer(), 0,
			   sizeof(VansLocalFogVolumeGPU) * MaxLocalFogVolumes }});
		const VkDeviceSize tileCount = static_cast<VkDeviceSize>(m_GridWidth) * m_GridHeight;
		descriptors->WriteBufferDescriptor(m_PassSets[target], 8,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ m_LocalFogTileHeadersBuffer.GetNativeBuffer(), 0,
			   sizeof(glm::uvec2) * tileCount }});
		descriptors->WriteBufferDescriptor(m_PassSets[target], 9,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ m_LocalFogTileIndicesBuffer.GetNativeBuffer(), 0,
			   sizeof(std::uint32_t) * tileCount * MaxLocalFogCandidatesPerTile }});
		descriptors->WriteImageDescriptor(m_PassSets[target], 10,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			m_LocalFogFieldResources.GetDescriptorInfos());
		descriptors->WriteBufferDescriptor(m_PassSets[target], 11,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{ m_LocalFogFieldResources.GetMetadataDescriptor() });
		descriptors->WriteImageDescriptor(m_PassSets[target], 12,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_Injection[target].GetSampler(), m_Injection[target].GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(m_PassSets[target], 19,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_MaterialScatteringExtinction.GetSampler(),
			   m_MaterialScatteringExtinction.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(m_PassSets[target], 20,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_MaterialLightingPhaseCloud.GetSampler(),
			   m_MaterialLightingPhaseCloud.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(m_PassSets[target], 21,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_MaterialEmissiveWeight.GetSampler(),
			   m_MaterialEmissiveWeight.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(m_PassSets[target], 22,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ m_MaterialScatteringExtinction.GetSampler(),
			   m_MaterialScatteringExtinction.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->CommitDescriptorUpdates();
	}
	m_LocalFogFieldDescriptorsDirty = false;
	return true;
}

bool VansNearMediaSystem::CreateVolumetricParticleDescriptors()
{
	auto* descriptors = VansVKDescriptorManager::GetInstance();
	const std::vector<VkDescriptorSetLayoutBinding> bindings =
		BuildNearMediaPassBindings(true);
	const std::vector<VkDescriptorBindingFlags> bindingFlags =
		BuildNearMediaPassBindingFlags(bindings);
	if (!descriptors->CreateDesciptorSetLayoutWithFlags(
		bindings, bindingFlags,
		VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		m_VolumetricParticlePassLayout))
		return false;
	std::vector<VkDescriptorSet> sets;
	if (!descriptors->AllocateDescriptorSet(
		{ m_VolumetricParticlePassLayout, m_VolumetricParticlePassLayout },
		sets, VansDescriptorLifetimeRole::ScenePersistent) || sets.size() != 2)
		return false;
	m_VolumetricParticlePassSets[0] = sets[0];
	m_VolumetricParticlePassSets[1] = sets[1];

	auto* renderPasses = VansRenderPassManager::GetInstance();
	const VkDeviceSize tileCount =
		static_cast<VkDeviceSize>(m_GridWidth) * m_GridHeight;
	for (std::uint32_t target = 0; target < 2; ++target)
	{
		descriptors->BeginDescriptorUpdate();
		descriptors->WriteImageDescriptor(m_VolumetricParticlePassSets[target], 0,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_RawInjection.GetSampler(), m_RawInjection.GetImageView(),
			   VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(m_VolumetricParticlePassSets[target], 1,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ m_Injection[target ^ 1u].GetSampler(),
			   m_Injection[target ^ 1u].GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteBufferDescriptor(m_VolumetricParticlePassSets[target], 2,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{{ m_ParamsBuffer.GetNativeBuffer(), 0, sizeof(VansNearMediaParamsGPU) }});
		descriptors->WriteImageDescriptor(m_VolumetricParticlePassSets[target], 3,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ renderPasses->GetCascadeShadowSampler(),
			   renderPasses->GetCascadeShadowArrayView(),
			   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }});
		descriptors->WriteImageDescriptor(m_VolumetricParticlePassSets[target], 4,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			renderPasses->GetPunctualShadowDescriptorInfos());
		descriptors->WriteBufferDescriptor(m_VolumetricParticlePassSets[target], 7,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ m_LocalFogVolumesBuffer.GetNativeBuffer(), 0,
			   sizeof(VansLocalFogVolumeGPU) * MaxLocalFogVolumes }});
		descriptors->WriteBufferDescriptor(m_VolumetricParticlePassSets[target], 8,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ m_LocalFogTileHeadersBuffer.GetNativeBuffer(), 0,
			   sizeof(glm::uvec2) * tileCount }});
		descriptors->WriteBufferDescriptor(m_VolumetricParticlePassSets[target], 9,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ m_LocalFogTileIndicesBuffer.GetNativeBuffer(), 0,
			   sizeof(std::uint32_t) * tileCount * MaxLocalFogCandidatesPerTile }});
		descriptors->WriteImageDescriptor(m_VolumetricParticlePassSets[target], 10,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			m_LocalFogFieldResources.GetDescriptorInfos());
		descriptors->WriteBufferDescriptor(m_VolumetricParticlePassSets[target], 11,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{ m_LocalFogFieldResources.GetMetadataDescriptor() });
		descriptors->WriteImageDescriptor(m_VolumetricParticlePassSets[target], 12,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_Injection[target].GetSampler(), m_Injection[target].GetImageView(),
			   VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteBufferDescriptor(m_VolumetricParticlePassSets[target], 13,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ m_VolumetricParticlesBuffer.GetNativeBuffer(), 0,
			   sizeof(VansVolumetricParticleInstanceData) * MaxVolumetricParticles }});
		descriptors->WriteBufferDescriptor(m_VolumetricParticlePassSets[target], 14,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ m_VolumetricParticleTileHeadersBuffer.GetNativeBuffer(), 0,
			   sizeof(glm::uvec2) * tileCount }});
		descriptors->WriteBufferDescriptor(m_VolumetricParticlePassSets[target], 15,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ m_VolumetricParticleTileIndicesBuffer.GetNativeBuffer(), 0,
			   sizeof(std::uint32_t) * tileCount *
				MaxVolumetricParticleCandidatesPerTile }});
		descriptors->WriteBufferDescriptor(m_VolumetricParticlePassSets[target], 16,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{{ m_VolumetricParticleParamsBuffer.GetNativeBuffer(), 0,
			   sizeof(VansVolumetricParticleParamsGPU) }});
		descriptors->WriteImageDescriptor(m_VolumetricParticlePassSets[target], 17,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_VolumetricParticleActivity[target].GetSampler(),
			   m_VolumetricParticleActivity[target].GetImageView(),
			   VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(m_VolumetricParticlePassSets[target], 18,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ m_VolumetricParticleActivity[target ^ 1u].GetSampler(),
			   m_VolumetricParticleActivity[target ^ 1u].GetImageView(),
			   VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(m_VolumetricParticlePassSets[target], 19,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_MaterialScatteringExtinction.GetSampler(),
			   m_MaterialScatteringExtinction.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(m_VolumetricParticlePassSets[target], 20,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_MaterialLightingPhaseCloud.GetSampler(),
			   m_MaterialLightingPhaseCloud.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(m_VolumetricParticlePassSets[target], 21,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ m_MaterialEmissiveWeight.GetSampler(),
			   m_MaterialEmissiveWeight.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(m_VolumetricParticlePassSets[target], 22,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ m_MaterialScatteringExtinction.GetSampler(),
			   m_MaterialScatteringExtinction.GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->CommitDescriptorUpdates();
	}
	return true;
}

void VansNearMediaSystem::UpdateLocalFogFieldDescriptors()
{
	if (!m_LocalFogFieldDescriptorsDirty)
		return;
	auto* descriptors = VansVKDescriptorManager::GetInstance();
	for (VkDescriptorSet passSet : m_PassSets)
	{
		if (passSet == VK_NULL_HANDLE)
			continue;
		descriptors->BeginDescriptorUpdate();
		descriptors->WriteImageDescriptor(passSet, 10,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			m_LocalFogFieldResources.GetDescriptorInfos());
		descriptors->CommitDescriptorUpdates();
	}
	// 可选集合属于 NearMedia 的统一消费者；更新 Local Fog 字段不触碰
	// 粒子候选表，粒子功能在零 Local Fog 组件时仍可独立工作。
	for (VkDescriptorSet passSet : m_VolumetricParticlePassSets)
	{
		if (passSet == VK_NULL_HANDLE)
			continue;
		descriptors->BeginDescriptorUpdate();
		descriptors->WriteImageDescriptor(passSet, 10,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			m_LocalFogFieldResources.GetDescriptorInfos());
		descriptors->CommitDescriptorUpdates();
	}
	m_LocalFogFieldDescriptorsDirty = false;
	m_HistoryValid = false;
}

void VansNearMediaSystem::RefreshLocalFogRegistry()
{
	if (!m_Scene)
		return;
	const std::uint64_t generation =
		m_Scene->GetSceneObjectCollectionGeneration();
	if (m_LocalFogRegistryGeneration == generation)
		return;

	m_LocalFogRegistry.clear();
	for (VansScriptObject* object : m_Scene->GetSceneObjects())
	{
		if (!object)
			continue;
		auto* component =
			object->GetComponent<VansScriptLocalVolumetricFogComponent>();
		if (component)
			m_LocalFogRegistry.push_back({ object, component });
	}
	m_LocalFogRegistryGeneration = generation;
}

void VansNearMediaSystem::CollectLocalFogVolumes(
	std::vector<VansLocalFogVolumeGPU>& outVolumes,
	std::vector<glm::uvec2>& outTileHeaders,
	std::vector<std::uint32_t>& outTileIndices)
{
	m_LocalFogFieldResources.BeginBuild();
	outVolumes.clear();
	const std::size_t tileCount =
		static_cast<std::size_t>(m_GridWidth) * m_GridHeight;
	outTileHeaders.resize(tileCount);
	outTileIndices.assign(tileCount * MaxLocalFogCandidatesPerTile, 0u);
	for (std::size_t tileIndex = 0; tileIndex < tileCount; ++tileIndex)
	{
		outTileHeaders[tileIndex] = glm::uvec2(
			static_cast<std::uint32_t>(tileIndex) *
				MaxLocalFogCandidatesPerTile,
			0u);
	}
	if (!m_Scene)
	{
		m_LocalFogFieldDescriptorsDirty |= m_LocalFogFieldResources.EndBuild();
		return;
	}

	RefreshLocalFogRegistry();
	VansCamera* camera = m_Scene->GetCamera();
	const glm::vec3 cameraWorld = camera
		? glm::vec3(camera->GetPosition()) : glm::vec3(0.0f);
	const glm::mat4 viewProjection = camera
		? camera->GetProjectiveMatrix() * camera->GetViewMatrix()
		: glm::mat4(1.0f);

	for (const LocalFogRegistryEntry& entry : m_LocalFogRegistry)
	{
		VansScriptObject* object = entry.object;
		auto* component = entry.component;
		if (!object || !component || !object->IsActive() ||
			outVolumes.size() >= MaxLocalFogVolumes ||
			!component->IsEffectivelyEnabled() ||
			!VansTransformStore::IsAllocated(object->m_TransformID))
			continue;
		auto settings = component->m_Settings;
		Vans::NormalizeLocalVolumetricFogConfig(settings);
		const VansTransform& transform =
			VansTransformStore::GetTransform(object->m_TransformID);
		glm::mat4 localToWorld =
			const_cast<VansTransform&>(transform).GetModelMatrix();
		const glm::vec3 dimensions = glm::max(glm::vec3(
			glm::length(glm::vec3(localToWorld[0])),
			glm::length(glm::vec3(localToWorld[1])),
			glm::length(glm::vec3(localToWorld[2]))), glm::vec3(0.001f));
		const glm::mat4 worldToLocal = glm::inverse(localToWorld);

		std::uint32_t minimumTileX = 0u;
		std::uint32_t minimumTileY = 0u;
		std::uint32_t maximumTileX = m_GridWidth - 1u;
		std::uint32_t maximumTileY = m_GridHeight - 1u;
		if (camera)
		{
			const glm::vec3 cameraLocal = glm::vec3(
				worldToLocal * glm::vec4(cameraWorld, 1.0f));
			const bool cameraInside = glm::all(glm::lessThanEqual(
				glm::abs(cameraLocal), glm::vec3(0.5f)));
			glm::vec2 minimumUv(std::numeric_limits<float>::max());
			glm::vec2 maximumUv(-std::numeric_limits<float>::max());
			bool anyInFront = false;
			bool anyBehindNearPlane = false;
			for (int z = 0; z < 2; ++z)
			for (int y = 0; y < 2; ++y)
			for (int x = 0; x < 2; ++x)
			{
				const glm::vec3 localCorner(
					x == 0 ? -0.5f : 0.5f,
					y == 0 ? -0.5f : 0.5f,
					z == 0 ? -0.5f : 0.5f);
				const glm::vec4 worldCorner =
					localToWorld * glm::vec4(localCorner, 1.0f);
				const glm::vec4 clip = viewProjection * worldCorner;
				if (clip.w <= 1.0e-4f)
				{
					anyBehindNearPlane = true;
					continue;
				}
				anyInFront = true;
				const glm::vec2 ndc = glm::vec2(clip) / clip.w;
				const glm::vec2 uv(
					ndc.x * 0.5f + 0.5f,
					1.0f - (ndc.y * 0.5f + 0.5f));
				minimumUv = glm::min(minimumUv, uv);
				maximumUv = glm::max(maximumUv, uv);
			}
			if (!anyInFront)
				continue;
			if (!cameraInside && !anyBehindNearPlane)
			{
				if (maximumUv.x < 0.0f || minimumUv.x > 1.0f ||
					maximumUv.y < 0.0f || minimumUv.y > 1.0f)
					continue;
				minimumUv = glm::clamp(minimumUv, glm::vec2(0.0f), glm::vec2(1.0f));
				maximumUv = glm::clamp(maximumUv, glm::vec2(0.0f), glm::vec2(1.0f));
				minimumTileX = (std::min)(static_cast<std::uint32_t>(
					minimumUv.x * static_cast<float>(m_GridWidth)), m_GridWidth - 1u);
				minimumTileY = (std::min)(static_cast<std::uint32_t>(
					minimumUv.y * static_cast<float>(m_GridHeight)), m_GridHeight - 1u);
				maximumTileX = (std::min)(static_cast<std::uint32_t>(
					maximumUv.x * static_cast<float>(m_GridWidth)), m_GridWidth - 1u);
				maximumTileY = (std::min)(static_cast<std::uint32_t>(
					maximumUv.y * static_cast<float>(m_GridHeight)), m_GridHeight - 1u);
			}
		}

		VansLocalFogVolumeGPU volume{};
		volume.worldToLocal = worldToLocal;
		volume.extinctionAnisotropyCloudPadding = glm::vec4(
			1.0f / (std::max)(settings.visibilityDistanceMeters, 0.1f),
			settings.anisotropy,
			settings.receiveCloudShadows ? 1.0f : 0.0f,
			0.0f);
		volume.dimensionsAndEdgeFade = glm::vec4(
			dimensions, (std::max)(settings.edgeFadeDistanceMeters, 0.0f));
		volume.scatteringAlbedo = glm::vec4(
			glm::clamp(ToVec3(settings.singleScatteringAlbedo),
				glm::vec3(0.0f), glm::vec3(1.0f)), 0.0f);
		volume.emissivePerMeter = glm::vec4(
			glm::max(ToVec3(settings.emissivePerMeter), glm::vec3(0.0f)), 0.0f);
		volume.lightingAndDistanceFade = glm::vec4(
			settings.directLightingScale,
			settings.skyLightingScale,
			settings.distanceFadeStartMeters,
			settings.distanceFadeEndMeters);
		std::uint32_t featureFlags = 0u;
		std::uint32_t shapeHandle = 0u;
		if (settings.shapeMask.enabled && settings.shapeMask.influence > 1.0e-6f)
		{
			shapeHandle = m_LocalFogFieldResources.RegisterScalar(
				settings.shapeMask.source, settings.shapeMask.mapping);
			if (shapeHandle >= 2u)
				featureFlags |= LocalFogShapeEnabled;
		}
		std::uint32_t detailHandle = 0u;
		if (settings.detailNoise.enabled && settings.detailNoise.influence > 1.0e-6f)
		{
			detailHandle = m_LocalFogFieldResources.RegisterScalar(
				settings.detailNoise.source, settings.detailNoise.mapping);
			if (detailHandle >= 2u)
				featureFlags |= LocalFogDetailEnabled;
		}
		std::uint32_t flowHandle = 1u;
		const bool detailCanFlow = (featureFlags & LocalFogDetailEnabled) != 0u &&
			settings.detailNoise.mapping.addressMode ==
				Vans::VansLocalFogTextureAddressMode::Repeat;
		if (settings.flow.enabled && detailCanFlow &&
			settings.flow.speedMetersPerSecond > 1.0e-6f)
		{
			if (!settings.flow.source.assetGuid.empty())
				flowHandle = m_LocalFogFieldResources.RegisterVector2(
					settings.flow.source, settings.flow.mapping);
			if (flowHandle >= 2u)
			{
				featureFlags |= LocalFogFlowEnabled | LocalFogFlowHasTexture;
			}
			else
			{
				const glm::vec2 fallback(
					settings.flow.fallbackDirectionLocalXZ[0],
					settings.flow.fallbackDirectionLocalXZ[1]);
				if (glm::length(fallback) > 1.0e-6f)
					featureFlags |= LocalFogFlowEnabled;
			}
		}
		if (settings.shapeMask.invert)
			featureFlags |= LocalFogShapeInvert;
		if (settings.detailNoise.invert)
			featureFlags |= LocalFogDetailInvert;
		volume.fieldHandlesAndFlags = glm::uvec4(
			shapeHandle, detailHandle, flowHandle, featureFlags);
		volume.shapeTilingOffset = glm::vec4(
			settings.shapeMask.mapping.tiling[0], settings.shapeMask.mapping.tiling[1],
			settings.shapeMask.mapping.offset[0], settings.shapeMask.mapping.offset[1]);
		volume.shapeRemapInfluenceLod = glm::vec4(
			settings.shapeMask.inputMinimum, settings.shapeMask.inputMaximum,
			settings.shapeMask.influence, settings.shapeMask.lodBias);
		volume.detailTilingOffset = glm::vec4(
			settings.detailNoise.mapping.tiling[0], settings.detailNoise.mapping.tiling[1],
			settings.detailNoise.mapping.offset[0], settings.detailNoise.mapping.offset[1]);
		volume.detailRemapInfluenceLod = glm::vec4(
			settings.detailNoise.inputMinimum, settings.detailNoise.inputMaximum,
			settings.detailNoise.influence, settings.detailNoise.lodBias);
		volume.flowTilingOffset = glm::vec4(
			settings.flow.mapping.tiling[0], settings.flow.mapping.tiling[1],
			settings.flow.mapping.offset[0], settings.flow.mapping.offset[1]);
		volume.flowSpeedDistancePhaseLod = glm::vec4(
			settings.flow.speedMetersPerSecond, settings.flow.loopDistanceMeters,
			settings.flow.phaseOffset01, settings.flow.lodBias);
		volume.flowFallbackDirectionPadding = glm::vec4(
			settings.flow.fallbackDirectionLocalXZ[0],
			settings.flow.fallbackDirectionLocalXZ[1], 0.0f, 0.0f);
		const std::uint32_t volumeIndex =
			static_cast<std::uint32_t>(outVolumes.size());
		outVolumes.push_back(volume);

		for (std::uint32_t tileY = minimumTileY; tileY <= maximumTileY; ++tileY)
		for (std::uint32_t tileX = minimumTileX; tileX <= maximumTileX; ++tileX)
		{
			glm::uvec2& header = outTileHeaders[
				static_cast<std::size_t>(tileY) * m_GridWidth + tileX];
			if (header.y == std::numeric_limits<std::uint32_t>::max())
				continue;
			if (header.y >= MaxLocalFogCandidatesPerTile)
			{
				// 极端重叠时让 shader 回退遍历全部可见雾体，保证结果正确。
				header.y = std::numeric_limits<std::uint32_t>::max();
				continue;
			}
			outTileIndices[header.x + header.y] = volumeIndex;
			++header.y;
		}
	}
	m_LocalFogFieldDescriptorsDirty |= m_LocalFogFieldResources.EndBuild();
}

void VansNearMediaSystem::PrepareVolumetricParticles(
	const VansRenderViewSnapshot& view,
	bool featureRequested,
	const std::vector<VansVolumetricParticleInstanceData>& instances)
{
	if (!m_Initialized)
		return;
	if (featureRequested != m_PreviousVolumetricParticleFeatureRequested)
		m_HistoryValid = false;
	m_PreviousVolumetricParticleFeatureRequested = featureRequested;
	m_VolumetricParticleFeatureRequested = false;
	if (!featureRequested)
		return;

	if (!m_VolumetricParticleResourcesCreated &&
		!CreateVolumetricParticleResources())
	{
		if (!m_VolumetricParticlePreparationFailed)
			VANS_LOG_ERROR("[NearMedia] Cannot create optional volumetric-particle resources");
		m_VolumetricParticlePreparationFailed = true;
		return;
	}
	m_VolumetricParticlePreparationFailed = false;
	m_VolumetricParticleFeatureRequested = true;
	if (!m_VolumetricParticleFirstNonEmptyInputLogged && !instances.empty())
	{
		const glm::vec4 first = instances.front().m_WorldPositionRadius;
		VANS_LOG("[NearMedia] First non-empty volumetric-particle snapshot: count="
			<< instances.size() << ", firstCenter=(" << first.x << "," << first.y
			<< "," << first.z << "), radius=" << first.w << ", viewPosition=("
			<< view.position.x << "," << view.position.y << "," << view.position.z
			<< "), viewForward=(" << view.forward.x << "," << view.forward.y
			<< "," << view.forward.z << ")");
		m_VolumetricParticleFirstNonEmptyInputLogged = true;
	}

	const std::size_t tileCount =
		static_cast<std::size_t>(m_GridWidth) * m_GridHeight;
	m_VolumetricParticleScratch.clear();
	m_VolumetricParticleTileHeaderScratch.resize(tileCount);
	m_VolumetricParticleTileIndexScratch.assign(
		tileCount * MaxVolumetricParticleCandidatesPerTile, 0u);
	for (std::size_t tileIndex = 0; tileIndex < tileCount; ++tileIndex)
	{
		m_VolumetricParticleTileHeaderScratch[tileIndex] = glm::uvec2(
			static_cast<std::uint32_t>(tileIndex) *
				MaxVolumetricParticleCandidatesPerTile,
			0u);
	}

	std::vector<std::size_t> orderedIndices(instances.size());
	for (std::size_t index = 0; index < orderedIndices.size(); ++index)
		orderedIndices[index] = index;
	std::sort(orderedIndices.begin(), orderedIndices.end(),
		[&](std::size_t leftIndex, std::size_t rightIndex)
		{
			const auto& left = instances[leftIndex];
			const auto& right = instances[rightIndex];
			if (left.m_Metadata.w != right.m_Metadata.w)
				return left.m_Metadata.w > right.m_Metadata.w;
			if (left.m_Metadata.x != right.m_Metadata.x)
				return left.m_Metadata.x < right.m_Metadata.x;
			return leftIndex < rightIndex;
		});

	const auto& environment = m_Scene->GetEnvironmentSettings();
	const float effectiveFarDistanceMeters = environment.heightFog.enabled
		? (std::max)(m_Quality.farDistanceMeters,
			environment.heightFog.maximumDistanceMeters)
		: m_Quality.farDistanceMeters;
	const float nearDistance = (std::max)(m_Quality.nearDistanceMeters, 0.001f);
	const float farDistance = (std::max)(effectiveFarDistanceMeters,
		nearDistance + 0.001f);
	const float slicePower = (std::max)(m_Quality.sliceDistributionPower, 0.001f);
	const float tanHalfFov = std::tan((std::max)(view.fieldOfViewRadians, 0.01f) * 0.5f);
	const float aspect = (std::max)(view.aspectRatio, 0.01f);
	std::uint32_t overflowTileCount = 0u;
	constexpr std::uint32_t OverflowCandidateCount =
		std::numeric_limits<std::uint32_t>::max();

	for (std::size_t orderedIndex : orderedIndices)
	{
		VansVolumetricParticleInstanceData particle = instances[orderedIndex];
		if (m_VolumetricParticleScratch.size() >= MaxVolumetricParticles)
			break;
		const glm::vec3 center = glm::vec3(particle.m_WorldPositionRadius);
		const float radius = particle.m_WorldPositionRadius.w;
		const glm::vec3 relative = center - view.position;
		const float viewDepth = glm::dot(relative, view.forward);
		const float maximumDistance = (std::min)(
			particle.m_DistanceAndPadding.x, farDistance);
		if (radius <= 1.0e-5f || viewDepth + radius < nearDistance ||
			viewDepth - radius > maximumDistance)
			continue;

		glm::vec2 minimumUv(0.0f);
		glm::vec2 maximumUv(1.0f);
		if (viewDepth > radius + nearDistance)
		{
			const float projectionDepth = (std::max)(viewDepth - radius, nearDistance);
			const float halfHeight = (std::max)(tanHalfFov * viewDepth, 1.0e-4f);
			const float halfWidth = (std::max)(halfHeight * aspect, 1.0e-4f);
			const float conservativeHalfHeight =
				(std::max)(tanHalfFov * projectionDepth, 1.0e-4f);
			const float conservativeHalfWidth =
				(std::max)(conservativeHalfHeight * aspect, 1.0e-4f);
			const glm::vec2 centerUv(
				0.5f + 0.5f * glm::dot(relative, view.right) / halfWidth,
				0.5f - 0.5f * glm::dot(relative, view.up) / halfHeight);
			const glm::vec2 radiusUv(
				0.5f * radius / conservativeHalfWidth,
				0.5f * radius / conservativeHalfHeight);
			minimumUv = centerUv - radiusUv;
			maximumUv = centerUv + radiusUv;
			if (maximumUv.x < 0.0f || minimumUv.x > 1.0f ||
				maximumUv.y < 0.0f || minimumUv.y > 1.0f)
				continue;
			minimumUv = glm::clamp(minimumUv, glm::vec2(0.0f), glm::vec2(1.0f));
			maximumUv = glm::clamp(maximumUv, glm::vec2(0.0f), glm::vec2(1.0f));
		}

		const float minimumDepth = glm::clamp(
			viewDepth - radius, nearDistance, farDistance);
		const float maximumDepth = glm::clamp(
			viewDepth + radius, nearDistance, farDistance);
		auto depthToSlice = [&](float depth)
		{
			const float normalized = glm::clamp(
				(depth - nearDistance) / (farDistance - nearDistance), 0.0f, 1.0f);
			return static_cast<std::uint32_t>(glm::clamp(
				std::floor(std::pow(normalized, 1.0f / slicePower) *
					static_cast<float>(m_Quality.slices)),
				0.0f, static_cast<float>(m_Quality.slices - 1u)));
		};
		particle.m_Metadata.y = depthToSlice(minimumDepth);
		particle.m_Metadata.z = depthToSlice(maximumDepth);
		const std::uint32_t particleIndex = static_cast<std::uint32_t>(
			m_VolumetricParticleScratch.size());
		m_VolumetricParticleScratch.push_back(particle);

		const std::uint32_t minimumTileX = (std::min)(
			static_cast<std::uint32_t>(minimumUv.x * m_GridWidth), m_GridWidth - 1u);
		const std::uint32_t minimumTileY = (std::min)(
			static_cast<std::uint32_t>(minimumUv.y * m_GridHeight), m_GridHeight - 1u);
		const std::uint32_t maximumTileX = (std::min)(
			static_cast<std::uint32_t>(maximumUv.x * m_GridWidth), m_GridWidth - 1u);
		const std::uint32_t maximumTileY = (std::min)(
			static_cast<std::uint32_t>(maximumUv.y * m_GridHeight), m_GridHeight - 1u);
		for (std::uint32_t tileY = minimumTileY; tileY <= maximumTileY; ++tileY)
		for (std::uint32_t tileX = minimumTileX; tileX <= maximumTileX; ++tileX)
		{
			glm::uvec2& header = m_VolumetricParticleTileHeaderScratch[
				static_cast<std::size_t>(tileY) * m_GridWidth + tileX];
			if (header.y == OverflowCandidateCount)
				continue;
			if (header.y >= MaxVolumetricParticleCandidatesPerTile)
			{
				// 与 Local Fog 的可靠性策略一致：不能静默截断参与介质
				// 累加的粒子。溢出 Tile 在 Shader 中退化为完整粒子扫描。
				header.y = OverflowCandidateCount;
				++overflowTileCount;
				continue;
			}
			m_VolumetricParticleTileIndexScratch[header.x + header.y] = particleIndex;
			++header.y;
		}
	}
	if (overflowTileCount > 0u && !m_VolumetricParticleOverflowFallbackLogged)
	{
		VANS_LOG("[NearMedia] Volumetric-particle candidate overflow fallback active: tiles="
			<< overflowTileCount << ", candidatesPerTile="
			<< MaxVolumetricParticleCandidatesPerTile << ", particles="
			<< m_VolumetricParticleScratch.size());
		m_VolumetricParticleOverflowFallbackLogged = true;
	}

	if (!m_VolumetricParticleScratch.empty())
		m_VolumetricParticlesBuffer.SetBufferData(
			m_VolumetricParticleScratch.data(), 0,
			m_VolumetricParticleScratch.size() *
				sizeof(VansVolumetricParticleInstanceData));
	m_VolumetricParticleTileHeadersBuffer.SetBufferData(
		m_VolumetricParticleTileHeaderScratch.data(), 0,
		m_VolumetricParticleTileHeaderScratch.size() * sizeof(glm::uvec2));
	m_VolumetricParticleTileIndicesBuffer.SetBufferData(
		m_VolumetricParticleTileIndexScratch.data(), 0,
		m_VolumetricParticleTileIndexScratch.size() * sizeof(std::uint32_t));
	VansVolumetricParticleParamsGPU params{};
	params.particleCountCandidateLimitAndGrid = glm::uvec4(
		static_cast<std::uint32_t>(m_VolumetricParticleScratch.size()),
		MaxVolumetricParticleCandidatesPerTile,
		m_GridWidth, m_GridHeight);
	m_VolumetricParticleParamsBuffer.SetBufferData(&params, 0, sizeof(params));
}

void VansNearMediaSystem::UploadParameters()
{
	CollectLocalFogVolumes(
		m_LocalFogVolumeScratch,
		m_LocalFogTileHeaderScratch,
		m_LocalFogTileIndexScratch);
	UpdateLocalFogFieldDescriptors();
	const auto& volumes = m_LocalFogVolumeScratch;
	const auto& tileHeaders = m_LocalFogTileHeaderScratch;
	const auto& tileIndices = m_LocalFogTileIndexScratch;
	const auto& environment = m_Scene->GetEnvironmentSettings();
	const float effectiveFarDistanceMeters = environment.heightFog.enabled
		? (std::max)(m_Quality.farDistanceMeters,
			environment.heightFog.maximumDistanceMeters)
		: m_Quality.farDistanceMeters;
	const bool volumesChanged = volumes.size() != m_PreviousVolumes.size() ||
		(!volumes.empty() && std::memcmp(volumes.data(), m_PreviousVolumes.data(),
			volumes.size() * sizeof(VansLocalFogVolumeGPU)) != 0);
	const bool depthRangeChanged = std::abs(effectiveFarDistanceMeters -
		m_PreviousEffectiveFarDistanceMeters) > 0.001f;
	if (volumesChanged || depthRangeChanged)
		m_HistoryValid = false;
	m_PreviousVolumes = volumes;
	m_PreviousEffectiveFarDistanceMeters = effectiveFarDistanceMeters;

	std::array<VansLocalFogVolumeGPU, MaxLocalFogVolumes> upload{};
	std::copy(volumes.begin(), volumes.end(), upload.begin());
	m_LocalFogVolumesBuffer.SetBufferData(
		upload.data(), 0, sizeof(upload));
	m_LocalFogTileHeadersBuffer.SetBufferData(tileHeaders.data(), 0,
		tileHeaders.size() * sizeof(glm::uvec2));
	m_LocalFogTileIndicesBuffer.SetBufferData(tileIndices.data(), 0,
		tileIndices.size() * sizeof(std::uint32_t));

	VansNearMediaParamsGPU params{};
	params.depthRangeAndGrid = glm::vec4(
		m_Quality.nearDistanceMeters,
		effectiveFarDistanceMeters,
		static_cast<float>(m_GridWidth),
		static_cast<float>(m_GridHeight));
	params.sliceHistoryAndVolumeCount = glm::vec4(
		m_Quality.sliceDistributionPower,
		(m_HistoryValid && m_Quality.temporalReprojection) ? 1.0f : 0.0f,
		m_Quality.historyWeight,
		static_cast<float>(volumes.size()));
	params.localFogTileGridAndLimits = glm::vec4(
		static_cast<float>(m_GridWidth),
		static_cast<float>(m_GridHeight),
		static_cast<float>(MaxLocalFogCandidatesPerTile),
		m_Quality.temporalReprojection ? 1.0f : 0.0f);
	params.lightTransmittance = glm::vec4(
		static_cast<float>(m_Quality.lightTransmittanceSamples),
		m_Quality.lightTransmittanceMaxDistanceMeters,
		0.0f, 0.0f);
	m_ParamsBuffer.SetBufferData(&params, 0, sizeof(params));
}

void VansNearMediaSystem::BindGlobalDescriptor(VkDescriptorSet globalSet)
{
	if (!m_ParamsBufferCreated || globalSet == VK_NULL_HANDLE)
		return;
	auto* descriptors = VansVKDescriptorManager::GetInstance();
	descriptors->BeginDescriptorUpdate();
	descriptors->WriteBufferDescriptor(globalSet, GLOBAL_BINDING_LOCAL_MEDIA_UBO,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		{{ m_ParamsBuffer.GetNativeBuffer(), 0, sizeof(VansNearMediaParamsGPU) }});
	descriptors->CommitDescriptorUpdates();
}

void VansNearMediaSystem::TransitionForWrite(VansVKCommandBuffer& commandBuffer,
	VansVKImage& image, bool initialized)
{
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = initialized ?
		(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT) : 0;
	barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.oldLayout = initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image.GetImage();
	barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	commandBuffer.PipelineBarrier(
		initialized ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {}, {}, { barrier });
	image.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
}

void VansNearMediaSystem::BarrierForSampling(
	VansVKCommandBuffer& commandBuffer, VansVKImage& image)
{
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image.GetImage();
	barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {}, {}, { barrier });
}

void VansNearMediaSystem::Record(VansVKCommandBuffer& commandBuffer)
{
	if (!m_Initialized)
		return;
	UploadParameters();
	const std::uint32_t target = m_FrameParity & 1u;
	const std::uint32_t previous = target ^ 1u;
	const bool withVolumetricParticles =
		m_VolumetricParticleFeatureRequested &&
		m_VolumetricParticleResourcesCreated;
	// 历史描述符始终存在；首次使用前先为尚未写过的另一槽建立合法布局。
	// 内容有效性仍只由 m_HistoryValid 控制，不能与图片布局状态混用。
	if (!m_InjectionInitialized[previous])
	{
		TransitionForWrite(commandBuffer, m_Injection[previous], false);
		BarrierForSampling(commandBuffer, m_Injection[previous]);
		m_InjectionInitialized[previous] = true;
	}

	TransitionForWrite(commandBuffer, m_MaterialScatteringExtinction,
		m_MaterialVolumesInitialized);
	TransitionForWrite(commandBuffer, m_MaterialLightingPhaseCloud,
		m_MaterialVolumesInitialized);
	TransitionForWrite(commandBuffer, m_MaterialEmissiveWeight,
		m_MaterialVolumesInitialized);
	if (withVolumetricParticles)
	{
		if (!m_VolumetricParticleActivityInitialized[previous])
		{
			TransitionForWrite(commandBuffer,
				m_VolumetricParticleActivity[previous], false);
			BarrierForSampling(commandBuffer,
				m_VolumetricParticleActivity[previous]);
			m_VolumetricParticleActivityInitialized[previous] = true;
		}
		TransitionForWrite(commandBuffer,
			m_VolumetricParticleActivity[target],
			m_VolumetricParticleActivityInitialized[target]);
		// Local Fog 与粒子是独立提供者，只向公共材质 V-Buffer 注入
		// sigmaS、sigmaT、emissive 与 phase/lighting 权重。
		commandBuffer.EnsureComputeShader(*m_NearMediaUnifiedInjectionShader,
			{ m_Scene->GetGlobalDescriptorSetLayout(),
			  m_VolumetricParticlePassLayout });
		commandBuffer.DispatchCompute(*m_NearMediaUnifiedInjectionShader,
			DivideRoundUp(m_GridWidth, 4u), DivideRoundUp(m_GridHeight, 4u),
			DivideRoundUp(m_Quality.slices, 4u),
			{ m_Scene->GetGlobalDescriptorSet(),
			  m_VolumetricParticlePassSets[target] });
		BarrierForSampling(commandBuffer,
			m_VolumetricParticleActivity[target]);
		m_VolumetricParticleActivityInitialized[target] = true;
	}
	else
	{
		commandBuffer.EnsureComputeShader(*m_InjectionShader,
			{ m_Scene->GetGlobalDescriptorSetLayout(), m_PassLayout });
		commandBuffer.DispatchCompute(*m_InjectionShader,
			DivideRoundUp(m_GridWidth, 4u), DivideRoundUp(m_GridHeight, 4u),
			DivideRoundUp(m_Quality.slices, 4u),
			{ m_Scene->GetGlobalDescriptorSet(), m_PassSets[target] });
	}
	BarrierForSampling(commandBuffer, m_MaterialScatteringExtinction);
	BarrierForSampling(commandBuffer, m_MaterialLightingPhaseCloud);
	BarrierForSampling(commandBuffer, m_MaterialEmissiveWeight);
	m_MaterialVolumesInitialized = true;

	// 唯一的 NearMedia 光照阶段读取公共材质体素，并将普通阴影与
	// light-to-froxel 介质透射相乘。它不读取任何具体介质提供者数据。
	TransitionForWrite(commandBuffer, m_RawInjection,
		m_RawInjectionInitialized);
	commandBuffer.EnsureComputeShader(*m_LightingShader,
		{ m_Scene->GetGlobalDescriptorSetLayout(),
		  withVolumetricParticles ? m_VolumetricParticlePassLayout : m_PassLayout });
	commandBuffer.DispatchCompute(*m_LightingShader,
		DivideRoundUp(m_GridWidth, 4u), DivideRoundUp(m_GridHeight, 4u),
		DivideRoundUp(m_Quality.slices, 4u),
		{ m_Scene->GetGlobalDescriptorSet(),
		  withVolumetricParticles ?
			m_VolumetricParticlePassSets[target] : m_PassSets[target] });
	BarrierForSampling(commandBuffer, m_RawInjection);
	m_RawInjectionInitialized = true;

	TransitionForWrite(commandBuffer, m_Injection[target],
		m_InjectionInitialized[target]);
	if (withVolumetricParticles)
	{
		commandBuffer.EnsureComputeShader(
			*m_VolumetricParticleTemporalResolveShader,
			{ m_Scene->GetGlobalDescriptorSetLayout(),
			  m_VolumetricParticlePassLayout });
		commandBuffer.DispatchCompute(
			*m_VolumetricParticleTemporalResolveShader,
			DivideRoundUp(m_GridWidth, 4u), DivideRoundUp(m_GridHeight, 4u),
			DivideRoundUp(m_Quality.slices, 4u),
			{ m_Scene->GetGlobalDescriptorSet(),
			  m_VolumetricParticlePassSets[target] });
		if (!m_VolumetricParticleFirstDispatchLogged &&
			!m_VolumetricParticleScratch.empty())
		{
			VANS_LOG("[NearMedia] Particle material injection and unified light transmittance active: visibleParticles="
				<< m_VolumetricParticleScratch.size());
			m_VolumetricParticleFirstDispatchLogged = true;
		}
	}
	else
	{
		commandBuffer.EnsureComputeShader(*m_TemporalResolveShader,
			{ m_Scene->GetGlobalDescriptorSetLayout(), m_PassLayout });
		commandBuffer.DispatchCompute(*m_TemporalResolveShader,
			DivideRoundUp(m_GridWidth, 4u), DivideRoundUp(m_GridHeight, 4u),
			DivideRoundUp(m_Quality.slices, 4u),
			{ m_Scene->GetGlobalDescriptorSet(), m_PassSets[target] });
	}
	BarrierForSampling(commandBuffer, m_Injection[target]);
	m_InjectionInitialized[target] = true;
	m_HasResolvedInjection = true;
	TransitionForWrite(commandBuffer, m_Scattering, m_OutputInitialized);
	TransitionForWrite(commandBuffer, m_OpticalDepth, m_OutputInitialized);
	commandBuffer.EnsureComputeShader(*m_IntegrationShader,
		{ m_Scene->GetGlobalDescriptorSetLayout(), m_PassLayout });
	commandBuffer.DispatchCompute(*m_IntegrationShader,
		DivideRoundUp(m_GridWidth, 8u), DivideRoundUp(m_GridHeight, 8u), 1u,
		{ m_Scene->GetGlobalDescriptorSet(), m_PassSets[target] });
	BarrierForSampling(commandBuffer, m_Scattering);
	BarrierForSampling(commandBuffer, m_OpticalDepth);
	m_HistoryValid = true;
	m_OutputInitialized = true;
	m_FrameParity ^= 1u;
}

VkDescriptorImageInfo VansNearMediaSystem::GetScatteringDescriptor()
{
	return { m_Scattering.GetSampler(), m_Scattering.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
}

VkDescriptorImageInfo VansNearMediaSystem::GetOpticalDepthDescriptor()
{
	return { m_OpticalDepth.GetSampler(), m_OpticalDepth.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
}

VkDescriptorImageInfo VansNearMediaSystem::GetCurrentInjectionDescriptor()
{
	const std::uint32_t current = m_HasResolvedInjection ?
		((m_FrameParity ^ 1u) & 1u) : 0u;
	return { m_Injection[current].GetSampler(),
		m_Injection[current].GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
}

bool VansNearMediaSystem::Reinitialize(const VansNearMediaQualityConfig& quality,
	std::uint32_t renderWidth, std::uint32_t renderHeight)
{
	if (!m_Device || !m_Scene)
		return false;
	VansVKDevice* device = m_Device;
	VansScene* scene = m_Scene;
	device->WaitForDevice();
	return Initialize(*device, *scene, quality, renderWidth, renderHeight);
}

void VansNearMediaSystem::DestroyDescriptors()
{
	DestroyVolumetricParticleDescriptors();
	auto* descriptors = VansVKDescriptorManager::GetInstance();
	std::vector<VkDescriptorSet> sets;
	for (VkDescriptorSet& set : m_PassSets)
	{
		if (set != VK_NULL_HANDLE)
			sets.push_back(set);
		set = VK_NULL_HANDLE;
	}
	if (!sets.empty())
		descriptors->DestroyDescriptorSet(sets);
	if (m_PassLayout != VK_NULL_HANDLE)
	{
		descriptors->DestroyDescriptorSetLayout(m_PassLayout);
		m_PassLayout = VK_NULL_HANDLE;
	}
}

void VansNearMediaSystem::DestroyVolumetricParticleDescriptors()
{
	auto* descriptors = VansVKDescriptorManager::GetInstance();
	std::vector<VkDescriptorSet> sets;
	for (VkDescriptorSet& set : m_VolumetricParticlePassSets)
	{
		if (set != VK_NULL_HANDLE)
			sets.push_back(set);
		set = VK_NULL_HANDLE;
	}
	if (!sets.empty())
		descriptors->DestroyDescriptorSet(sets);
	if (m_VolumetricParticlePassLayout != VK_NULL_HANDLE)
	{
		descriptors->DestroyDescriptorSetLayout(m_VolumetricParticlePassLayout);
		m_VolumetricParticlePassLayout = VK_NULL_HANDLE;
	}
}

void VansNearMediaSystem::DestroyResources()
{
	if (!m_Device)
		return;
	DestroyVolumetricParticleResources();
	m_LocalFogFieldResources.Shutdown();
	VkDevice device = m_Device->GetLogicDevice();
	m_RawInjection.DestroyVulkanImage(device);
	m_MaterialScatteringExtinction.DestroyVulkanImage(device);
	m_MaterialLightingPhaseCloud.DestroyVulkanImage(device);
	m_MaterialEmissiveWeight.DestroyVulkanImage(device);
	for (auto& injection : m_Injection)
		injection.DestroyVulkanImage(device);
	m_Scattering.DestroyVulkanImage(device);
	m_OpticalDepth.DestroyVulkanImage(device);
	if (m_ParamsBufferCreated)
		m_ParamsBuffer.DestroyVulkanBuffer(device);
	if (m_LocalFogVolumesBufferCreated)
		m_LocalFogVolumesBuffer.DestroyVulkanBuffer(device);
	if (m_LocalFogTileHeadersBufferCreated)
		m_LocalFogTileHeadersBuffer.DestroyVulkanBuffer(device);
	if (m_LocalFogTileIndicesBufferCreated)
		m_LocalFogTileIndicesBuffer.DestroyVulkanBuffer(device);
	m_ParamsBufferCreated = false;
	m_LocalFogVolumesBufferCreated = false;
	m_LocalFogTileHeadersBufferCreated = false;
	m_LocalFogTileIndicesBufferCreated = false;
	m_LocalFogFieldDescriptorsDirty = false;
}

void VansNearMediaSystem::DestroyVolumetricParticleResources()
{
	if (!m_Device)
		return;
	VkDevice device = m_Device->GetLogicDevice();
	for (auto& activity : m_VolumetricParticleActivity)
		activity.DestroyVulkanImage(device);
	if (m_VolumetricParticlesBufferCreated)
		m_VolumetricParticlesBuffer.DestroyVulkanBuffer(device);
	if (m_VolumetricParticleTileHeadersBufferCreated)
		m_VolumetricParticleTileHeadersBuffer.DestroyVulkanBuffer(device);
	if (m_VolumetricParticleTileIndicesBufferCreated)
		m_VolumetricParticleTileIndicesBuffer.DestroyVulkanBuffer(device);
	if (m_VolumetricParticleParamsBufferCreated)
		m_VolumetricParticleParamsBuffer.DestroyVulkanBuffer(device);
	m_VolumetricParticlesBufferCreated = false;
	m_VolumetricParticleTileHeadersBufferCreated = false;
	m_VolumetricParticleTileIndicesBufferCreated = false;
	m_VolumetricParticleParamsBufferCreated = false;
	m_VolumetricParticleResourcesCreated = false;
	m_VolumetricParticleActivityInitialized[0] = false;
	m_VolumetricParticleActivityInitialized[1] = false;
}

void VansNearMediaSystem::Shutdown()
{
	if (m_Device)
	{
		DestroyDescriptors();
		DestroyResources();
	}
	m_Device = nullptr;
	m_Scene = nullptr;
	m_LocalFogRegistry.clear();
	m_LocalFogVolumeScratch.clear();
	m_LocalFogTileHeaderScratch.clear();
	m_LocalFogTileIndexScratch.clear();
	m_LocalFogRegistryGeneration = UINT64_MAX;
	m_PreviousVolumes.clear();
	m_VolumetricParticleScratch.clear();
	m_VolumetricParticleTileHeaderScratch.clear();
	m_VolumetricParticleTileIndexScratch.clear();
	m_PreviousEffectiveFarDistanceMeters = 0.0f;
	m_RawInjectionInitialized = false;
	m_MaterialVolumesInitialized = false;
	m_InjectionInitialized[0] = false;
	m_InjectionInitialized[1] = false;
	m_HasResolvedInjection = false;
	m_HistoryValid = false;
	m_OutputInitialized = false;
	m_VolumetricParticleResourcesCreated = false;
	m_VolumetricParticleFeatureRequested = false;
	m_PreviousVolumetricParticleFeatureRequested = false;
	m_VolumetricParticlePreparationFailed = false;
	m_VolumetricParticleFirstNonEmptyInputLogged = false;
	m_VolumetricParticleFirstDispatchLogged = false;
	m_VolumetricParticleOverflowFallbackLogged = false;
	m_FrameParity = 0;
	m_Initialized = false;
}
}
