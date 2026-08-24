#include "VansSkeletalMeshImportSettings.h"

#include "Serialization/VansSerializedValueAccess.h"
#include "VansAssetMeta.h"

namespace Vans
{
VansSkeletalMeshImportSettings ReadSkeletalMeshImportSettings(const VansAssetMeta& meta)
{
	VansSkeletalMeshImportSettings settings;
	settings.sourceSkeletonGuid = meta.guid.ToString();
	for (const auto& [fingerprint, guid] : meta.subAssets)
	{
		constexpr const char* prefix = "bone:";
		if (fingerprint.rfind(prefix, 0) == 0)
			settings.boneGuidByCanonicalPath.emplace(fingerprint.substr(5), guid.ToString());
	}
	const VansSerializedValue snapshot = meta.SerializedSettingsSnapshot();
	const VansSerializedValue* skeletal = FindObjectField(snapshot, "skeletalImport");
	if (skeletal && skeletal->kind == VansSerializedValue::Kind::Object)
	{
		settings.bindPoseSource = ReadSerializedStringField(
			*skeletal, "bindPoseSource", settings.bindPoseSource);
		settings.meshNodeTransformPolicy = ReadSerializedStringField(
			*skeletal, "meshNodeTransformPolicy", settings.meshNodeTransformPolicy);
		settings.rigidAttachmentPolicy = ReadSerializedStringField(
			*skeletal, "rigidAttachmentPolicy", settings.rigidAttachmentPolicy);
		settings.diagnostics = ReadSerializedBoolField(
			*skeletal, "diagnostics", settings.diagnostics);

		const VansSerializedValue* legacy = FindObjectField(*skeletal, "legacyFixups");
		if (legacy && legacy->kind == VansSerializedValue::Kind::Object)
		{
			settings.legacyFixups.repairInvalidIdentityBindPose = ReadSerializedBoolField(
				*legacy, "repairInvalidIdentityBindPose",
				settings.legacyFixups.repairInvalidIdentityBindPose);
			settings.legacyFixups.remapWeaponAttachmentsToHands = ReadSerializedBoolField(
				*legacy, "remapWeaponAttachmentsToHands",
				settings.legacyFixups.remapWeaponAttachmentsToHands);
			settings.legacyFixups.nearestBoneRigidBind = ReadSerializedBoolField(
				*legacy, "nearestBoneRigidBind",
				settings.legacyFixups.nearestBoneRigidBind);
		}
		return settings;
	}
	return settings;
}
}
