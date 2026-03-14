#pragma once
#include <string>
#include <vector>

// Forward declarations for Assimp types
struct aiScene;
struct aiMesh;
struct aiNode;
struct aiMaterial;
enum aiTextureType;

namespace VansGraphics
{
	// ── FBX/OBJ sub-mesh material info extracted by Assimp ──────────────────
	// Populated during LoadMultiMesh so the scene loader can auto-create
	// materials and textures without manual JSON entries.
	struct FBXSubmeshMaterialInfo
	{
		std::string materialName;        // aiMaterial name (e.g. "car_chassis")

		// Texture file paths extracted from aiMaterial (empty if not present)
		std::string diffuseTexPath;      // aiTextureType_DIFFUSE
		std::string normalTexPath;       // aiTextureType_NORMALS / aiTextureType_HEIGHT
		std::string metallicTexPath;     // aiTextureType_METALNESS
		std::string roughnessTexPath;    // aiTextureType_DIFFUSE_ROUGHNESS / aiTextureType_SHININESS
		std::string aoTexPath;           // aiTextureType_AMBIENT_OCCLUSION / aiTextureType_LIGHTMAP
		std::string opacityTexPath;      // aiTextureType_OPACITY

		// Basic material parameters from aiMaterial
		float opacity = 1.0f;            // AI_MATKEY_OPACITY
		float metallic = 0.0f;
		float roughness = 0.5f;

		// Returns true if the submesh should use a transparent material
		bool IsTransparent() const
		{
			return !opacityTexPath.empty() || opacity < 0.99f;
		}
	};
}
