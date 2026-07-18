#pragma once

#include "VansAsset.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
	class VansSceneAssetRegistry
	{
	public:
		VansAsset* FindMesh(const std::string& name);
		VansAsset* FindShader(const std::string& name);
		VansAsset* FindTexture(const std::string& name);
		VansAsset* FindMaterial(const std::string& name);

		void AddMesh(VansAsset* asset);
		void AddSceneSubMesh(VansAsset* asset);
		void AddShader(VansAsset* asset);
		void AddTexture(VansAsset* asset);
		void AddMaterial(VansAsset* asset);

		void RegisterMesh(VansAsset* asset);
		void RegisterSceneSubMesh(VansAsset* asset);
		void RegisterShader(VansAsset* asset);
		void RegisterTexture(VansAsset* asset);
		void RegisterMaterial(VansAsset* asset);

		bool HasProjectMeshAlias(const std::string& name) const;
		void SetProjectMeshAlias(const std::string& name, VansAsset* asset);
		void ClearProjectMeshAliases();

		void RebuildLookup();
		void ClearSceneLookup();

		void ClearMeshes();
		void ClearSceneSubMeshes();
		void ClearShaders();
		void ClearTextures();
		void ClearMaterials();
		void RemoveSceneSubMesh(VansAsset* asset);

		const std::vector<VansAsset*>& GetMeshes() const { return m_Meshes; }
		const std::vector<VansAsset*>& GetSceneSubMeshes() const { return m_SceneSubMeshes; }
		const std::vector<VansAsset*>& GetShaders() const { return m_Shaders; }
		const std::vector<VansAsset*>& GetTextures() const { return m_Textures; }
		const std::vector<VansAsset*>& GetMaterials() const { return m_Materials; }

	private:
		static VansAsset* FindInLookup(
			const std::unordered_map<std::string, VansAsset*>& lookup,
			const std::string& name);
		static VansAsset* FindAndBackfill(
			const std::vector<VansAsset*>& assets,
			std::unordered_map<std::string, VansAsset*>& lookup,
			const std::string& name);
		static void RegisterByName(std::unordered_map<std::string, VansAsset*>& lookup, VansAsset* asset);

		std::vector<VansAsset*> m_Meshes;
		std::unordered_map<std::string, VansAsset*> m_ProjectMeshAliases;
		std::unordered_map<std::string, VansAsset*> m_MeshAssetLookup;

		std::vector<VansAsset*> m_SceneSubMeshes;
		std::unordered_map<std::string, VansAsset*> m_SceneSubMeshAssetLookup;

		std::vector<VansAsset*> m_Textures;
		std::unordered_map<std::string, VansAsset*> m_TextureAssetLookup;

		std::vector<VansAsset*> m_Shaders;
		std::unordered_map<std::string, VansAsset*> m_ShaderAssetLookup;

		std::vector<VansAsset*> m_Materials;
		std::unordered_map<std::string, VansAsset*> m_MaterialAssetLookup;
	};
}
