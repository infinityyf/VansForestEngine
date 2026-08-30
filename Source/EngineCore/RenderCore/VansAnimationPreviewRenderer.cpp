#include "VansAnimationPreviewRenderer.h"

#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include "../RuntimeCore/VansThreadContract.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace VansGraphics
{
namespace
{
	constexpr std::size_t MaxPreviewTriangles = 30000;

	std::uint8_t ToByte(float value)
	{
		value = std::clamp(value, 0.0f, 1.0f);
		return static_cast<std::uint8_t>(std::lround(value * 255.0f));
	}

	float Edge(float ax, float ay, float bx, float by, float px, float py)
	{
		return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
	}

	glm::vec3 RotateForView(const glm::vec3& value, float cy, float sy, float cp, float sp)
	{
		const float x = cy * value.x + sy * value.z;
		const float z = -sy * value.x + cy * value.z;
		return { x, cp * value.y - sp * z, sp * value.y + cp * z };
	}

	bool IsFinite(const glm::vec3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	bool IsFinite(const glm::vec4& value)
	{
		return IsFinite(glm::vec3(value)) && std::isfinite(value.w);
	}
}

VansAnimationPreviewRenderer::~VansAnimationPreviewRenderer()
{
	Shutdown();
}

bool VansAnimationPreviewRenderer::PrepareCpu(
	const std::filesystem::path& modelPath,
	float scaleFactor,
	const Vans::VansSkeletalMeshImportSettings& importSettings,
	std::string& error)
{
	if (m_Device != nullptr || m_Ready)
	{
		error = "Animation preview GPU resource is still active";
		return false;
	}
	m_Skeleton = {};
	m_Vertices.clear();
	m_Indices.clear();
	m_ProjectedVertices.clear();
	m_Pixels.clear();
	m_Depth.clear();
	m_Stats = {};
	if (!LoadCpuModel(modelPath, scaleFactor, importSettings, error))
		return false;

	m_Pixels.resize(static_cast<std::size_t>(PreviewWidth) * PreviewHeight * 4);
	m_Depth.resize(static_cast<std::size_t>(PreviewWidth) * PreviewHeight);
	m_ProjectedVertices.resize(m_Vertices.size());
	m_Stats.width = PreviewWidth;
	m_Stats.height = PreviewHeight;
	m_Stats.vertexCount = m_Vertices.size();
	m_Stats.sourceTriangleCount = m_Indices.size() / 3;
	m_Stats.renderedTriangleCount =
		(m_Stats.sourceTriangleCount + m_TriangleStride - 1) / m_TriangleStride;
	for (const CpuVertex& vertex : m_Vertices)
	{
		float weightSum = 0.0f;
		bool hasInfluence = false;
		for (std::uint32_t influence = 0; influence < MAX_BONE_INFLUENCE; ++influence)
		{
			const int boneId = vertex.skin.boneIDs[influence];
			const float weight = vertex.skin.weights[influence];
			if (!std::isfinite(weight))
			{
				++m_Stats.nonFiniteBoneWeightCount;
				continue;
			}
			if (boneId < 0 || weight <= 0.0f)
				continue;
			hasInfluence = true;
			if (static_cast<std::size_t>(boneId) >= m_Skeleton.bones.size())
			{
				++m_Stats.invalidBoneInfluenceCount;
				continue;
			}
			weightSum += weight;
		}
		if (!hasInfluence)
		{
			++m_Stats.unboundVertexCount;
		}
		else
		{
			m_Stats.maxBoneWeightSumError = std::max(
				m_Stats.maxBoneWeightSumError,
				std::abs(weightSum - 1.0f));
		}
	}

	BoneMatricesSSBO bindMatrices{};
	for (glm::mat4& matrix : bindMatrices.boneMatrices)
		matrix = glm::mat4(1.0f);
	const auto start = std::chrono::steady_clock::now();
	Rasterize(bindMatrices, {}, glm::vec3(0.0f), {});
	m_Stats.renderMilliseconds = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - start).count();
	return true;
}

bool VansAnimationPreviewRenderer::InitializeGpuRenderThread(
	VansVKDevice& device,
	std::string& error)
{
	VANS_ASSERT_RENDER_THREAD();
	if (m_Pixels.empty() || m_Vertices.empty())
	{
		error = "Animation preview CPU data is not prepared";
		return false;
	}
	m_Device = &device;
	VkExtent3D extent{ PreviewWidth, PreviewHeight, 1 };
	VkDevice& logicalDevice = device.GetLogicDevice();
	if (!m_ColorImage.CreateVulkanImage(
		logicalDevice,
		extent,
		VK_FORMAT_R8G8B8A8_UNORM,
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,
		false,
		true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
	{
		error = "Failed to create isolated animation preview image";
		Shutdown();
		return false;
	}
	m_Ready = true;
	return UploadRenderThread(error);
}

void VansAnimationPreviewRenderer::Shutdown()
{
	if (m_Device)
	{
		VANS_ASSERT_RENDER_THREAD();
		VkDevice& logicalDevice = m_Device->GetLogicDevice();
		if (logicalDevice != VK_NULL_HANDLE)
			m_ColorImage.DestroyVulkanImage(logicalDevice);
	}
	m_Device = nullptr;
	m_Skeleton = {};
	m_Vertices.clear();
	m_Indices.clear();
	m_ProjectedVertices.clear();
	m_Pixels.clear();
	m_Depth.clear();
	m_ModelCenter = glm::vec3(0.0f);
	m_ModelRadius = 1.0f;
	m_TriangleStride = 1;
	m_Stats = {};
	m_Ready = false;
}

bool VansAnimationPreviewRenderer::LoadCpuModel(
	const std::filesystem::path& modelPath,
	float scaleFactor,
	const Vans::VansSkeletalMeshImportSettings& importSettings,
	std::string& error)
{
	VkDevice nullDevice = VK_NULL_HANDLE;
	VkQueue nullQueue = VK_NULL_HANDLE;
	VansMesh imported(true, false);
	imported.LoadMultiMesh(
		nullDevice,
		nullQueue,
		nullptr,
		modelPath.string(),
		false,
		false,
		true,
		scaleFactor,
		importSettings,
		{},
		false,
		false);

	if (imported.m_SubMeshes.empty())
	{
		error = "Animation preview model contains no renderable submeshes";
		return false;
	}
	if (imported.m_AnimImportResult.skeleton.bones.empty())
	{
		error = "Animation preview model contains no skeletal hierarchy";
		return false;
	}
	m_Skeleton = imported.m_AnimImportResult.skeleton;

	glm::vec3 boundsMin(std::numeric_limits<float>::max());
	glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
	for (std::size_t submeshIndex = 0;
		submeshIndex < imported.m_SubMeshes.size(); ++submeshIndex)
	{
		const VansMesh* submesh = imported.m_SubMeshes[submeshIndex];
		if (!submesh) continue;
		const std::vector<float>& raw = submesh->GetMeshRawPositionData();
		const std::vector<int>& sourceIndices = submesh->GetMeshTriangleIndex();
		if (raw.size() < 8 || sourceIndices.empty()) continue;

		const std::size_t vertexCount = raw.size() / 8;
		const std::uint32_t baseVertex = static_cast<std::uint32_t>(m_Vertices.size());
		glm::vec3 baseColor(0.68f, 0.72f, 0.78f);
		if (submeshIndex < imported.m_SubmeshMaterialInfos.size())
		{
			const auto& color = imported.m_SubmeshMaterialInfos[submeshIndex].diffuseColor;
			baseColor = glm::clamp(glm::vec3(color[0], color[1], color[2]),
				glm::vec3(0.08f), glm::vec3(1.0f));
		}
		const std::vector<VertexBoneData>* skin =
			submeshIndex < imported.m_SubMeshBoneData.size()
				? &imported.m_SubMeshBoneData[submeshIndex]
				: nullptr;

		m_Vertices.reserve(m_Vertices.size() + vertexCount);
		for (std::size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
		{
			const std::size_t offset = vertexIndex * 8;
			CpuVertex vertex;
			vertex.bindPosition = { raw[offset], raw[offset + 1], raw[offset + 2] };
			vertex.bindNormal = glm::normalize(glm::vec3(
				raw[offset + 4], raw[offset + 5], raw[offset + 6]));
			if (!IsFinite(vertex.bindNormal))
				vertex.bindNormal = glm::vec3(0.0f, 1.0f, 0.0f);
			vertex.baseColor = baseColor;
			if (skin && vertexIndex < skin->size()) vertex.skin = (*skin)[vertexIndex];
			boundsMin = glm::min(boundsMin, vertex.bindPosition);
			boundsMax = glm::max(boundsMax, vertex.bindPosition);
			m_Vertices.push_back(vertex);
		}
		m_Indices.reserve(m_Indices.size() + sourceIndices.size());
		for (int index : sourceIndices)
			if (index >= 0 && static_cast<std::size_t>(index) < vertexCount)
				m_Indices.push_back(baseVertex + static_cast<std::uint32_t>(index));
	}

	if (m_Vertices.empty() || m_Indices.size() < 3)
	{
		error = "Animation preview model produced empty CPU geometry";
		return false;
	}
	m_ModelCenter = (boundsMin + boundsMax) * 0.5f;
	const glm::vec3 extent = (boundsMax - boundsMin) * 0.5f;
	m_ModelRadius = std::max({ extent.x, extent.y, extent.z, 0.0001f });
	const std::size_t triangleCount = m_Indices.size() / 3;
	m_TriangleStride = std::max<std::size_t>(
		1, (triangleCount + MaxPreviewTriangles - 1) / MaxPreviewTriangles);
	return true;
}

bool VansAnimationPreviewRenderer::RasterizeFrame(
	const BoneMatricesSSBO& boneMatrices,
	const std::vector<glm::vec4>& perBoneVisualizationColors,
	const glm::vec3& modelOffset,
	const VansAnimationPreviewView& view,
	std::string& error)
{
	if (m_Pixels.empty() || m_Vertices.empty())
	{
		error = "Animation preview CPU data is not prepared";
		return false;
	}
	const auto start = std::chrono::steady_clock::now();
	Rasterize(boneMatrices, perBoneVisualizationColors, modelOffset, view);
	m_Stats.renderMilliseconds = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - start).count();
	return true;
}

void VansAnimationPreviewRenderer::Rasterize(
	const BoneMatricesSSBO& boneMatrices,
	const std::vector<glm::vec4>& perBoneVisualizationColors,
	const glm::vec3& modelOffset,
	const VansAnimationPreviewView& view)
{
	glm::vec3 deformedBoundsMin(std::numeric_limits<float>::max());
	glm::vec3 deformedBoundsMax(std::numeric_limits<float>::lowest());
	float deformedRadius = 0.0f;
	std::size_t validDeformedVertexCount = 0;
	m_Stats.invalidDeformedVertexCount = 0;
	for (std::uint32_t y = 0; y < PreviewHeight; ++y)
	{
		const float gradient = static_cast<float>(y) / static_cast<float>(PreviewHeight - 1);
		const glm::vec3 background = glm::mix(
			glm::vec3(0.045f, 0.055f, 0.075f),
			glm::vec3(0.10f, 0.115f, 0.145f), gradient);
		for (std::uint32_t x = 0; x < PreviewWidth; ++x)
		{
			const std::size_t pixel = static_cast<std::size_t>(y) * PreviewWidth + x;
			m_Pixels[pixel * 4] = ToByte(background.r);
			m_Pixels[pixel * 4 + 1] = ToByte(background.g);
			m_Pixels[pixel * 4 + 2] = ToByte(background.b);
			m_Pixels[pixel * 4 + 3] = 255;
		}
	}
	std::fill(m_Depth.begin(), m_Depth.end(), -std::numeric_limits<float>::infinity());

	const float yaw = std::isfinite(view.yaw) ? view.yaw : 0.0f;
	const float pitch = std::clamp(std::isfinite(view.pitch) ? view.pitch : 0.0f, -1.45f, 1.45f);
	const float zoom = std::clamp(std::isfinite(view.zoom) ? view.zoom : 1.0f, 0.2f, 3.0f);
	const float cy = std::cos(yaw), sy = std::sin(yaw);
	const float cp = std::cos(pitch), sp = std::sin(pitch);
	const float pixelsPerUnit = 0.43f * static_cast<float>((std::min)(PreviewWidth, PreviewHeight))
		* zoom / m_ModelRadius;
	const glm::vec3 lightDirection = glm::normalize(glm::vec3(-0.35f, 0.65f, 0.68f));

	for (std::size_t vertexIndex = 0; vertexIndex < m_Vertices.size(); ++vertexIndex)
	{
		const CpuVertex& source = m_Vertices[vertexIndex];
		glm::vec4 position(0.0f);
		glm::vec3 normal(0.0f);
		glm::vec4 visualization(0.0f);
		float totalWeight = 0.0f;
		for (std::uint32_t influence = 0; influence < MAX_BONE_INFLUENCE; ++influence)
		{
			const int bone = source.skin.boneIDs[influence];
			const float weight = source.skin.weights[influence];
			if (bone < 0 || bone >= static_cast<int>(MAX_BONES) || weight <= 0.0f) continue;
			const glm::mat4& matrix = boneMatrices.boneMatrices[bone];
			position += matrix * glm::vec4(source.bindPosition, 1.0f) * weight;
			normal += glm::mat3(matrix) * source.bindNormal * weight;
			if (bone < static_cast<int>(perBoneVisualizationColors.size()))
				visualization += perBoneVisualizationColors[bone] * weight;
			totalWeight += weight;
		}
		if (totalWeight <= 0.00001f)
		{
			position = glm::vec4(source.bindPosition, 1.0f);
			normal = source.bindNormal;
		}
		else if (std::abs(totalWeight - 1.0f) > 0.0001f)
		{
			position /= totalWeight;
			normal /= totalWeight;
			visualization /= totalWeight;
		}
		if (!IsFinite(position) || !IsFinite(normal))
		{
			m_ProjectedVertices[vertexIndex].valid = false;
			++m_Stats.invalidDeformedVertexCount;
			continue;
		}
		const glm::vec3 deformedPosition(position);
		deformedBoundsMin = glm::min(deformedBoundsMin, deformedPosition);
		deformedBoundsMax = glm::max(deformedBoundsMax, deformedPosition);
		deformedRadius = std::max(
			deformedRadius, glm::length(deformedPosition - m_ModelCenter));
		++validDeformedVertexCount;

		const glm::vec3 viewPosition = RotateForView(
			glm::vec3(position) + modelOffset - m_ModelCenter, cy, sy, cp, sp);
		glm::vec3 viewNormal = RotateForView(glm::normalize(normal), cy, sy, cp, sp);
		const float diffuse = std::max(0.18f, glm::dot(viewNormal, lightDirection));
		const float rim = std::pow(std::clamp(1.0f - std::abs(viewNormal.z), 0.0f, 1.0f), 2.0f) * 0.18f;
		const float visualizationAlpha = std::clamp(visualization.a, 0.0f, 1.0f);
		const glm::vec3 surface = glm::mix(source.baseColor, glm::vec3(visualization), visualizationAlpha);

		ProjectedVertex& projected = m_ProjectedVertices[vertexIndex];
		projected.x = static_cast<float>(PreviewWidth) * 0.5f + viewPosition.x * pixelsPerUnit;
		projected.y = static_cast<float>(PreviewHeight) * 0.5f - viewPosition.y * pixelsPerUnit;
		projected.depth = viewPosition.z;
		projected.color = glm::clamp(surface * (0.32f + diffuse * 0.78f) + rim,
			glm::vec3(0.0f), glm::vec3(1.0f));
		projected.valid = std::isfinite(projected.x) && std::isfinite(projected.y)
			&& std::isfinite(projected.depth);
	}
	if (validDeformedVertexCount > 0)
	{
		m_Stats.deformedBoundsMin = deformedBoundsMin;
		m_Stats.deformedBoundsMax = deformedBoundsMax;
		m_Stats.deformedRadiusRatio = deformedRadius / m_ModelRadius;
	}
	else
	{
		m_Stats.deformedBoundsMin = glm::vec3(0.0f);
		m_Stats.deformedBoundsMax = glm::vec3(0.0f);
		m_Stats.deformedRadiusRatio = 0.0f;
	}

	const std::size_t triangleCount = m_Indices.size() / 3;
	for (std::size_t triangle = 0; triangle < triangleCount; triangle += m_TriangleStride)
	{
		const std::size_t firstIndex = triangle * 3;
		const std::uint32_t ia = m_Indices[firstIndex];
		const std::uint32_t ib = m_Indices[firstIndex + 1];
		const std::uint32_t ic = m_Indices[firstIndex + 2];
		if (ia >= m_ProjectedVertices.size() || ib >= m_ProjectedVertices.size()
			|| ic >= m_ProjectedVertices.size()) continue;
		const ProjectedVertex& a = m_ProjectedVertices[ia];
		const ProjectedVertex& b = m_ProjectedVertices[ib];
		const ProjectedVertex& c = m_ProjectedVertices[ic];
		if (!a.valid || !b.valid || !c.valid) continue;
		const float area = Edge(a.x, a.y, b.x, b.y, c.x, c.y);
		if (std::abs(area) < 0.25f) continue;

		const int minX = std::clamp(static_cast<int>(std::floor((std::min)({ a.x, b.x, c.x }))),
			0, static_cast<int>(PreviewWidth) - 1);
		const int maxX = std::clamp(static_cast<int>(std::ceil((std::max)({ a.x, b.x, c.x }))),
			0, static_cast<int>(PreviewWidth) - 1);
		const int minY = std::clamp(static_cast<int>(std::floor((std::min)({ a.y, b.y, c.y }))),
			0, static_cast<int>(PreviewHeight) - 1);
		const int maxY = std::clamp(static_cast<int>(std::ceil((std::max)({ a.y, b.y, c.y }))),
			0, static_cast<int>(PreviewHeight) - 1);
		if (minX > maxX || minY > maxY) continue;

		const float inverseArea = 1.0f / area;
		for (int y = minY; y <= maxY; ++y)
		{
			for (int x = minX; x <= maxX; ++x)
			{
				const float px = static_cast<float>(x) + 0.5f;
				const float py = static_cast<float>(y) + 0.5f;
				const float wa = Edge(b.x, b.y, c.x, c.y, px, py) * inverseArea;
				const float wb = Edge(c.x, c.y, a.x, a.y, px, py) * inverseArea;
				const float wc = 1.0f - wa - wb;
				if (wa < -0.0001f || wb < -0.0001f || wc < -0.0001f) continue;
				const float depth = wa * a.depth + wb * b.depth + wc * c.depth;
				const std::size_t pixel = static_cast<std::size_t>(y) * PreviewWidth + x;
				if (depth <= m_Depth[pixel]) continue;
				m_Depth[pixel] = depth;
				const glm::vec3 color = wa * a.color + wb * b.color + wc * c.color;
				m_Pixels[pixel * 4] = ToByte(color.r);
				m_Pixels[pixel * 4 + 1] = ToByte(color.g);
				m_Pixels[pixel * 4 + 2] = ToByte(color.b);
				m_Pixels[pixel * 4 + 3] = 255;
			}
		}
	}
}

bool VansAnimationPreviewRenderer::UploadRenderThread(std::string& error)
{
	VANS_ASSERT_RENDER_THREAD();
	if (!m_Device || m_Pixels.empty())
	{
		error = "Animation preview has no render target or pixels";
		return false;
	}
	if (!m_Device->SetDeviceImageData(
		m_ColorImage,
		m_Device->GetImmediateGraphicsCommandBuffer(),
		m_Pixels.data(),
		0,
		static_cast<int>(m_Pixels.size()),
		{ 0, 0, 0 },
		{ PreviewWidth, PreviewHeight, 1 },
		0,
		0,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
	{
		error = "Failed to upload isolated animation preview pixels";
		return false;
	}
	return true;
}
}
