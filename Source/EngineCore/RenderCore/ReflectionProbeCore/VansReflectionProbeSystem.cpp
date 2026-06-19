#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansReflectionProbeSystem.h"
#include "../VansScene.h"
#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansTexture.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../VulkanCore/VansVKMemoryManager.h"
#include "../../Configration/VansConfigration.h"
#include "../../Util/VansLog.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <queue>
#include <limits>
#include <../../GLM/gtc/matrix_transform.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../../../External/tinygltf/stb_image_write.h"

namespace fs = std::filesystem;

namespace
{
	constexpr uint32_t kReflectionProbeCacheVersion = 5u;
	constexpr const char* kReflectionProbeCacheVersionFile = "cache.version";
}

namespace VansGraphics
{
	namespace
	{
		struct CaptureCameraData
		{
			glm::mat4 viewProjection;
			glm::mat4 inverseViewProjection;
			glm::vec4 position;
			glm::vec4 giVolumeMin;
			glm::vec4 giVolumeSizeAndBias;
		};

		struct CaptureDrawData
		{
			glm::mat4 model;
			glm::vec4 albedo;
			glm::vec4 emissive;
			glm::vec4 params;
		};

		float HalfToFloat(uint16_t value)
		{
			const uint32_t sign = uint32_t(value & 0x8000u) << 16u;
			int32_t exponent = int32_t((value >> 10u) & 0x1fu);
			uint32_t mantissa = value & 0x03ffu;
			uint32_t bits = 0;
			if (exponent == 0)
			{
				if (mantissa == 0) bits = sign;
				else
				{
					exponent = 1;
					while ((mantissa & 0x0400u) == 0u) { mantissa <<= 1u; --exponent; }
					mantissa &= 0x03ffu;
					bits = sign | (uint32_t(exponent + 112) << 23u) | (mantissa << 13u);
				}
			}
			else if (exponent == 31) bits = sign | 0x7f800000u | (mantissa << 13u);
			else bits = sign | (uint32_t(exponent + 112) << 23u) | (mantissa << 13u);
			float result = 0.0f; std::memcpy(&result, &bits, sizeof(result)); return result;
		}

		glm::vec3 ReadVec3(const nlohmann::json& value, const glm::vec3& fallback)
		{
			if (!value.is_array() || value.size() < 3) return fallback;
			return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
		}

		nlohmann::json WriteVec3(const glm::vec3& value)
		{
			return nlohmann::json::array({ value.x, value.y, value.z });
		}

		uint32_t NormalizeResolution(uint32_t value)
		{
			value = std::clamp(value, 32u, 512u);
			uint32_t result = 1;
			while (result < value) result <<= 1;
			return result;
		}
	}

	VansReflectionProbeSystem::~VansReflectionProbeSystem()
	{
		if (m_Device != VK_NULL_HANDLE) Clear(m_Device);
	}

	VansReflectionProbeDesc VansReflectionProbeSystem::ParseProbe(const nlohmann::json& value)
	{
		VansReflectionProbeDesc p;
		p.name = value.value("name", p.name);
		const std::string type = value.value("type", "baked");
		p.type = type == "realtime" ? ReflectionProbeType::Realtime :
			(type == "sky" ? ReflectionProbeType::Sky : ReflectionProbeType::Baked);
		p.shape = value.value("shape", "box") == "sphere" ? ReflectionProbeShape::Sphere : ReflectionProbeShape::Box;
		const std::string refresh = value.value("refreshMode", "on_load");
		p.refreshMode = refresh == "every_frame" ? ReflectionProbeRefreshMode::EveryFrame :
			(refresh == "time_sliced" ? ReflectionProbeRefreshMode::TimeSliced :
			(refresh == "on_demand" ? ReflectionProbeRefreshMode::OnDemand : ReflectionProbeRefreshMode::OnLoad));
		if (value.contains("position")) p.position = ReadVec3(value["position"], p.position);
		p.capturePosition = value.contains("capturePosition") ? ReadVec3(value["capturePosition"], p.position) : p.position;
		if (value.contains("boxMin")) p.boxMin = ReadVec3(value["boxMin"], p.boxMin);
		if (value.contains("boxMax")) p.boxMax = ReadVec3(value["boxMax"], p.boxMax);
		p.radius = std::max(value.value("radius", p.radius), 0.01f);
		p.blendDistance = std::max(value.value("blendDistance", p.blendDistance), 0.001f);
		p.priority = value.value("priority", p.priority);
		p.intensity = std::max(value.value("intensity", p.intensity), 0.0f);
		p.specularIntensity = std::max(value.value("specularIntensity", p.specularIntensity), 0.0f);
		p.nearPlane = std::max(value.value("nearPlane", p.nearPlane), 0.001f);
		p.farPlane = std::max(value.value("farPlane", p.farPlane), p.nearPlane + 0.01f);
		p.resolution = NormalizeResolution(value.value("resolution", p.resolution));
		p.cullingMask = value.value("cullingMask", p.cullingMask);
		p.regionId = value.value("regionId", p.regionId);
		p.realtimeFacesPerFrame = std::clamp(value.value("facesPerFrame", p.realtimeFacesPerFrame), 1u, 6u);
		p.enabled = value.value("enabled", p.enabled);
		p.boxProjection = value.value("boxProjection", p.boxProjection);
		p.autoGenerated = value.value("autoGenerated", p.autoGenerated);
		p.portal = value.value("portal", p.portal);
		p.cachePath = value.value("cachePath", std::string());
		return p;
	}

	nlohmann::json VansReflectionProbeSystem::SerializeProbe(const VansReflectionProbeDesc& p)
	{
		const char* type = p.type == ReflectionProbeType::Realtime ? "realtime" : (p.type == ReflectionProbeType::Sky ? "sky" : "baked");
		const char* refresh = p.refreshMode == ReflectionProbeRefreshMode::EveryFrame ? "every_frame" :
			(p.refreshMode == ReflectionProbeRefreshMode::TimeSliced ? "time_sliced" :
			(p.refreshMode == ReflectionProbeRefreshMode::OnDemand ? "on_demand" : "on_load"));
		return {
			{ "name", p.name }, { "type", type }, { "shape", p.shape == ReflectionProbeShape::Sphere ? "sphere" : "box" },
			{ "refreshMode", refresh }, { "position", WriteVec3(p.position) }, { "capturePosition", WriteVec3(p.capturePosition) },
			{ "boxMin", WriteVec3(p.boxMin) }, { "boxMax", WriteVec3(p.boxMax) }, { "radius", p.radius },
			{ "blendDistance", p.blendDistance }, { "priority", p.priority }, { "intensity", p.intensity },
			{ "specularIntensity", p.specularIntensity },
			{ "nearPlane", p.nearPlane }, { "farPlane", p.farPlane }, { "resolution", p.resolution },
			{ "cullingMask", p.cullingMask }, { "regionId", p.regionId }, { "facesPerFrame", p.realtimeFacesPerFrame },
			{ "enabled", p.enabled }, { "boxProjection", p.boxProjection }, { "autoGenerated", p.autoGenerated },
			{ "portal", p.portal }, { "cachePath", p.cachePath }
		};
	}

	void VansReflectionProbeSystem::LoadFromSceneJson(const nlohmann::json& root, const std::string& scenePath)
	{
		m_Probes.clear(); m_BakeResults.clear(); m_BakeQueue.clear();
		m_ScenePath = scenePath;
		const auto& block = root.contains("reflectionProbes") ? root["reflectionProbes"] : nlohmann::json();
		if (block.is_object())
		{
			const auto& lighting = block.value("lighting", nlohmann::json::object());
			m_LightingSettings.maxBlendCount = std::clamp(lighting.value("maxBlendCount", m_LightingSettings.maxBlendCount), 1u, 4u);
			m_LightingSettings.ssrRoughnessFadeStart = lighting.value("ssrRoughnessFadeStart", m_LightingSettings.ssrRoughnessFadeStart);
			m_LightingSettings.ssrRoughnessFadeEnd = lighting.value("ssrRoughnessFadeEnd", m_LightingSettings.ssrRoughnessFadeEnd);
			m_LightingSettings.skyIntensity = std::max(lighting.value("skyIntensity", m_LightingSettings.skyIntensity), 0.0f);
			const auto& settings = block.value("placement", nlohmann::json::object());
			m_PlacementSettings.enabled = settings.value("enabled", m_PlacementSettings.enabled);
			m_PlacementSettings.geometryOnly = settings.value("geometryOnly", true);
			if (settings.contains("volumeMin")) m_PlacementSettings.volumeMin = ReadVec3(settings["volumeMin"], m_PlacementSettings.volumeMin);
			if (settings.contains("volumeMax")) m_PlacementSettings.volumeMax = ReadVec3(settings["volumeMax"], m_PlacementSettings.volumeMax);
			m_PlacementSettings.cellSize = settings.value("cellSize", m_PlacementSettings.cellSize);
			m_PlacementSettings.indoorSpacing = settings.value("indoorSpacing", m_PlacementSettings.indoorSpacing);
			m_PlacementSettings.corridorSpacing = settings.value("corridorSpacing", m_PlacementSettings.corridorSpacing);
			m_PlacementSettings.outdoorSpacing = settings.value("outdoorSpacing", m_PlacementSettings.outdoorSpacing);
			m_PlacementSettings.solidThreshold = settings.value("solidThreshold", m_PlacementSettings.solidThreshold);
			m_PlacementSettings.refinementThreshold = settings.value("refinementThreshold", m_PlacementSettings.refinementThreshold);
			m_PlacementSettings.maxProbeCount = settings.value("maxProbeCount", m_PlacementSettings.maxProbeCount);
			m_PlacementSettings.uniformSpacing = std::max(settings.value("uniformSpacing", m_PlacementSettings.uniformSpacing), 0.5f);
			m_PlacementSettings.uniformBoxSizeScale = std::clamp(
				settings.value("uniformBoxSizeScale", m_PlacementSettings.uniformBoxSizeScale), 0.05f, 1.0f);
			m_PlacementSettings.uniformProbeResolution = NormalizeResolution(
				settings.value("uniformProbeResolution", m_PlacementSettings.uniformProbeResolution));
			if (block.contains("probes") && block["probes"].is_array())
				for (const auto& value : block["probes"]) m_Probes.push_back(ParseProbe(value));
		}
		EnsureDefaults();
		m_BakeResults.resize(m_Probes.size());
		for (size_t i = 0; i < m_Probes.size(); ++i)
		{
			m_BakeResults[i].arrayLayer = (uint32_t)i;
			m_BakeResults[i].cachePath = m_Probes[i].cachePath;
			m_BakeResults[i].dirty = m_Probes[i].type != ReflectionProbeType::Sky;
		}
	}

