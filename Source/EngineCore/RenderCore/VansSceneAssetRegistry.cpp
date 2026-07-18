#include "VansSceneAssetRegistry.h"

#include <algorithm>

namespace VansGraphics
{
	VansAsset* VansSceneAssetRegistry::FindInLookup(
		const std::unordered_map<std::string, VansAsset*>& lookup,
		const std::string& name)
	{
		const auto it = lookup.find(name);
		return it != lookup.end() ? it->second : nullptr;
	}

	VansAsset* VansSceneAssetRegistry::FindAndBackfill(
		const std::vector<VansAsset*>& assets,
		std::unordered_map<std::string, VansAsset*>& lookup,
		const std::string& name)
	{
		for (auto* asset : assets)
		{
			if (asset && asset->m_AssetName == name)
			{
				lookup[name] = asset;
				return asset;
			}
		}
		return nullptr;
	}

	void VansSceneAssetRegistry::RegisterByName(std::unordered_map<std::string, VansAsset*>& lookup, VansAsset* asset)
	{
		if (asset && !asset->m_AssetName.empty())
			lookup[asset->m_AssetName] = asset;
	}

	VansAsset* VansSceneAssetRegistry::FindMesh(const std::string& name)
	{
		if (const auto alias = m_ProjectMeshAliases.find(name); alias != m_ProjectMeshAliases.end())
			return alias->second;
		if (auto* mesh = FindInLookup(m_MeshAssetLookup, name))
			return mesh;
		if (auto* mesh = FindInLookup(m_SceneSubMeshAssetLookup, name))
			return mesh;
		if (auto* mesh = FindAndBackfill(m_Meshes, m_MeshAssetLookup, name))
			return mesh;
		return FindAndBackfill(m_SceneSubMeshes, m_SceneSubMeshAssetLookup, name);
	}

	VansAsset* VansSceneAssetRegistry::FindShader(const std::string& name)
	{
		if (auto* shader = FindInLookup(m_ShaderAssetLookup, name))
			return shader;
		return FindAndBackfill(m_Shaders, m_ShaderAssetLookup, name);
	}

	VansAsset* VansSceneAssetRegistry::FindTexture(const std::string& name)
	{
		if (auto* texture = FindInLookup(m_TextureAssetLookup, name))
			return texture;
		return FindAndBackfill(m_Textures, m_TextureAssetLookup, name);
	}

	VansAsset* VansSceneAssetRegistry::FindMaterial(const std::string& name)
	{
		if (auto* material = FindInLookup(m_MaterialAssetLookup, name))
			return material;
		return FindAndBackfill(m_Materials, m_MaterialAssetLookup, name);
	}

	void VansSceneAssetRegistry::AddMesh(VansAsset* asset)
	{
		m_Meshes.push_back(asset);
		RegisterMesh(asset);
	}

	void VansSceneAssetRegistry::AddSceneSubMesh(VansAsset* asset)
	{
		m_SceneSubMeshes.push_back(asset);
		RegisterSceneSubMesh(asset);
	}

	void VansSceneAssetRegistry::AddShader(VansAsset* asset)
	{
		m_Shaders.push_back(asset);
		RegisterShader(asset);
	}

	void VansSceneAssetRegistry::AddTexture(VansAsset* asset)
	{
		m_Textures.push_back(asset);
		RegisterTexture(asset);
	}

	void VansSceneAssetRegistry::AddMaterial(VansAsset* asset)
	{
		m_Materials.push_back(asset);
		RegisterMaterial(asset);
	}

	void VansSceneAssetRegistry::RegisterMesh(VansAsset* asset)
	{
		RegisterByName(m_MeshAssetLookup, asset);
	}

	void VansSceneAssetRegistry::RegisterSceneSubMesh(VansAsset* asset)
	{
		RegisterByName(m_SceneSubMeshAssetLookup, asset);
	}

	void VansSceneAssetRegistry::RegisterShader(VansAsset* asset)
	{
		RegisterByName(m_ShaderAssetLookup, asset);
	}

	void VansSceneAssetRegistry::RegisterTexture(VansAsset* asset)
	{
		RegisterByName(m_TextureAssetLookup, asset);
	}

	void VansSceneAssetRegistry::RegisterMaterial(VansAsset* asset)
	{
		RegisterByName(m_MaterialAssetLookup, asset);
	}

	bool VansSceneAssetRegistry::HasProjectMeshAlias(const std::string& name) const
	{
		return m_ProjectMeshAliases.find(name) != m_ProjectMeshAliases.end();
	}

	void VansSceneAssetRegistry::SetProjectMeshAlias(const std::string& name, VansAsset* asset)
	{
		if (!name.empty() && asset)
			m_ProjectMeshAliases[name] = asset;
	}

	void VansSceneAssetRegistry::ClearProjectMeshAliases()
	{
		m_ProjectMeshAliases.clear();
	}

	void VansSceneAssetRegistry::RebuildLookup()
	{
		m_MeshAssetLookup.clear();
		m_SceneSubMeshAssetLookup.clear();
		m_TextureAssetLookup.clear();
		m_ShaderAssetLookup.clear();
		m_MaterialAssetLookup.clear();

		for (auto* mesh : m_Meshes)
			RegisterMesh(mesh);
		for (auto* mesh : m_SceneSubMeshes)
			RegisterSceneSubMesh(mesh);
		for (auto* texture : m_Textures)
			RegisterTexture(texture);
		for (auto* shader : m_Shaders)
			RegisterShader(shader);
		for (auto* material : m_Materials)
			RegisterMaterial(material);
	}

	void VansSceneAssetRegistry::ClearSceneLookup()
	{
		m_SceneSubMeshAssetLookup.clear();
		m_MaterialAssetLookup.clear();
	}

	void VansSceneAssetRegistry::ClearMeshes()
	{
		m_Meshes.clear();
		m_MeshAssetLookup.clear();
	}

	void VansSceneAssetRegistry::ClearSceneSubMeshes()
	{
		m_SceneSubMeshes.clear();
		m_SceneSubMeshAssetLookup.clear();
	}

	void VansSceneAssetRegistry::ClearShaders()
	{
		m_Shaders.clear();
		m_ShaderAssetLookup.clear();
	}

	void VansSceneAssetRegistry::ClearTextures()
	{
		m_Textures.clear();
		m_TextureAssetLookup.clear();
	}

	void VansSceneAssetRegistry::ClearMaterials()
	{
		m_Materials.clear();
		m_MaterialAssetLookup.clear();
	}

	void VansSceneAssetRegistry::RemoveSceneSubMesh(VansAsset* asset)
	{
		auto it = std::remove(m_SceneSubMeshes.begin(), m_SceneSubMeshes.end(), asset);
		m_SceneSubMeshes.erase(it, m_SceneSubMeshes.end());
		m_SceneSubMeshAssetLookup.clear();
		for (auto* subMesh : m_SceneSubMeshes)
			RegisterSceneSubMesh(subMesh);
	}
}
