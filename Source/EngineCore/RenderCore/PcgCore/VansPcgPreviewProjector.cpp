#include "VansPcgPreviewProjector.h"

#include "../VansRenderThreadTransaction.h"
#include "../VansScene.h"
#include "../SceneBuild/VansSceneProjectResourceBuilder.h"
#include "../VegetationCore/VansVegetationCollection.h"
#include "../VegetationCore/VansVegetationSystem.h"
#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansVKDevice.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace VansGraphics
{
namespace
{
class PcgPreviewProjectionTransaction final : public IVansRenderThreadTransaction
{
public:
	explicit PcgPreviewProjectionTransaction(std::function<bool()> apply)
		: m_Apply(std::move(apply))
	{
	}

	bool Execute(VansGraphicsDevice&) override { return m_Apply(); }

private:
	std::function<bool()> m_Apply;
};

bool HasCpuGeometry(const VansMesh* mesh)
{
	if (!mesh) return false;
	if (!mesh->m_IsMultiMesh) return !mesh->GetMeshRawPositionData().empty();
	return std::all_of(
		mesh->m_SubMeshes.begin(), mesh->m_SubMeshes.end(),
		[](const auto* part)
		{
			return part && !part->GetMeshRawPositionData().empty();
		});
}
}

bool VansPcgPreviewProjector::Project(
	VansScene& scene,
	VansPcgPreviewProjection projection,
	std::string& error)
{
	error.clear();
	const auto apply = [&]()
	{
		auto* device = scene.GetRuntimeResourceDevice();
		if (!device || !scene.GetVegetationCollection())
		{
			error = "The scene vegetation renderer is unavailable.";
			return false;
		}

		VkDevice native = device->GetLogicDevice();
		std::vector<Vans::VansSceneMeshResourceRequest> meshes;
		for (auto request : projection.resources.meshes)
		{
			auto* loaded = static_cast<VansMesh*>(scene.FindMeshAsset(request.name));
			if (loaded && (!request.needCpuData || HasCpuGeometry(loaded))) continue;
			if (loaded)
			{
				// Preserve the existing runtime mesh while retaining a CPU copy for grass skinning.
				request.name = "pcg-cpu/" + request.assetGuid;
				if (scene.FindMeshAsset(request.name)) continue;
			}
			request.supportRayTracing = false;
			meshes.push_back(std::move(request));
		}

		std::vector<Vans::VansSceneTextureResourceRequest> textures;
		for (const auto& request : projection.resources.textures)
			if (!scene.FindTextureAssetByGuid(request.assetGuid)) textures.push_back(request);

		std::vector<Vans::VansSceneShaderResourceRequest> shaders;
		for (const auto& request : projection.resources.shaders)
			if (!scene.FindShaderAsset(request.name)) shaders.push_back(request);

		for (const auto& guid : projection.requiredMaterials)
		{
			if (scene.FindMaterialAsset(guid)) continue;
			error = "This material was added after the scene opened. Reopen the scene to initialize it: " + guid;
			return false;
		}

		if (!VansSceneProjectResourceBuilder::LoadMeshes(
				scene, meshes, projection.loadContext, native, device) ||
			(!textures.empty() && !VansSceneProjectResourceBuilder::LoadTextures(
				scene, textures, projection.loadContext, device, false)))
		{
			error = "A selected PCG model or texture could not be loaded.";
			return false;
		}
		if (!shaders.empty())
		{
			if (!VansSceneProjectResourceBuilder::RegisterShaders(
					scene, shaders, projection.loadContext, native, false))
			{
				error = "A selected PCG shader could not be registered.";
				return false;
			}
		}
		scene.FinalizeProjectResourceBatch();

		if (!textures.empty() && projection.refreshMaterialTextures)
			projection.refreshMaterialTextures(scene);

		if (!scene.GetVegetationCollection()->Apply(
				scene, native, *projection.update, nullptr, error))
			return false;
		scene.GetVegetationCollection()->ForEach([](VansVegetationSystem& system)
		{
			for (const auto& part : system.GetRenderConfigsGPU())
			{
				auto* material = static_cast<VansGrassMaterial*>(part.material);
				if (material && material->m_GrassOwnedLayout == VK_NULL_HANDLE)
					material->BuildGrassTextureDescriptors();
			}
		});
		return true;
	};

	try
	{
		if (!scene.ExecuteRenderThreadTransaction(
				std::make_unique<PcgPreviewProjectionTransaction>(apply)))
		{
			if (error.empty())
				error = "PCG preview could not run at this frame boundary. Apply again after the scene is ready.";
			return false;
		}
	}
	catch (const std::exception& exception)
	{
		error = exception.what();
		return false;
	}

	scene.DiscardPendingVegetationUpdates();
	return true;
}
}