	void VansReflectionProbeSystem::SaveToSceneJson(nlohmann::json& root) const
	{
		nlohmann::json probes = nlohmann::json::array();
		for (const auto& probe : m_Probes) probes.push_back(SerializeProbe(probe));
		root["reflectionProbes"] = {
			{ "lighting", {
				{ "maxBlendCount", m_LightingSettings.maxBlendCount },
				{ "ssrRoughnessFadeStart", m_LightingSettings.ssrRoughnessFadeStart },
				{ "ssrRoughnessFadeEnd", m_LightingSettings.ssrRoughnessFadeEnd },
				{ "skyIntensity", m_LightingSettings.skyIntensity }
			} },
			{ "placement", {
				{ "enabled", m_PlacementSettings.enabled }, { "geometryOnly", true },
				{ "volumeMin", WriteVec3(m_PlacementSettings.volumeMin) }, { "volumeMax", WriteVec3(m_PlacementSettings.volumeMax) },
				{ "cellSize", m_PlacementSettings.cellSize }, { "indoorSpacing", m_PlacementSettings.indoorSpacing },
				{ "corridorSpacing", m_PlacementSettings.corridorSpacing }, { "outdoorSpacing", m_PlacementSettings.outdoorSpacing },
				{ "solidThreshold", m_PlacementSettings.solidThreshold }, { "refinementThreshold", m_PlacementSettings.refinementThreshold },
				{ "maxProbeCount", m_PlacementSettings.maxProbeCount },
				{ "uniformSpacing", m_PlacementSettings.uniformSpacing },
				{ "uniformBoxSizeScale", m_PlacementSettings.uniformBoxSizeScale },
				{ "uniformProbeResolution", m_PlacementSettings.uniformProbeResolution }
			} },
			{ "probes", probes }
		};
	}

	bool VansReflectionProbeSystem::SaveConfiguration() const
	{
		if (m_ScenePath.empty()) return false;
		std::ifstream input(m_ScenePath);
		if (!input.is_open()) return false;
		nlohmann::json root;
		try { input >> root; } catch (...) { return false; }
		input.close();
		SaveToSceneJson(root);
		std::ofstream output(m_ScenePath, std::ios::trunc);
		if (!output.is_open()) return false;
		output << root.dump(4) << '\n';
		return output.good();
	}

	void VansReflectionProbeSystem::EnsureDefaults()
	{
		m_Probes.erase(std::remove_if(m_Probes.begin(), m_Probes.end(), [](const auto& p) { return p.type == ReflectionProbeType::Sky; }), m_Probes.end());
		VansReflectionProbeDesc sky;
		sky.name = "Sky Fallback"; sky.type = ReflectionProbeType::Sky; sky.shape = ReflectionProbeShape::Sphere;
		sky.radius = 0.0f; sky.priority = -100000.0f; sky.boxProjection = false; sky.enabled = true;
		m_Probes.push_back(sky);
	}

