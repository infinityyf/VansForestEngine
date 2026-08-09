#include "VansSkeletalMeshImportSettings.h"

#include "Serialization/VansSerializedValueAccess.h"
#include "VansAssetMeta.h"

namespace Vans
{
VansSkeletalMeshImportSettings ReadSkeletalMeshImportSettings(const VansAssetMeta& meta)
{
	VansSkeletalMeshImportSettings settings;
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
		if (settings.rigidAttachmentPolicy == "legacyNearestBone")
		{
			settings.legacyFixups.nearestBoneRigidBind = true;
			settings.rigidAttachmentPolicy = "preserveNodeOffset";
		}
		return settings;
	}

	// Read-only compatibility for existing model metadata. Newly written model
	// metadata uses the structured skeletalImport object above.
	const bool hasLegacyBindFixup =
		FindObjectField(snapshot, "rebuildIdentityBoneOffsetsFromHierarchy") != nullptr;
	const bool hasLegacyWeaponFixup =
		FindObjectField(snapshot, "remapWeaponAttachmentBonesToHands") != nullptr;
	if (hasLegacyBindFixup || hasLegacyWeaponFixup)
	{
		settings.legacyFixups.nearestBoneRigidBind = true;
		settings.legacyFixups.repairInvalidIdentityBindPose = meta.ReadBoolSetting(
			"rebuildIdentityBoneOffsetsFromHierarchy",
			settings.legacyFixups.repairInvalidIdentityBindPose);
		settings.legacyFixups.remapWeaponAttachmentsToHands = meta.ReadBoolSetting(
			"remapWeaponAttachmentBonesToHands",
			settings.legacyFixups.remapWeaponAttachmentsToHands);
	}
	return settings;
}
}
