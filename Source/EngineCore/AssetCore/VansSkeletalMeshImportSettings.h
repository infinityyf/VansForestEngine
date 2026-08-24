#pragma once

#include <string>
#include <unordered_map>

namespace Vans
{
struct VansAssetMeta;

struct VansLegacySkeletalImportFixups
{
	bool repairInvalidIdentityBindPose = false;
	bool remapWeaponAttachmentsToHands = false;
	bool nearestBoneRigidBind = false;
};

struct VansSkeletalMeshImportSettings
{
	// 骨架资产身份与模型 meta 中的稳定骨骼子资产。运行时只能把这些 GUID
	// 编译为当前 Skeleton index，场景和编辑器不得持久化数组下标。
	std::string sourceSkeletonGuid;
	std::unordered_map<std::string, std::string> boneGuidByCanonicalPath;

	// Preferred source for inverse bind matrices.
	// "fbxCluster" preserves offsets authored by the source file.
	// "hierarchy" rebuilds inverse-binds from the imported bone node hierarchy.
	// This is useful for FBX files whose cluster offsets are authored in a space
	// that is incompatible with the engine's single-skeleton skinning path.
	std::string bindPoseSource = "fbxCluster";

	// How mesh-node transforms are reconciled with vertex positions and skinning.
	// "auto" bakes skinned mesh-node transforms when the FBX actually uses
	// non-identity mesh nodes for skinned submeshes; otherwise it preserves the
	// authored Assimp vertex/bind space.
	// "bakeSkinned" additionally bakes skinned mesh-node transforms into vertices
	// and normalizes matching inverse-bind offsets into the same model space.
	// This is a generic multi-skinned-mesh FBX compatibility mode, not a legacy
	// asset-specific fixup.
	std::string meshNodeTransformPolicy = "auto";

	// How unskinned meshes parented under bones should be represented.
	// "preserveNodeOffset" rigidly binds such meshes to the nearest ancestor bone
	// while preserving their authored node offset. The old nearest-bone switch is
	// isolated under legacyFixups as a metadata compatibility alias.
	std::string rigidAttachmentPolicy = "preserveNodeOffset";

	bool diagnostics = false;
	VansLegacySkeletalImportFixups legacyFixups;
};

// Single metadata projection used by scene loading, packaging and isolated
// editor previews. Keeping this translation in AssetCore prevents each
// consumer from growing subtly different import behavior.
VansSkeletalMeshImportSettings ReadSkeletalMeshImportSettings(const VansAssetMeta& meta);
}