	void VansReflectionProbeSystem::Clear(VkDevice device)
	{
		DestroyCaptureResources();
		auto* descriptors = VansVKDescriptorManager::GetInstance();
		if (!m_PrefilterSets.empty()) descriptors->DestroyDescriptorSet(m_PrefilterSets);
		if (m_PrefilterLayout != VK_NULL_HANDLE) descriptors->DestroyDescriptorSetLayout(m_PrefilterLayout);
		for (VkImageView view : m_PrefilterMipViews) if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr);
		m_PrefilterSets.clear(); m_PrefilterMipViews.clear(); m_PrefilterLayout = VK_NULL_HANDLE;
		delete m_PrefilterShader; m_PrefilterShader = nullptr;
		m_MetadataBuffer.DestroyVulkanBuffer(device);
		delete m_SpecularArray; m_SpecularArray = nullptr;
		m_Device = VK_NULL_HANDLE;
		m_GPUProbes.clear(); m_BakeQueue.clear();
		m_ActiveBakeIndex = size_t(-1); m_ActiveBakeFace = 0;
		m_GIWarmupFramesRemaining = 0; m_LastGIWarmupFrame = 0xffffffffu;
	}

	void VansReflectionProbeSystem::ClearAutoProbes()
	{
		for (size_t i = m_Probes.size(); i-- > 0;)
			if (m_Probes[i].autoGenerated) { m_Probes.erase(m_Probes.begin() + i); m_BakeResults.erase(m_BakeResults.begin() + i); }
		EnsureDefaults();
	}

	void VansReflectionProbeSystem::ConvertToManual(size_t index)
	{
		if (index < m_Probes.size()) m_Probes[index].autoGenerated = false;
	}

	void VansReflectionProbeSystem::GenerateAutoProbes(const VansScene& scene, bool replaceExisting)
	{
		if (replaceExisting) ClearAutoProbes();
		if (!m_PlacementSettings.enabled || m_Probes.size() > m_PlacementSettings.maxProbeCount) return;

		const float spacing = std::max(m_PlacementSettings.uniformSpacing, 0.5f);
		const glm::vec3 size = glm::max(m_PlacementSettings.volumeMax - m_PlacementSettings.volumeMin, glm::vec3(0.5f));
		const glm::ivec3 gridCount = glm::clamp(
			glm::ivec3(glm::ceil(size / spacing)), glm::ivec3(1), glm::ivec3(32));
		const glm::vec3 cellSize = size / glm::vec3(gridCount);
		const glm::vec3 halfCell = cellSize * 0.5f;
		const glm::vec3 boxHalfExtent = halfCell * m_PlacementSettings.uniformBoxSizeScale;

		for (int z = 0; z < gridCount.z && m_Probes.size() <= m_PlacementSettings.maxProbeCount; ++z)
		for (int y = 0; y < gridCount.y && m_Probes.size() <= m_PlacementSettings.maxProbeCount; ++y)
		for (int x = 0; x < gridCount.x && m_Probes.size() <= m_PlacementSettings.maxProbeCount; ++x)
		{
			VansReflectionProbeDesc probe;
			probe.name = "Auto Grid " + std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(z);
			probe.autoGenerated = true;
			probe.shape = ReflectionProbeShape::Box;
			probe.boxProjection = false;
			const glm::vec3 cellOrigin = m_PlacementSettings.volumeMin + glm::vec3(x, y, z) * cellSize;
			probe.position = probe.capturePosition = cellOrigin + halfCell;
			probe.boxMin = probe.position - boxHalfExtent;
			probe.boxMax = probe.position + boxHalfExtent;
			// When authored boxes are smaller than their grid cells, extend only
			// the fade region far enough to meet neighboring cells without holes.
			probe.blendDistance = std::max(glm::length(glm::max(halfCell - boxHalfExtent, glm::vec3(0.0f))),
				std::max(std::min(boxHalfExtent.x, std::min(boxHalfExtent.y, boxHalfExtent.z)) * 0.25f, 0.25f));
			probe.priority = 1.0f;
			probe.resolution = m_PlacementSettings.uniformProbeResolution;
			m_Probes.insert(m_Probes.end() - 1, probe);
			m_BakeResults.insert(m_BakeResults.end() - 1, ReflectionProbeBakeResult{});
		}

		(void)scene; // unused in uniform mode; kept for signature compatibility
	}

	void VansReflectionProbeSystem::BuildPlacementGrid(const VansScene& scene)
	{
		auto& grid = m_PlacementGrid;
		grid.origin = m_PlacementSettings.volumeMin;
		grid.extent = glm::max(m_PlacementSettings.volumeMax - m_PlacementSettings.volumeMin, glm::vec3(0.25f));
		grid.cellSize = std::max(m_PlacementSettings.cellSize, 0.25f);
		grid.dimensions = glm::clamp(glm::uvec3(glm::ceil(grid.extent / grid.cellSize)), glm::uvec3(1u), glm::uvec3(64u));
		grid.extent = glm::vec3(grid.dimensions) * grid.cellSize;
		const uint32_t cellCount = grid.dimensions.x * grid.dimensions.y * grid.dimensions.z;
		grid.cells.assign(cellCount, ProbePlacementCell{});
		auto index = [&](uint32_t x, uint32_t y, uint32_t z) { return x + grid.dimensions.x * (y + grid.dimensions.y * z); };
		auto clampCell = [&](const glm::vec3& p)
		{
			return glm::clamp(glm::ivec3(glm::floor((p - grid.origin) / grid.cellSize)), glm::ivec3(0), glm::ivec3(grid.dimensions) - 1);
		};

		std::vector<VansRenderNode*> placementNodes = scene.m_OpaqueRenderNodes;
		for (auto* node : placementNodes)
		{
			if (!node || !node->m_Mesh || !node->m_Mesh->HasCPUPlacementData()) continue;
			const auto& positions = node->m_Mesh->GetMeshRawPositionData();
			const auto& triangles = node->m_Mesh->GetMeshTriangleIndex();
			constexpr size_t cpuVertexStride = 8;
			const size_t vertexCount = positions.size() / cpuVertexStride;
			if (vertexCount < 3) continue;
			const glm::mat4 model = node->m_ModelData.ModelMatrix;
			const size_t triangleCount = triangles.empty() ? vertexCount / 3 : triangles.size() / 3;
			for (size_t t = 0; t < triangleCount; ++t)
			{
				glm::vec3 v[3]; bool valid = true;
				for (int c = 0; c < 3; ++c)
				{
					const int indexedVertex = triangles.empty() ? int(t * 3 + c) : triangles[t * 3 + c];
					if (indexedVertex < 0 || size_t(indexedVertex) >= vertexCount) { valid = false; break; }
					const size_t offset = size_t(indexedVertex) * cpuVertexStride;
					v[c] = glm::vec3(model * glm::vec4(positions[offset], positions[offset + 1], positions[offset + 2], 1.0f));
				}
				if (!valid) continue;
				const glm::vec3 crossValue = glm::cross(v[1] - v[0], v[2] - v[0]);
				const float area = glm::length(crossValue) * 0.5f;
				if (area <= 1e-8f) continue;
				const glm::vec3 normal = glm::normalize(crossValue);
				const glm::ivec3 first = clampCell(glm::min(v[0], glm::min(v[1], v[2])));
				const glm::ivec3 last = clampCell(glm::max(v[0], glm::max(v[1], v[2])));
				const int overlapCount = std::max((last.x-first.x+1)*(last.y-first.y+1)*(last.z-first.z+1), 1);
				for (int z = first.z; z <= last.z; ++z) for (int y = first.y; y <= last.y; ++y) for (int x = first.x; x <= last.x; ++x)
				{
					auto& cell = grid.cells[index(x,y,z)];
					cell.surfaceArea += area / float(overlapCount);
					cell.solidFraction = std::min(1.0f, cell.solidFraction + area / (grid.cellSize * grid.cellSize * float(overlapCount)));
					cell.normalMean += normal * area;
				}
			}
		}

		const int directions[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
		for (uint32_t z=0;z<grid.dimensions.z;++z) for(uint32_t y=0;y<grid.dimensions.y;++y) for(uint32_t x=0;x<grid.dimensions.x;++x)
		{
			auto& cell = grid.cells[index(x,y,z)];
			if (cell.solidFraction >= m_PlacementSettings.solidThreshold || cell.surfaceArea > grid.cellSize * grid.cellSize * 0.05f)
				cell.cellClass = ProbeCellClass::Solid;
			else cell.cellClass = ProbeCellClass::Empty;
			if (glm::length(cell.normalMean) > 1e-5f) cell.normalMean = glm::normalize(cell.normalMean);
		}
		for (uint32_t z=0;z<grid.dimensions.z;++z) for(uint32_t y=0;y<grid.dimensions.y;++y) for(uint32_t x=0;x<grid.dimensions.x;++x)
		{
			auto& cell = grid.cells[index(x,y,z)]; if (cell.cellClass == ProbeCellClass::Solid) continue;
			uint32_t solidNeighbours = 0;
			for (const auto& d : directions)
			{
				int nx=int(x)+d[0],ny=int(y)+d[1],nz=int(z)+d[2];
				if(nx>=0&&ny>=0&&nz>=0&&nx<(int)grid.dimensions.x&&ny<(int)grid.dimensions.y&&nz<(int)grid.dimensions.z&&grid.cells[index(nx,ny,nz)].cellClass==ProbeCellClass::Solid) ++solidNeighbours;
			}
			cell.occlusion = solidNeighbours / 6.0f;
			if (solidNeighbours) cell.cellClass = ProbeCellClass::Boundary;
		}

		std::queue<uint32_t> open;
		auto enqueueExterior = [&](uint32_t x,uint32_t y,uint32_t z)
		{
			auto& cell=grid.cells[index(x,y,z)];
			if(cell.cellClass!=ProbeCellClass::Solid&&cell.cellClass!=ProbeCellClass::Exterior){cell.cellClass=ProbeCellClass::Exterior;open.push(index(x,y,z));}
		};
		for(uint32_t z=0;z<grid.dimensions.z;++z)for(uint32_t y=0;y<grid.dimensions.y;++y){enqueueExterior(0,y,z);enqueueExterior(grid.dimensions.x-1,y,z);}
		for(uint32_t z=0;z<grid.dimensions.z;++z)for(uint32_t x=0;x<grid.dimensions.x;++x){enqueueExterior(x,0,z);enqueueExterior(x,grid.dimensions.y-1,z);}
		for(uint32_t y=0;y<grid.dimensions.y;++y)for(uint32_t x=0;x<grid.dimensions.x;++x){enqueueExterior(x,y,0);enqueueExterior(x,y,grid.dimensions.z-1);}
		while(!open.empty())
		{
			uint32_t value=open.front();open.pop(); uint32_t x=value%grid.dimensions.x;uint32_t yz=value/grid.dimensions.x;uint32_t y=yz%grid.dimensions.y;uint32_t z=yz/grid.dimensions.y;
			for(const auto& d:directions){int nx=int(x)+d[0],ny=int(y)+d[1],nz=int(z)+d[2];if(nx>=0&&ny>=0&&nz>=0&&nx<(int)grid.dimensions.x&&ny<(int)grid.dimensions.y&&nz<(int)grid.dimensions.z)enqueueExterior(nx,ny,nz);}
		}
	}

	void VansReflectionProbeSystem::BuildRegions()
	{
		m_Regions.clear(); auto& grid=m_PlacementGrid; if(grid.cells.empty())return;
		auto index=[&](uint32_t x,uint32_t y,uint32_t z){return x+grid.dimensions.x*(y+grid.dimensions.y*z);};
		GeometryRegion exterior; exterior.id=0; exterior.type=ProbeRegionType::Exterior; exterior.boundsMin=grid.origin+grid.extent; exterior.boundsMax=grid.origin;
		for(uint32_t i=0;i<grid.cells.size();++i)if(grid.cells[i].cellClass==ProbeCellClass::Exterior){grid.cells[i].regionId=0;exterior.cellIndices.push_back(i);uint32_t x=i%grid.dimensions.x;uint32_t yz=i/grid.dimensions.x;uint32_t y=yz%grid.dimensions.y;uint32_t z=yz/grid.dimensions.y;glm::vec3 c=grid.origin+(glm::vec3(x,y,z)+0.5f)*grid.cellSize;exterior.boundsMin=glm::min(exterior.boundsMin,c-glm::vec3(grid.cellSize*0.5f));exterior.boundsMax=glm::max(exterior.boundsMax,c+glm::vec3(grid.cellSize*0.5f));exterior.centroid+=c;exterior.meanOcclusion+=grid.cells[i].occlusion;}
		if(!exterior.cellIndices.empty()){exterior.centroid/=float(exterior.cellIndices.size());exterior.meanOcclusion/=float(exterior.cellIndices.size());exterior.volume=float(exterior.cellIndices.size())*grid.cellSize*grid.cellSize*grid.cellSize;m_Regions.push_back(exterior);}
		const int directions[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
		uint32_t nextId=1;
		for(uint32_t seed=0;seed<grid.cells.size();++seed)
		{
			if(grid.cells[seed].cellClass==ProbeCellClass::Solid||grid.cells[seed].cellClass==ProbeCellClass::Exterior||grid.cells[seed].regionId!=0xffffffffu)continue;
			GeometryRegion region;region.id=nextId++;region.boundsMin=grid.origin+grid.extent;region.boundsMax=grid.origin;std::queue<uint32_t> queue;queue.push(seed);grid.cells[seed].regionId=region.id;
			while(!queue.empty()){uint32_t value=queue.front();queue.pop();region.cellIndices.push_back(value);uint32_t x=value%grid.dimensions.x;uint32_t yz=value/grid.dimensions.x;uint32_t y=yz%grid.dimensions.y;uint32_t z=yz/grid.dimensions.y;glm::vec3 c=grid.origin+(glm::vec3(x,y,z)+0.5f)*grid.cellSize;region.boundsMin=glm::min(region.boundsMin,c-glm::vec3(grid.cellSize*0.5f));region.boundsMax=glm::max(region.boundsMax,c+glm::vec3(grid.cellSize*0.5f));region.centroid+=c;region.meanOcclusion+=grid.cells[value].occlusion;region.surfaceArea+=grid.cells[value].surfaceArea;for(const auto& d:directions){int nx=int(x)+d[0],ny=int(y)+d[1],nz=int(z)+d[2];if(nx<0||ny<0||nz<0||nx>=(int)grid.dimensions.x||ny>=(int)grid.dimensions.y||nz>=(int)grid.dimensions.z)continue;uint32_t n=index(nx,ny,nz);auto& cell=grid.cells[n];if(cell.cellClass!=ProbeCellClass::Solid&&cell.cellClass!=ProbeCellClass::Exterior&&cell.regionId==0xffffffffu){cell.regionId=region.id;queue.push(n);}}}
			region.centroid/=float(region.cellIndices.size());region.meanOcclusion/=float(region.cellIndices.size());region.volume=float(region.cellIndices.size())*grid.cellSize*grid.cellSize*grid.cellSize;glm::vec3 size=region.boundsMax-region.boundsMin;float minAxis=std::max(std::min(size.x,std::min(size.y,size.z)),grid.cellSize);float maxAxis=std::max(size.x,std::max(size.y,size.z));region.compactness=maxAxis/minAxis;region.type=region.compactness>3.0f?ProbeRegionType::Corridor:ProbeRegionType::Interior;m_Regions.push_back(std::move(region));
		}
	}

	void VansReflectionProbeSystem::BuildAutoProbesFromRegions()
	{
		const float cell=m_PlacementGrid.cellSize;
		auto insertProbe=[&](VansReflectionProbeDesc probe)
		{
			if(m_Probes.size()>m_PlacementSettings.maxProbeCount)return false;probe.autoGenerated=true;probe.resolution=128;m_Probes.insert(m_Probes.end()-1,probe);m_BakeResults.insert(m_BakeResults.end()-1,ReflectionProbeBakeResult{});return true;
		};
		for(const auto& region:m_Regions)
		{
			if(region.type==ProbeRegionType::Exterior)continue;
			glm::vec3 size=region.boundsMax-region.boundsMin;int axis=size.y>size.x?1:0;if(size.z>size[axis])axis=2;float spacing=region.type==ProbeRegionType::Corridor?m_PlacementSettings.corridorSpacing:m_PlacementSettings.indoorSpacing;int count=std::max(1,(int)std::ceil(size[axis]/std::max(spacing,cell)));
			for(int part=0;part<count;++part){VansReflectionProbeDesc p;p.name="Auto Region "+std::to_string(region.id)+" #"+std::to_string(part);p.shape=ReflectionProbeShape::Box;p.regionId=region.id;p.position=p.capturePosition=region.centroid;p.boxMin=region.boundsMin+glm::vec3(cell*0.25f);p.boxMax=region.boundsMax-glm::vec3(cell*0.25f);float start=region.boundsMin[axis]+size[axis]*float(part)/count;float end=region.boundsMin[axis]+size[axis]*float(part+1)/count;p.boxMin[axis]=start;p.boxMax[axis]=end;p.position[axis]=p.capturePosition[axis]=(start+end)*0.5f;p.blendDistance=std::max(0.25f,std::min(cell,size[axis]/float(count)*0.25f));p.portal=region.type==ProbeRegionType::Corridor&&region.compactness>5.0f;p.priority=p.portal?1.5f:2.0f;if(p.portal)p.name="Auto Portal "+std::to_string(region.id)+" #"+std::to_string(part);if(!insertProbe(p))return;}
		}
		const uint32_t stride=std::max(1u,(uint32_t)std::round(m_PlacementSettings.outdoorSpacing/cell));auto& grid=m_PlacementGrid;
		const float spacing=float(stride)*cell;const glm::vec3 halfExtent(spacing*0.75f);
		for(uint32_t z=stride/2;z<grid.dimensions.z;z+=stride)for(uint32_t y=stride/2;y<grid.dimensions.y;y+=stride)for(uint32_t x=stride/2;x<grid.dimensions.x;x+=stride){uint32_t i=x+grid.dimensions.x*(y+grid.dimensions.y*z);if(grid.cells[i].cellClass!=ProbeCellClass::Exterior)continue;VansReflectionProbeDesc p;p.name="Auto Exterior "+std::to_string(x)+"_"+std::to_string(y)+"_"+std::to_string(z);p.shape=ReflectionProbeShape::Box;p.regionId=0;p.position=p.capturePosition=grid.origin+(glm::vec3(x,y,z)+0.5f)*cell;p.boxMin=glm::max(grid.origin,p.position-halfExtent);p.boxMax=glm::min(grid.origin+grid.extent,p.position+halfExtent);p.blendDistance=std::max(cell*0.5f,spacing*0.25f);p.boxProjection=false;p.priority=1.0f;if(!insertProbe(p))return;}
	}

	void VansReflectionProbeSystem::EvaluateCoverage()
	{
		m_GeometryErrors.assign(m_Regions.size(),ProbeGeometryError{});auto& grid=m_PlacementGrid;
		for(size_t r=0;r<m_Regions.size();++r){const auto& region=m_Regions[r];if(region.cellIndices.empty())continue;uint32_t uncovered=0;float maxDistance=0.0f;for(uint32_t cellIndex:region.cellIndices){uint32_t x=cellIndex%grid.dimensions.x;uint32_t yz=cellIndex/grid.dimensions.x;uint32_t y=yz%grid.dimensions.y;uint32_t z=yz/grid.dimensions.y;glm::vec3 c=grid.origin+(glm::vec3(x,y,z)+0.5f)*grid.cellSize;float best=std::numeric_limits<float>::max();bool covered=false;for(const auto& p:m_Probes){if(p.type==ReflectionProbeType::Sky||!p.enabled)continue;best=std::min(best,glm::distance(c,p.capturePosition));if(p.shape==ReflectionProbeShape::Box)covered|=glm::all(glm::greaterThanEqual(c,p.boxMin))&&glm::all(glm::lessThanEqual(c,p.boxMax));else covered|=glm::distance(c,p.position)<=p.radius;}if(!covered)++uncovered;if(best<std::numeric_limits<float>::max())maxDistance=std::max(maxDistance,best);}auto& error=m_GeometryErrors[r];error.uncoveredCellRatio=float(uncovered)/region.cellIndices.size();error.maxDistanceToProbe=maxDistance;glm::vec3 size=region.boundsMax-region.boundsMin;error.boundsAspectError=region.compactness;}
	}

	std::vector<std::string> VansReflectionProbeSystem::ValidatePlacement() const
	{
		std::vector<std::string> errors;
		for (size_t i = 0; i < m_Probes.size(); ++i)
		{
			const auto& p = m_Probes[i];
			if (p.type == ReflectionProbeType::Sky) continue;
			if (p.shape == ReflectionProbeShape::Box && glm::any(glm::lessThanEqual(p.boxMax, p.boxMin))) errors.push_back(p.name + ": invalid box bounds");
			if (p.shape == ReflectionProbeShape::Sphere && p.radius <= 0.0f) errors.push_back(p.name + ": radius must be positive");
			if (p.blendDistance <= 0.0f) errors.push_back(p.name + ": blend distance must be positive");
			if (p.capturePosition.x < p.boxMin.x || p.capturePosition.y < p.boxMin.y || p.capturePosition.z < p.boxMin.z ||
				p.capturePosition.x > p.boxMax.x || p.capturePosition.y > p.boxMax.y || p.capturePosition.z > p.boxMax.z)
				if (p.shape == ReflectionProbeShape::Box) errors.push_back(p.name + ": capture position is outside influence box");
			if (!m_PlacementGrid.cells.empty())
			{
				glm::ivec3 cell = glm::ivec3(glm::floor((p.capturePosition - m_PlacementGrid.origin) / m_PlacementGrid.cellSize));
				if (glm::all(glm::greaterThanEqual(cell, glm::ivec3(0))) && glm::all(glm::lessThan(cell, glm::ivec3(m_PlacementGrid.dimensions))))
				{
					uint32_t index = cell.x + m_PlacementGrid.dimensions.x * (cell.y + m_PlacementGrid.dimensions.y * cell.z);
					if (m_PlacementGrid.cells[index].cellClass == ProbeCellClass::Solid) errors.push_back(p.name + ": capture position intersects static geometry");
					uint32_t cellRegion = m_PlacementGrid.cells[index].regionId;
					if (!p.portal && p.regionId != 0xffffffffu && cellRegion != 0xffffffffu && p.regionId != cellRegion) errors.push_back(p.name + ": capture position is assigned to a different region");
				}
			}
		}
		for (size_t i = 0; i < m_GeometryErrors.size(); ++i)
			if (m_GeometryErrors[i].uncoveredCellRatio > m_PlacementSettings.refinementThreshold)
				errors.push_back("Region " + std::to_string(i < m_Regions.size() ? m_Regions[i].id : (uint32_t)i) + ": uncovered cell ratio " + std::to_string(m_GeometryErrors[i].uncoveredCellRatio));
		return errors;
	}

	bool VansReflectionProbeSystem::LoadCachedProbe(VansVKCommandBuffer& commandBuffer, size_t probeIndex)
	{
		if (probeIndex >= m_Probes.size() || !m_SpecularArray) return false;
		auto& p = m_Probes[probeIndex];
		if (p.type == ReflectionProbeType::Sky) return false;
		fs::path base = p.cachePath;
		if (base.empty())
		{
			base = fs::path(m_ScenePath).parent_path() / "ReflectionProbes" / p.name;
			p.cachePath = base.string();
		}
		std::ifstream versionFile(base / kReflectionProbeCacheVersionFile);
		uint32_t cacheVersion = 0;
		if (!(versionFile >> cacheVersion) || cacheVersion != kReflectionProbeCacheVersion) return false;
		static const char* faces[] = { "px", "nx", "py", "ny", "pz", "nz" };
		for (uint32_t face = 0; face < 6; ++face)
		{
			fs::path path = base / (std::string(faces[face]) + ".hdr");
			if (!fs::exists(path)) path.replace_extension(".png");
			if (!fs::exists(path) || !m_SpecularArray->LoadHDRTextureLayer(commandBuffer, path.string(), int(probeIndex * 6 + face))) return false;
		}
		return true;
	}

	void VansReflectionProbeSystem::CreateGPUResources(VansVKDevice& device, VansVKCommandBuffer& commandBuffer)
	{
		if (m_Device != VK_NULL_HANDLE) Clear(m_Device);
		m_Device = device.GetLogicDevice();
		m_ArrayResolution = 32;
		for (const auto& p : m_Probes) if (p.type != ReflectionProbeType::Sky) m_ArrayResolution = std::max(m_ArrayResolution, NormalizeResolution(p.resolution));
		m_MipCount = 1u + (uint32_t)std::floor(std::log2(float(m_ArrayResolution)));
		const uint32_t cubeCount = std::max(1u, (uint32_t)m_Probes.size());
		m_SpecularArray = new VansTexture();
		m_SpecularArray->InitCubeTextureArray(commandBuffer, m_ArrayResolution, m_ArrayResolution, cubeCount, 4, true, HDR_PRES_16);
		m_BakeResults.resize(m_Probes.size());
		for (size_t i = 0; i < m_Probes.size(); ++i)
		{
			auto& result = m_BakeResults[i]; result.arrayLayer = (uint32_t)i; result.mipCount = m_MipCount;
			result.valid = LoadCachedProbe(commandBuffer, i); result.dirty = !result.valid && m_Probes[i].type != ReflectionProbeType::Sky;
			result.cachePath = m_Probes[i].cachePath; result.status = result.valid ? "Loaded baked cache" : (m_Probes[i].type == ReflectionProbeType::Sky ? "Global sky fallback" : "Bake required");
			if (result.dirty && (m_Probes[i].type == ReflectionProbeType::Baked || m_Probes[i].refreshMode == ReflectionProbeRefreshMode::OnLoad)) RequestBake(i);
		}
		CreatePrefilterResources(device);
		PrefilterAll(device, commandBuffer);
		BuildGPUData();
		const size_t size = sizeof(m_Header) + std::max<size_t>(1, m_GPUProbes.size()) * sizeof(VansReflectionProbeGPU);
		m_MetadataBuffer.CreatVulkanBuffer(m_Device, size, VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		UploadMetadata();
	}

	void VansReflectionProbeSystem::CreatePrefilterResources(VansVKDevice& device)
	{
		if (!m_SpecularArray || m_MipCount <= 1) return;
		m_PrefilterShader = new VansComputeShader();
		m_PrefilterShader->InitShader(device.GetLogicDevice(),
			(VansConfigration::GetInstance()->GetProjectRootPath() + "EngineAssets/Shaders/ReflectionProbePrefilter").c_str());
		struct Params { float roughness; uint32_t outputSize; uint32_t cubeCount; uint32_t sampleCount; uint32_t baseCube; };
		m_PrefilterShader->SetPushConstant(sizeof(Params));
		VkDescriptorSetLayoutBinding source{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
		VkDescriptorSetLayoutBinding output{ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
		auto* manager = VansVKDescriptorManager::GetInstance();
		manager->CreateDesciptorSetLayout({ source, output }, m_PrefilterLayout);
		std::vector<VkDescriptorSetLayout> layouts(m_MipCount - 1, m_PrefilterLayout);
		manager->AllocateDescriptorSet(layouts, m_PrefilterSets);
		m_PrefilterMipViews.resize(m_MipCount - 1, VK_NULL_HANDLE);
		manager->ResetState();
		for (uint32_t mip = 1; mip < m_MipCount; ++mip)
		{
			m_PrefilterMipViews[mip - 1] = m_SpecularArray->GetImage().CreateMipArrayView(device.GetLogicDevice(), mip);
			manager->m_ImageDescInfos.push_back({ m_PrefilterSets[mip - 1], 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{{ m_SpecularArray->GetImage().GetSampler(), m_SpecularArray->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }} });
			manager->m_ImageDescInfos.push_back({ m_PrefilterSets[mip - 1], 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{{ VK_NULL_HANDLE, m_PrefilterMipViews[mip - 1], VK_IMAGE_LAYOUT_GENERAL }} });
		}
		manager->UpdateDescriptorSets();
	}

	void VansReflectionProbeSystem::PrefilterAll(VansVKDevice& device, VansVKCommandBuffer& commandBuffer)
	{
		if (!m_PrefilterShader || m_PrefilterSets.empty()) return;
		auto& image = m_SpecularArray->GetImage();
		commandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		VkImageMemoryBarrier toGeneral{}; toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toGeneral.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; toGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		toGeneral.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toGeneral.image = image.GetImage(); toGeneral.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, m_MipCount, 0, image.GetImageCreateInfo().arrayLayers };
		commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {}, {}, { toGeneral });
		struct Params { float roughness; uint32_t outputSize; uint32_t cubeCount; uint32_t sampleCount; uint32_t baseCube; } params{};
		params.cubeCount = std::max(1u, (uint32_t)m_Probes.size()); params.sampleCount = 128;
		commandBuffer.EnsureComputeShader(*m_PrefilterShader, { m_PrefilterLayout });
		for (uint32_t mip = 1; mip < m_MipCount; ++mip)
		{
			params.roughness = float(mip) / float(m_MipCount - 1); params.outputSize = std::max(1u, m_ArrayResolution >> mip);
			m_PrefilterShader->SetPushConstantData(&params);
			commandBuffer.DispatchCompute(*m_PrefilterShader, (params.outputSize + 7u) / 8u,
				(params.outputSize + 7u) / 8u, params.cubeCount * 6u, { m_PrefilterSets[mip - 1] });
		}
		VkImageMemoryBarrier toRead = toGeneral; toRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; toRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {}, {}, { toRead });
		commandBuffer.EndCommandBufferRecord();
		VansVKCommandBuffer::SubmitCommands(device.GetGraphicsQueue(), device.GetLogicDevice(),
			{ commandBuffer.GetVKCommandBuffer() }, {}, {}, commandBuffer.m_CommandBufferFinishSubmitFence);
		commandBuffer.ResetCommandBuffer(false); image.SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	void VansReflectionProbeSystem::PrefilterProbe(VansVKDevice& device,
		VansVKCommandBuffer& commandBuffer, uint32_t probeIndex)
	{
		if (!m_PrefilterShader || m_PrefilterSets.empty() || probeIndex >= m_Probes.size()) return;
		auto& image = m_SpecularArray->GetImage();
		commandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		VkImageMemoryBarrier toGeneral{}; toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toGeneral.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; toGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		toGeneral.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toGeneral.image = image.GetImage(); toGeneral.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, m_MipCount, 0, image.GetImageCreateInfo().arrayLayers };
		commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {}, {}, { toGeneral });
		struct Params { float roughness; uint32_t outputSize; uint32_t cubeCount; uint32_t sampleCount; uint32_t baseCube; } params{};
		params.cubeCount = 1u; params.sampleCount = 128u; params.baseCube = probeIndex;
		commandBuffer.EnsureComputeShader(*m_PrefilterShader, { m_PrefilterLayout });
		for (uint32_t mip = 1; mip < m_MipCount; ++mip)
		{
			params.roughness = float(mip) / float(m_MipCount - 1);
			params.outputSize = std::max(1u, m_ArrayResolution >> mip);
			m_PrefilterShader->SetPushConstantData(&params);
			commandBuffer.DispatchCompute(*m_PrefilterShader, (params.outputSize + 7u) / 8u,
				(params.outputSize + 7u) / 8u, 6u, { m_PrefilterSets[mip - 1] });
		}
		VkImageMemoryBarrier toRead = toGeneral; toRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; toRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {}, {}, { toRead });
		commandBuffer.EndCommandBufferRecord();
		VansVKCommandBuffer::SubmitCommands(device.GetGraphicsQueue(), device.GetLogicDevice(),
			{ commandBuffer.GetVKCommandBuffer() }, {}, {}, commandBuffer.m_CommandBufferFinishSubmitFence);
		commandBuffer.ResetCommandBuffer(false); image.SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	void VansReflectionProbeSystem::BuildGPUData()
	{
		m_GPUProbes.clear();
		for (size_t i = 0; i < m_Probes.size(); ++i)
		{
			const auto& p = m_Probes[i];
			if (p.type == ReflectionProbeType::Sky) continue;
			VansReflectionProbeGPU gpu{};
			gpu.positionAndRadius = glm::vec4(p.position, p.radius);
			gpu.boxMinAndType = glm::vec4(p.boxMin, p.shape == ReflectionProbeShape::Box ? 1.0f : 0.0f);
			gpu.boxMaxAndPriority = glm::vec4(p.boxMax, p.priority);
			gpu.fadeAndIntensity = glm::vec4(p.blendDistance, p.intensity, 0.0f, 0.0f);
			gpu.capturePositionAndLayer = glm::vec4(p.capturePosition, float(i));
			uint32_t flags = (p.enabled && i < m_BakeResults.size() && m_BakeResults[i].valid ? 1u : 0u) |
				(p.boxProjection ? 2u : 0u) | (p.portal ? 4u : 0u) | (uint32_t(p.type) << 8u);
			gpu.regionAndFlags = glm::uvec4(p.regionId, flags, p.cullingMask, 0u);
			gpu.specularAndMip = glm::vec4(0.0f, p.specularIntensity, float(m_MipCount - 1), 0.0f);
			m_GPUProbes.push_back(gpu);
		}
		m_Header.probeCount = (uint32_t)m_GPUProbes.size();
		m_Header.activeProbeCount = (uint32_t)std::count_if(m_GPUProbes.begin(), m_GPUProbes.end(), [](const auto& p) { return (p.regionAndFlags.y & 1u) != 0u; });
		m_Header.maxBlendCount = m_LightingSettings.maxBlendCount;
		m_Header.debugView = (uint32_t)m_EditorState.debugView;
		m_Header.lightingParams = glm::vec4(m_LightingSettings.ssrRoughnessFadeStart,
			std::max(m_LightingSettings.ssrRoughnessFadeEnd, m_LightingSettings.ssrRoughnessFadeStart + 0.001f),
			m_LightingSettings.skyIntensity, 0.0f);

		m_Header.uniformGridOrigin = glm::vec4(0.0f);
		m_Header.uniformGridInvCellSize = glm::vec4(0.0f);
		m_Header.uniformGridDimensionsAndFlags = glm::uvec4(0u);
		const glm::vec3 volumeSize = glm::max(
			m_PlacementSettings.volumeMax - m_PlacementSettings.volumeMin, glm::vec3(0.5f));
		const glm::uvec3 gridDimensions = glm::uvec3(glm::clamp(
			glm::ivec3(glm::ceil(volumeSize / std::max(m_PlacementSettings.uniformSpacing, 0.5f))),
			glm::ivec3(1), glm::ivec3(32)));
		const size_t expectedUniformProbeCount = size_t(gridDimensions.x) * gridDimensions.y * gridDimensions.z;
		const bool pureUniformGrid = m_GPUProbes.size() == expectedUniformProbeCount &&
			std::all_of(m_Probes.begin(), m_Probes.end(), [](const VansReflectionProbeDesc& probe)
			{
				return probe.type == ReflectionProbeType::Sky || probe.autoGenerated;
			});
		if (pureUniformGrid)
		{
			const glm::vec3 cellSize = volumeSize / glm::vec3(gridDimensions);
			m_Header.uniformGridOrigin = glm::vec4(m_PlacementSettings.volumeMin, 0.0f);
			m_Header.uniformGridInvCellSize = glm::vec4(1.0f / cellSize, 0.0f);
			m_Header.uniformGridDimensionsAndFlags = glm::uvec4(gridDimensions, 1u);
		}
	}

	void VansReflectionProbeSystem::UploadMetadata()
	{
		if (m_MetadataBuffer.GetNativeBuffer() == VK_NULL_HANDLE) return;
		BuildGPUData();
		std::vector<uint8_t> bytes(sizeof(m_Header) + m_GPUProbes.size() * sizeof(VansReflectionProbeGPU));
		std::memcpy(bytes.data(), &m_Header, sizeof(m_Header));
		if (!m_GPUProbes.empty()) std::memcpy(bytes.data() + sizeof(m_Header), m_GPUProbes.data(), m_GPUProbes.size() * sizeof(VansReflectionProbeGPU));
		m_MetadataBuffer.SetBufferData(bytes.data(), 0, (int)bytes.size());
	}

	void VansReflectionProbeSystem::UpdateGlobalDescriptors(VkDescriptorSet globalSet)
	{
		if (!m_SpecularArray || globalSet == VK_NULL_HANDLE || m_MetadataBuffer.GetNativeBuffer() == VK_NULL_HANDLE) return;
		auto* desc = VansVKDescriptorManager::GetInstance(); desc->ResetState();
		desc->m_ImageDescInfos.push_back({ globalSet, GLOBAL_BINDING_REFLECTION_PROBE_SPECULAR, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ m_SpecularArray->GetImage().GetSampler(), m_SpecularArray->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }} });
		desc->m_BufferDescInfos.push_back({ globalSet, GLOBAL_BINDING_REFLECTION_PROBE_BUFFER, 0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{ m_MetadataBuffer.GetNativeBuffer(), 0, m_MetadataBuffer.GetBufferSize() }} });
		desc->UpdateDescriptorSets();
	}

	void VansReflectionProbeSystem::RequestBake(size_t index)
	{
		if (index >= m_Probes.size() || m_Probes[index].type == ReflectionProbeType::Sky) return;
		if (std::find(m_BakeQueue.begin(), m_BakeQueue.end(), index) == m_BakeQueue.end()) m_BakeQueue.push_back(index);
		m_BakeResults[index].status = "Queued";
	}

	void VansReflectionProbeSystem::RequestBakeAll() { for (size_t i = 0; i < m_Probes.size(); ++i) RequestBake(i); }
	void VansReflectionProbeSystem::DeferInitialBakeForGI(uint32_t spatialUpdateDivisor, uint32_t directionUpdateSlices)
	{
		if (m_BakeQueue.empty())
		{
			m_GIWarmupFramesRemaining = 0;
			return;
		}
		const uint32_t divisor = std::max(spatialUpdateDivisor, 1u);
		const uint32_t directionSlices = std::max(directionUpdateSlices, 1u);
		m_GIWarmupFramesRemaining = divisor * divisor * divisor * directionSlices;
		m_LastGIWarmupFrame = 0xffffffffu;
	}
	bool VansReflectionProbeSystem::ConsumeBakeRequest(size_t& outIndex)
	{
		if (m_BakeQueue.empty()) return false; outIndex = m_BakeQueue.front(); m_BakeQueue.erase(m_BakeQueue.begin()); return true;
	}
	void VansReflectionProbeSystem::MarkBakeComplete(size_t index, bool success, const std::string& message)
	{
		if (index >= m_BakeResults.size()) return;
		auto& r = m_BakeResults[index]; r.valid = success; r.dirty = !success; r.status = message; r.revision = ++m_BakeRevision; UploadMetadata();
	}
	void VansReflectionProbeSystem::MarkDirty(size_t index)
	{
		if (index < m_BakeResults.size()) { m_BakeResults[index].dirty = true; m_BakeResults[index].status = "Modified - bake required"; UploadMetadata(); }
	}
	void VansReflectionProbeSystem::UpdateRealtimeProbes(uint32_t frameIndex)
	{
		for (size_t i = 0; i < m_Probes.size(); ++i)
		{
			const auto& p = m_Probes[i]; if (!p.enabled || p.type != ReflectionProbeType::Realtime) continue;
			if (p.refreshMode == ReflectionProbeRefreshMode::EveryFrame || (p.refreshMode == ReflectionProbeRefreshMode::TimeSliced && frameIndex % 6u == i % 6u)) RequestBake(i);
		}
	}

	bool VansReflectionProbeSystem::EnsureCaptureResources(VansScene& scene, VansVKDevice& device)
	{
		if (!m_SpecularArray || scene.m_GlobalDescriptorSet == VK_NULL_HANDLE) return false;
		auto* materialManager = scene.GetMaterialManager();
		if (!materialManager || materialManager->m_AtmospherePBRDataBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
		{
			VANS_LOG_ERROR("Reflection probe capture requires an initialized atmosphere buffer.");
			return false;
		}
		const size_t requiredLayerCount = m_Probes.size() * 6u;
		if (m_SpecularArray->GetImage().GetImageCreateInfo().arrayLayers != requiredLayerCount)
		{
			VANS_LOG_ERROR("Reflection probe capture array is stale; recreate GPU resources before baking.");
			return false;
		}
		if (m_CaptureRenderPass != VK_NULL_HANDLE)
		{
			if (m_CaptureFramebuffers.size() == requiredLayerCount) return true;
			DestroyCaptureResources();
		}

		VkDevice logicalDevice = device.GetLogicDevice();
		auto fail = [this]()
		{
			DestroyCaptureResources();
			return false;
		};
		if (!m_CaptureCameraBuffer.CreatVulkanBuffer(logicalDevice, sizeof(CaptureCameraData), VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) return false;
		m_CaptureCameraBufferCreated = true;

		auto* descriptors = VansVKDescriptorManager::GetInstance();
		auto* shR = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT);
		auto* shG = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT);
		auto* shB = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT);
		if (!materialManager->m_PreConvDiffuse || !shR || !shG || !shB)
		{
			VANS_LOG_ERROR("Reflection probe capture requires sky diffuse and GI SH volume textures.");
			return fail();
		}

		const std::vector<VkDescriptorSetLayoutBinding> bindings = {
			{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
		};
		if (!descriptors->CreateDesciptorSetLayout(bindings, m_CaptureDescriptorLayout)) return fail();
		std::vector<VkDescriptorSet> captureSets;
		if (!descriptors->AllocateDescriptorSet({ m_CaptureDescriptorLayout }, captureSets) || captureSets.empty()) return fail();
		m_CaptureDescriptorSet = captureSets[0];
		auto* renderPassManager = VansRenderPassManager::GetInstance();
		descriptors->ResetState();
		descriptors->m_BufferDescInfos.push_back({ m_CaptureDescriptorSet, 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{{ m_CaptureCameraBuffer.GetNativeBuffer(), 0, sizeof(CaptureCameraData) }} });
		descriptors->m_BufferDescInfos.push_back({ m_CaptureDescriptorSet, 2, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{{ materialManager->m_AtmospherePBRDataBuffer.GetNativeBuffer(), 0,
				materialManager->m_AtmospherePBRDataBuffer.GetBufferSize() }} });
		descriptors->m_ImageDescInfos.push_back({ m_CaptureDescriptorSet, 1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ renderPassManager->GetCascadeShadowSampler(), renderPassManager->GetCascadeShadowArrayView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }} });
		descriptors->m_ImageDescInfos.push_back({ m_CaptureDescriptorSet, 3, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ materialManager->m_PreConvDiffuse->GetImage().GetSampler(), materialManager->m_PreConvDiffuse->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }} });
		descriptors->m_ImageDescInfos.push_back({ m_CaptureDescriptorSet, 4, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ shR->GetImage().GetSampler(), shR->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }} });
		descriptors->m_ImageDescInfos.push_back({ m_CaptureDescriptorSet, 5, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ shG->GetImage().GetSampler(), shG->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }} });
		descriptors->m_ImageDescInfos.push_back({ m_CaptureDescriptorSet, 6, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ shB->GetImage().GetSampler(), shB->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }} });
		descriptors->UpdateDescriptorSets();

		const VkExtent3D extent{ m_ArrayResolution, m_ArrayResolution, 1u };
		if (!m_CaptureDepthImage.CreateVulkanImage(logicalDevice, extent, VK_FORMAT_D32_SFLOAT, 1, 1,
			VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_SAMPLE_COUNT_1_BIT)) return fail();
		m_CaptureDepthCreated = true;

		VkAttachmentDescription attachments[2]{};
		attachments[0] = { 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		attachments[1] = { 0, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
		VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
		VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &colorRef; subpass.pDepthStencilAttachment = &depthRef;
		VkSubpassDependency dependencies[2]{};
		dependencies[0] = { VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_DEPENDENCY_BY_REGION_BIT };
		dependencies[1] = { 0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT };
		VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
		renderPassInfo.attachmentCount = 2; renderPassInfo.pAttachments = attachments;
		renderPassInfo.subpassCount = 1; renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 2; renderPassInfo.pDependencies = dependencies;
		if (vkCreateRenderPass(logicalDevice, &renderPassInfo, nullptr, &m_CaptureRenderPass) != VK_SUCCESS) return fail();

		m_CaptureFaceViews.resize(requiredLayerCount, VK_NULL_HANDLE);
		m_CaptureFramebuffers.resize(requiredLayerCount, VK_NULL_HANDLE);
		for (size_t layer = 0; layer < m_CaptureFaceViews.size(); ++layer)
		{
			m_CaptureFaceViews[layer] = m_SpecularArray->GetImage().CreateLayerMipView(logicalDevice, (uint32_t)layer, 0);
			if (m_CaptureFaceViews[layer] == VK_NULL_HANDLE) return fail();
			VkImageView framebufferAttachments[] = { m_CaptureFaceViews[layer], m_CaptureDepthImage.GetImageView() };
			VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
			framebufferInfo.renderPass = m_CaptureRenderPass; framebufferInfo.attachmentCount = 2;
			framebufferInfo.pAttachments = framebufferAttachments; framebufferInfo.width = m_ArrayResolution;
			framebufferInfo.height = m_ArrayResolution; framebufferInfo.layers = 1;
			if (vkCreateFramebuffer(logicalDevice, &framebufferInfo, nullptr, &m_CaptureFramebuffers[layer]) != VK_SUCCESS) return fail();
		}

		const std::string shaderRoot = VansConfigration::GetInstance()->GetProjectRootPath() + "EngineAssets/Shaders/";
		m_CaptureGeometryShader = new VansGraphicsShader();
		if (!m_CaptureGeometryShader->InitShader(logicalDevice, (shaderRoot + "ReflectionProbeCapture").c_str())) return fail();
		m_CaptureGeometryShader->SetPushConstant(sizeof(CaptureDrawData));
		m_CaptureGeometryShader->SetColorAttachmentCount(1);
		m_CaptureGeometryShader->SetDrawStateData(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS, VK_CULL_MODE_BACK_BIT);
		m_CaptureGeometryShader->SetFrontFace(VK_FRONT_FACE_CLOCKWISE);
		m_CaptureSkyShader = new VansGraphicsShader();
		if (!m_CaptureSkyShader->InitShader(logicalDevice, (shaderRoot + "ReflectionProbeCaptureSky").c_str())) return fail();
		m_CaptureSkyShader->SetColorAttachmentCount(1);
		m_CaptureSkyShader->SetDrawStateData(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS, VK_CULL_MODE_NONE);
		return true;
	}

	void VansReflectionProbeSystem::DestroyCaptureResources()
	{
		if (m_Device == VK_NULL_HANDLE) return;
		for (VkFramebuffer framebuffer : m_CaptureFramebuffers)
			if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
		for (VkImageView view : m_CaptureFaceViews)
			if (view != VK_NULL_HANDLE) vkDestroyImageView(m_Device, view, nullptr);
		m_CaptureFramebuffers.clear(); m_CaptureFaceViews.clear();
		if (m_CaptureRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(m_Device, m_CaptureRenderPass, nullptr);
		m_CaptureRenderPass = VK_NULL_HANDLE;
		delete m_CaptureGeometryShader; m_CaptureGeometryShader = nullptr;
		delete m_CaptureSkyShader; m_CaptureSkyShader = nullptr;
		if (m_CaptureDepthCreated) m_CaptureDepthImage.DestroyVulkanImage(m_Device);
		if (m_CaptureCameraBufferCreated) m_CaptureCameraBuffer.DestroyVulkanBuffer(m_Device);
		m_CaptureDepthCreated = false; m_CaptureCameraBufferCreated = false;
		auto* descriptors = VansVKDescriptorManager::GetInstance();
		if (m_CaptureDescriptorSet != VK_NULL_HANDLE)
		{
			std::vector<VkDescriptorSet> sets{ m_CaptureDescriptorSet }; descriptors->DestroyDescriptorSet(sets);
			m_CaptureDescriptorSet = VK_NULL_HANDLE;
		}
		descriptors->DestroyDescriptorSetLayout(m_CaptureDescriptorLayout);
	}

	bool VansReflectionProbeSystem::CaptureFaceGPU(VansScene& scene, VansVKDevice& device,
		VansVKCommandBuffer& commandBuffer, const VansReflectionProbeDesc& probe, size_t probeIndex, uint32_t face)
	{
		if (!EnsureCaptureResources(scene, device) || face >= 6 || probeIndex >= m_Probes.size()) return false;
		static const glm::vec3 directions[6] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
		static const glm::vec3 up[6] = { {0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0} };
		glm::mat4 view = glm::lookAt(probe.capturePosition, probe.capturePosition + directions[face], up[face]);
		glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, probe.nearPlane, probe.farPlane);
		const VansGISettings& gi = scene.GetGISettings();
		const float giVolumeSize = float(gi.gridSize) * gi.probeSpacing;
		const glm::vec3 giVolumeMin = gi.regionCenter - glm::vec3(giVolumeSize * 0.5f);
		CaptureCameraData cameraData{};
		cameraData.viewProjection = projection * view;
		cameraData.inverseViewProjection = glm::inverse(cameraData.viewProjection);
		cameraData.position = glm::vec4(probe.capturePosition, 1.0f);
		cameraData.giVolumeMin = glm::vec4(giVolumeMin, 0.0f);
		cameraData.giVolumeSizeAndBias = glm::vec4(giVolumeSize, giVolumeSize, giVolumeSize, gi.normalBias);
		m_CaptureCameraBuffer.SetBufferData(&cameraData, 0, sizeof(cameraData));

		commandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		VkClearValue clears[2]{}; clears[0].color = {{ 0.0f, 0.0f, 0.0f, 1.0f }}; clears[1].depthStencil = { 1.0f, 0 };
		VkRenderPassBeginInfo beginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		beginInfo.renderPass = m_CaptureRenderPass; beginInfo.framebuffer = m_CaptureFramebuffers[probeIndex * 6u + face];
		beginInfo.renderArea = { {0,0}, {m_ArrayResolution,m_ArrayResolution} }; beginInfo.clearValueCount = 2; beginInfo.pClearValues = clears;
		vkCmdBeginRenderPass(commandBuffer.GetVKCommandBuffer(), &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

		GlobalStateData state; state.currentRenderPass = m_CaptureRenderPass; state.currentSubpass = 0;
		// Cube images use Vulkan's native top-to-bottom t coordinate. A negative
		// viewport vertically mirrored every captured face relative to samplerCube.
		state.viewport = { 0.0f, 0.0f, float(m_ArrayResolution), float(m_ArrayResolution), 0.0f, 1.0f };
		state.scissor = { {0,0}, {m_ArrayResolution,m_ArrayResolution} };
		commandBuffer.SetViewport(0, { state.viewport }); commandBuffer.SetScissor(0, { state.scissor });
		const std::vector<VkDescriptorSetLayout> layouts{ scene.m_GlobalDescriptorSetLayout, m_CaptureDescriptorLayout };
		const std::vector<VkDescriptorSet> sets{ scene.m_GlobalDescriptorSet, m_CaptureDescriptorSet };
		commandBuffer.EnsureGraphicsShader(*m_CaptureSkyShader, state, layouts);
		commandBuffer.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_CaptureSkyShader, 0, sets, {});
		commandBuffer.BindGraphicsPipeline(*m_CaptureSkyShader->GetGraphicsPipeline());
		commandBuffer.Draw(3, 1, 0, 0);

		std::vector<VansRenderNode*> nodes = scene.m_OpaqueRenderNodes;
		if (scene.m_TerrainRenderNode) nodes.push_back(scene.m_TerrainRenderNode);
		for (VansRenderNode* node : nodes)
		{
			if (!node || !node->m_Mesh || !node->m_Material || (probe.cullingMask & uint32_t(node->GetNodeType())) == 0u) continue;
			CaptureDrawData draw{}; draw.model = node->m_ModelData.ModelMatrix; draw.albedo = glm::vec4(0.6f,0.6f,0.6f,1.0f);
			draw.params.w = -1.0f;
			if (auto* material = dynamic_cast<VansPBRMaterial*>(node->m_Material))
			{
				draw.albedo = glm::vec4(material->m_BasePBRParam.m_albedo, 1.0f);
				draw.params.y = material->m_BasePBRParam.m_roughness;
				draw.params.z = material->m_BasePBRParam.m_metallic;
				draw.params.w = float(material->m_MaterialIndex);
			}
			else if (auto* emissive = dynamic_cast<VansEmissiveMaterial*>(node->m_Material))
			{
				draw.emissive = glm::vec4(emissive->m_BasePBRParam.m_albedo * emissive->m_BasePBRParam.m_roughness, 1.0f);
				draw.params.x = 1.0f;
				draw.params.w = float(emissive->m_MaterialIndex);
			}
			commandBuffer.BindMesh(*node->m_Mesh, 0, state);
			commandBuffer.EnsureGraphicsShader(*m_CaptureGeometryShader, state, layouts);
			commandBuffer.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_CaptureGeometryShader, 0, sets, {});
			commandBuffer.UpdatePushConstants(*m_CaptureGeometryShader->GetGraphicsPipeline(),
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(draw), &draw);
			commandBuffer.DrawMesh(*node->m_Mesh, *m_CaptureGeometryShader, 1);
		}
		vkCmdEndRenderPass(commandBuffer.GetVKCommandBuffer());
		commandBuffer.EndCommandBufferRecord();
		const bool submitted = VansVKCommandBuffer::SubmitCommands(device.GetGraphicsQueue(), device.GetLogicDevice(),
			{ commandBuffer.GetVKCommandBuffer() }, {}, {}, commandBuffer.m_CommandBufferFinishSubmitFence);
		commandBuffer.ResetCommandBuffer(false);
		return submitted;
	}

	bool VansReflectionProbeSystem::SaveProbeCacheGPU(VansVKDevice& device, VansVKCommandBuffer& commandBuffer,
		const VansReflectionProbeDesc& probe, size_t probeIndex)
	{
		if (!m_SpecularArray || probeIndex >= m_Probes.size()) return false;
		const VkDeviceSize faceBytes = VkDeviceSize(m_ArrayResolution) * m_ArrayResolution * 4u * sizeof(uint16_t);
		VansVKBuffer readback;
		if (!readback.CreatVulkanBuffer(device.GetLogicDevice(), faceBytes * 6u, VK_FORMAT_R16_UINT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) return false;
		if (!readback.PersistentMap()) { readback.DestroyVulkanBuffer(device.GetLogicDevice()); return false; }
		std::vector<VkBufferImageCopy> regions(6);
		for (uint32_t face = 0; face < 6; ++face)
		{
			regions[face].bufferOffset = faceBytes * face;
			regions[face].imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, uint32_t(probeIndex * 6u + face), 1 };
			regions[face].imageExtent = { m_ArrayResolution, m_ArrayResolution, 1 };
		}
		auto& image = m_SpecularArray->GetImage();
		commandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		VkImageMemoryBarrier toTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		toTransfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		toTransfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toTransfer.image = image.GetImage(); toTransfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, uint32_t(probeIndex * 6u), 6 };
		commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, {}, {}, { toTransfer });
		VansVKMemoryManager::CopyImageToBuffer(commandBuffer, image, readback, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, regions);
		VkImageMemoryBarrier toRead = toTransfer; toRead.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, {}, {}, { toRead });
		commandBuffer.EndCommandBufferRecord();
		const bool submitted = VansVKCommandBuffer::SubmitCommands(device.GetGraphicsQueue(), device.GetLogicDevice(),
			{ commandBuffer.GetVKCommandBuffer() }, {}, {}, commandBuffer.m_CommandBufferFinishSubmitFence);
		commandBuffer.ResetCommandBuffer(false);
		if (!submitted)
		{
			readback.Unmap();
			readback.DestroyVulkanBuffer(device.GetLogicDevice());
			return false;
		}

		fs::path base = probe.cachePath.empty() ? fs::path(m_ScenePath).parent_path()/"ReflectionProbes"/probe.name : fs::path(probe.cachePath);
		std::error_code error; fs::create_directories(base, error);
		static const char* names[6] = { "px", "nx", "py", "ny", "pz", "nz" };
		const uint16_t* half = static_cast<const uint16_t*>(readback.GetMappedPtr());
		std::vector<float> pixels(size_t(m_ArrayResolution) * m_ArrayResolution * 4u);
		bool success = half != nullptr;
		for (uint32_t face = 0; success && face < 6; ++face)
		{
			const uint16_t* source = half + size_t(face) * pixels.size();
			for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = HalfToFloat(source[i]);
			const fs::path output = base / (std::string(names[face]) + ".hdr");
			success = stbi_write_hdr(output.string().c_str(), (int)m_ArrayResolution, (int)m_ArrayResolution, 4, pixels.data()) != 0;
		}
		if (success)
		{
			std::ofstream versionFile(base / kReflectionProbeCacheVersionFile, std::ios::trunc);
			versionFile << kReflectionProbeCacheVersion << '\n';
			success = versionFile.good();
		}
		readback.Unmap(); readback.DestroyVulkanBuffer(device.GetLogicDevice());
		return success;
	}

	void VansReflectionProbeSystem::ProcessBakeQueue(VansScene& scene, VansVKDevice& device,
		VansVKCommandBuffer& commandBuffer, uint32_t frameIndex, bool updateRealtime)
	{
		if (updateRealtime) UpdateRealtimeProbes(frameIndex);
		if (!m_SpecularArray || m_BakeQueue.empty()) return;
		if (m_GIWarmupFramesRemaining > 0)
		{
			// Consume at most one warmup step per frame so every spatial GI phase
			// has completed once before the first capture.
			if (m_LastGIWarmupFrame != frameIndex)
			{
				m_LastGIWarmupFrame = frameIndex;
				--m_GIWarmupFramesRemaining;
			}
			return;
		}
		if (m_ActiveBakeIndex == size_t(-1)) { m_ActiveBakeIndex = m_BakeQueue.front(); m_ActiveBakeFace = 0; }
		if (m_ActiveBakeIndex >= m_Probes.size()) { m_BakeQueue.erase(m_BakeQueue.begin()); m_ActiveBakeIndex = size_t(-1); return; }
		auto& probe = m_Probes[m_ActiveBakeIndex]; auto& result = m_BakeResults[m_ActiveBakeIndex];
		result.status = "Capturing face " + std::to_string(m_ActiveBakeFace + 1) + "/6";
		if (!CaptureFaceGPU(scene, device, commandBuffer, probe, m_ActiveBakeIndex, m_ActiveBakeFace))
		{
			MarkBakeComplete(m_ActiveBakeIndex, false, "Capture failed"); m_BakeQueue.erase(m_BakeQueue.begin()); m_ActiveBakeIndex = size_t(-1); return;
		}
		fs::path base = probe.cachePath.empty() ? fs::path(m_ScenePath).parent_path()/"ReflectionProbes"/probe.name : fs::path(probe.cachePath);
		probe.cachePath = base.string(); result.cachePath = probe.cachePath;
		++m_ActiveBakeFace;
		if (m_ActiveBakeFace >= 6)
		{
			PrefilterProbe(device, commandBuffer, uint32_t(m_ActiveBakeIndex));
			const bool saved = SaveProbeCacheGPU(device, commandBuffer, probe, m_ActiveBakeIndex);
			MarkBakeComplete(m_ActiveBakeIndex, true, saved ? "GPU captured, GGX prefiltered and cached" : "GPU captured and prefiltered; cache readback failed");
			m_BakeQueue.erase(m_BakeQueue.begin()); m_ActiveBakeIndex = size_t(-1); m_ActiveBakeFace = 0;
		}
	}

	void VansReflectionProbeSystem::BakeQueuedProbesNow(VansScene& scene, VansVKDevice& device,
		VansVKCommandBuffer& commandBuffer)
	{
		if (!m_SpecularArray) return;
		if (m_GIWarmupFramesRemaining > 0) return;
		while (!m_BakeQueue.empty())
			ProcessBakeQueue(scene, device, commandBuffer, 0u, false);
	}

	uint32_t VansReflectionProbeSystem::GetBakeFaceBudget() const
	{
		const size_t index = m_ActiveBakeIndex != size_t(-1) ? m_ActiveBakeIndex : (m_BakeQueue.empty() ? size_t(-1) : m_BakeQueue.front());
		if (index >= m_Probes.size() || m_Probes[index].type != ReflectionProbeType::Realtime) return 1u;
		return std::clamp(m_Probes[index].realtimeFacesPerFrame, 1u, 6u);
	}
}
