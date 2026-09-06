#include "VansCameraGameplayAssetCompiler.h"

#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../CameraCore/VansCameraCore.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
namespace
{
const VansSerializedValue* At(const VansSerializedValue& root, const char* path)
{
	return FindSerializedPointer(root, path);
}

std::string StringAt(
	const VansSerializedValue& root,
	const char* path,
	std::string fallback = {})
{
	const VansSerializedValue* value = At(root, path);
	return value && value->kind == VansSerializedValue::Kind::String
		? value->stringValue : std::move(fallback);
}

double NumberAt(const VansSerializedValue& root, const char* path, double fallback)
{
	const VansSerializedValue* value = At(root, path);
	return value ? ReadSerializedNumber(*value, fallback) : fallback;
}

bool BoolAt(const VansSerializedValue& root, const char* path, bool fallback)
{
	const VansSerializedValue* value = At(root, path);
	return value && value->kind == VansSerializedValue::Kind::Bool
		? value->boolValue : fallback;
}

std::int64_t IntAt(const VansSerializedValue& root, const char* path, std::int64_t fallback)
{
	const VansSerializedValue* value = At(root, path);
	return value ? ReadSerializedInt(*value, fallback) : fallback;
}

double NumberField(
	const VansSerializedValue& object,
	const char* field,
	double fallback)
{
	const VansSerializedValue* value = FindObjectField(object, field);
	return value ? ReadSerializedNumber(*value, fallback) : fallback;
}

std::vector<std::string> StringArray(const VansSerializedValue* value)
{
	std::vector<std::string> result;
	if (!value || value->kind != VansSerializedValue::Kind::Array) return result;
	for (const VansSerializedValue& item : value->arrayItems)
		if (item.kind == VansSerializedValue::Kind::String && !item.stringValue.empty())
			result.push_back(item.stringValue);
	return result;
}

void AddDiagnostic(
	VansGameplayDiagnostics& diagnostics,
	std::string code,
	std::string message,
	std::string fieldPath)
{
	diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error,
		std::move(code), std::move(message), {}, std::move(fieldPath) });
}

VansGameplayPropertySchema SchemaField(
	std::string path,
	std::string displayName,
	std::string group,
	VansGameplayPropertyKind kind,
	VansSerializedValue defaultValue,
	bool required = false)
{
	VansGameplayPropertySchema field;
	field.fieldId = VansMakeStableId<VansActionFieldIdTag>(path);
	field.path = std::move(path);
	field.displayName = std::move(displayName);
	field.group = std::move(group);
	field.kind = kind;
	field.defaultValue = std::move(defaultValue);
	field.required = required;
	if (kind == VansGameplayPropertyKind::Array)
	{
		field.hasArrayElement = true;
		field.arrayElementKind = VansGameplayPropertyKind::Object;
		field.arrayElementDefault = VansSerializedValue::Object({});
	}
	return field;
}

VansGameplayPropertySchema SchemaChild(
	std::string stablePath,
	std::string memberName,
	std::string displayName,
	VansGameplayPropertyKind kind,
	VansSerializedValue defaultValue,
	bool required = false)
{
	VansGameplayPropertySchema field = SchemaField(
		std::move(stablePath), std::move(displayName), {}, kind,
		std::move(defaultValue), required);
	field.path = std::move(memberName);
	return field;
}

VansGameplayPropertySchema SchemaEnumChild(
	std::string stablePath,
	std::string memberName,
	std::string displayName,
	std::string defaultValue,
	std::vector<std::string> values)
{
	auto field = SchemaChild(std::move(stablePath), std::move(memberName),
		std::move(displayName), VansGameplayPropertyKind::Enum,
		VansSerializedValue::String(std::move(defaultValue)));
	field.enumValues = std::move(values);
	return field;
}

VansGameplayPropertySchema SchemaStringArrayChild(
	std::string stablePath,
	std::string memberName,
	std::string displayName)
{
	auto field = SchemaChild(std::move(stablePath), std::move(memberName),
		std::move(displayName), VansGameplayPropertyKind::Array,
		VansSerializedValue::Array({}));
	field.hasArrayElement = true;
	field.arrayElementKind = VansGameplayPropertyKind::String;
	field.arrayElementDefault = VansSerializedValue::String("");
	return field;
}

VansGameplayPropertySchema SchemaVector3Child(
	std::string stablePath,
	std::string memberName,
	std::string displayName,
	double x,
	double y,
	double z)
{
	auto field = SchemaChild(stablePath, std::move(memberName), std::move(displayName),
		VansGameplayPropertyKind::Vec3, VansSerializedValue::Object({
			{ "x", VansSerializedValue::Float(x) },
			{ "y", VansSerializedValue::Float(y) },
			{ "z", VansSerializedValue::Float(z) }
		}));
	field.children = {
		SchemaChild(stablePath + "/x", "x", "X", VansGameplayPropertyKind::Float,
			VansSerializedValue::Float(x)),
		SchemaChild(stablePath + "/y", "y", "Y", VansGameplayPropertyKind::Float,
			VansSerializedValue::Float(y)),
		SchemaChild(stablePath + "/z", "z", "Z", VansGameplayPropertyKind::Float,
			VansSerializedValue::Float(z))
	};
	return field;
}

VansGameplayAssetSchemaDescriptor CameraAssetSchema(
	VansAssetType type,
	std::string kind,
	std::string extension,
	const char* idPath)
{
	VansGameplayAssetSchemaDescriptor descriptor;
	descriptor.assetType = type;
	descriptor.assetKind = std::move(kind);
	descriptor.extension = std::move(extension);
	descriptor.fields.push_back(SchemaField("/assetKind", "Asset Kind", "Identity",
		VansGameplayPropertyKind::String,
		VansSerializedValue::String(descriptor.assetKind), true));
	descriptor.fields.back().readOnly = true;
	descriptor.fields.push_back(SchemaField(idPath, "Stable Id", "Identity",
		VansGameplayPropertyKind::String, VansSerializedValue::String(""), true));
	descriptor.fields.push_back(SchemaField("/displayName", "Display Name", "Identity",
		VansGameplayPropertyKind::String, VansSerializedValue::String("New Asset"), true));
	descriptor.fields.push_back(SchemaField("/authoringGuid", "Authoring Guid", "Identity",
		VansGameplayPropertyKind::String, VansSerializedValue::String("")));
	return descriptor;
}

bool CompileCameraProfile(
	const VansGameplayCookedAsset& cooked,
	VansCompiledGameplayAssetData& output,
	VansGameplayDiagnostics& diagnostics)
{
	const auto vectorAt = [&](const char* path, glm::vec3 fallback = glm::vec3(0.0f))
	{
		const VansSerializedValue* object = At(cooked.runtimeDocument, path);
		if (!object) return fallback;
		return glm::vec3(
			static_cast<float>(NumberField(*object, "x", fallback.x)),
			static_cast<float>(NumberField(*object, "y", fallback.y)),
			static_cast<float>(NumberField(*object, "z", fallback.z)));
	};
	if (cooked.assetType == VansAssetType::CameraRigProfile)
	{
		auto rig = std::make_shared<VansCameraRigDefinition>();
		rig->stableName = StringAt(cooked.runtimeDocument, "/cameraRigId");
		rig->id = VansMakeStableId<VansCameraRigIdTag>(rig->stableName);
		rig->follow.enabled = true;
		rig->follow.mode = StringAt(cooked.runtimeDocument, "/follow/mode", "SpringArm");
		rig->follow.targetBinding = StringAt(
			cooked.runtimeDocument, "/follow/targetBinding", "Avatar");
		rig->follow.localOffset = vectorAt("/follow/offset", { 0.0f, 1.6f, -3.0f });
		rig->follow.positionDamping = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/follow/damping", 0.15));
		rig->lookAt.enabled = BoolAt(cooked.runtimeDocument, "/lookAt/enabled", true);
		rig->lookAt.targetBinding = StringAt(
			cooked.runtimeDocument, "/lookAt/targetBinding", "Avatar");
		rig->lookAt.worldOffset = vectorAt("/lookAt/offset", { 0.0f, 1.4f, 0.0f });
		rig->lookAt.rotationDamping = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/lookAt/damping", 0.1));
		rig->collision.enabled = BoolAt(cooked.runtimeDocument, "/collision/enabled", true);
		rig->collision.radius = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/collision/probeRadius", 0.2));
		rig->collision.minimumDistance = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/collision/minimumDistance", 0.1));
		rig->collision.padding = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/collision/padding", 0.05));
		rig->collision.recoverySeconds = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/collision/recoverySeconds", 0.2));
		rig->collision.layers = StringArray(At(cooked.runtimeDocument, "/collision/layers"));
		rig->initialView.pose.position = rig->follow.localOffset;
		rig->initialView.lens.fieldOfView = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/lens/fieldOfView", 60.0));
		rig->initialView.lens.nearClip = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/lens/nearPlane", 0.1));
		rig->initialView.lens.farClip = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/lens/farPlane", 1000.0));
		rig->focusDistance = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/lens/focusDistance", 10.0));
		rig->composition.screenX = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/composition/screenX", 0.5));
		rig->composition.screenY = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/composition/screenY", 0.5));
		rig->composition.deadZoneX = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/composition/deadZoneX", 0.1));
		rig->composition.deadZoneY = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/composition/deadZoneY", 0.1));
		if (rig->initialView.lens.nearClip >= rig->initialView.lens.farClip)
		{
			AddDiagnostic(diagnostics, "GAF-CAMERA-LENS-RANGE",
				"Camera near plane must be smaller than far plane", "/lens/nearPlane");
			return false;
		}
		VansCameraRuntime validator;
		std::string error;
		if (!validator.RegisterRig(*rig, error))
		{
			AddDiagnostic(diagnostics, "GAF-CAMERA-RIG", error, "/cameraRigId");
			return false;
		}
		output = VansCompiledGameplayExtensionAsset{
			std::string(VansCameraRigGameplayAssetType), rig->stableName, std::move(rig) };
		return true;
	}

	auto shake = std::make_shared<VansCameraShakeDefinition>();
	shake->stableName = StringAt(cooked.runtimeDocument, "/cameraShakeId");
	shake->id = VansMakeStableId<VansCameraShakeIdTag>(shake->stableName);
	shake->translationAmplitude = vectorAt("/noise/translationAmplitude", glm::vec3(0.05f));
	shake->rotationAmplitude = vectorAt("/noise/rotationAmplitude", glm::vec3(0.5f));
	shake->frequency = static_cast<float>(NumberAt(cooked.runtimeDocument, "/noise/frequency", 12.0));
	shake->attackSeconds = static_cast<float>(NumberAt(cooked.runtimeDocument, "/envelope/attack", 0.05));
	shake->sustainSeconds = static_cast<float>(NumberAt(cooked.runtimeDocument, "/envelope/sustain", 0.1));
	shake->releaseSeconds = static_cast<float>(NumberAt(cooked.runtimeDocument, "/envelope/release", 0.15));
	shake->minimumDistance = static_cast<float>(NumberAt(
		cooked.runtimeDocument, "/falloff/minimumDistance", 0.0));
	shake->maximumDistance = static_cast<float>(NumberAt(
		cooked.runtimeDocument, "/falloff/maximumDistance", 25.0));
	shake->falloffExponent = static_cast<float>(NumberAt(
		cooked.runtimeDocument, "/falloff/exponent", 1.0));
	shake->seed = static_cast<std::uint64_t>((std::max<std::int64_t>)(
		0, IntAt(cooked.runtimeDocument, "/seed", 0)));
	VansCameraRuntime validator;
	std::string error;
	if (!validator.RegisterShake(*shake, error))
	{
		AddDiagnostic(diagnostics, "GAF-CAMERA-SHAKE", error, "/cameraShakeId");
		return false;
	}
	output = VansCompiledGameplayExtensionAsset{
		std::string(VansCameraShakeGameplayAssetType), shake->stableName, std::move(shake) };
	return true;
}
}

bool VansRegisterCameraGameplayAssetCompilers(
	VansGameplayAssetCompilerRegistry& registry,
	std::string& error)
{
	const auto compiler = [](const auto& cooked, auto& output, auto& diagnostics)
	{
		return CompileCameraProfile(cooked, output, diagnostics);
	};
	return registry.Register(VansAssetType::CameraRigProfile,
		std::string(VansCameraRigGameplayAssetType), compiler, error) &&
		registry.Register(VansAssetType::CameraShakeProfile,
			std::string(VansCameraShakeGameplayAssetType), compiler, error);
}

bool VansRegisterCameraGameplayAssetSchemas(
	VansGameplayAssetSchemaRegistry& registry,
	std::string& error)
{
	auto rig = CameraAssetSchema(
		VansAssetType::CameraRigProfile, "CameraRigProfile", ".vcamerarig", "/cameraRigId");
	rig.fields.push_back(SchemaField("/follow", "Follow", "Rig",
		VansGameplayPropertyKind::Object, VansSerializedValue::Object({
			{ "mode", VansSerializedValue::String("SpringArm") },
			{ "targetBinding", VansSerializedValue::String("Avatar") },
			{ "offset", VansSerializedValue::Object({
				{ "x", VansSerializedValue::Float(0.0) },
				{ "y", VansSerializedValue::Float(1.6) },
				{ "z", VansSerializedValue::Float(-3.0) } }) },
			{ "damping", VansSerializedValue::Float(0.15) }
		}), true));
	rig.fields.back().children = {
		SchemaEnumChild("/follow/mode", "mode", "Mode", "SpringArm",
			{ "Fixed", "SpringArm", "Orbit", "Rail" }),
		SchemaChild("/follow/targetBinding", "targetBinding", "Target Binding",
			VansGameplayPropertyKind::String, VansSerializedValue::String("Avatar")),
		SchemaVector3Child("/follow/offset", "offset", "Offset", 0.0, 1.6, -3.0),
		SchemaChild("/follow/damping", "damping", "Damping",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.15))
	};
	rig.fields.back().children[3].hasMinimum = true;
	rig.fields.back().children[3].minimum = 0.0;
	rig.fields.back().children[3].hasMaximum = true;
	rig.fields.back().children[3].maximum = 60.0;
	rig.fields.push_back(SchemaField("/lookAt", "Look At", "Rig",
		VansGameplayPropertyKind::Object, VansSerializedValue::Object({
			{ "enabled", VansSerializedValue::Bool(true) },
			{ "targetBinding", VansSerializedValue::String("Avatar") },
			{ "offset", VansSerializedValue::Object({
				{ "x", VansSerializedValue::Float(0.0) },
				{ "y", VansSerializedValue::Float(1.4) },
				{ "z", VansSerializedValue::Float(0.0) } }) },
			{ "damping", VansSerializedValue::Float(0.1) }
		}), true));
	rig.fields.back().children = {
		SchemaChild("/lookAt/enabled", "enabled", "Enabled",
			VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(true)),
		SchemaChild("/lookAt/targetBinding", "targetBinding", "Target Binding",
			VansGameplayPropertyKind::String, VansSerializedValue::String("Avatar")),
		SchemaVector3Child("/lookAt/offset", "offset", "Offset", 0.0, 1.4, 0.0),
		SchemaChild("/lookAt/damping", "damping", "Damping",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.1))
	};
	rig.fields.back().children[3].hasMinimum = true;
	rig.fields.back().children[3].minimum = 0.0;
	rig.fields.back().children[3].hasMaximum = true;
	rig.fields.back().children[3].maximum = 60.0;
	rig.fields.push_back(SchemaField("/collision", "Collision", "Rig",
		VansGameplayPropertyKind::Object, VansSerializedValue::Object({
			{ "enabled", VansSerializedValue::Bool(true) },
			{ "probeRadius", VansSerializedValue::Float(0.2) },
			{ "minimumDistance", VansSerializedValue::Float(0.1) },
			{ "padding", VansSerializedValue::Float(0.05) },
			{ "recoverySeconds", VansSerializedValue::Float(0.2) },
			{ "layers", VansSerializedValue::Array({}) }
		}), true));
	rig.fields.back().children = {
		SchemaChild("/collision/enabled", "enabled", "Enabled",
			VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(true)),
		SchemaChild("/collision/probeRadius", "probeRadius", "Probe Radius",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.2)),
		SchemaChild("/collision/minimumDistance", "minimumDistance", "Minimum Distance",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.1)),
		SchemaChild("/collision/padding", "padding", "Padding",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.05)),
		SchemaChild("/collision/recoverySeconds", "recoverySeconds", "Recovery",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.2)),
		SchemaStringArrayChild("/collision/layers", "layers", "Collision Layers")
	};
	for (std::size_t index : { std::size_t(1), std::size_t(2),
		std::size_t(3), std::size_t(4) })
	{
		rig.fields.back().children[index].hasMinimum = true;
		rig.fields.back().children[index].minimum = 0.0;
	}
	rig.fields.push_back(SchemaField("/lens", "Lens", "Rig",
		VansGameplayPropertyKind::Object, VansSerializedValue::Object({
			{ "fieldOfView", VansSerializedValue::Float(60.0) },
			{ "nearPlane", VansSerializedValue::Float(0.1) },
			{ "farPlane", VansSerializedValue::Float(1000.0) },
			{ "focusDistance", VansSerializedValue::Float(10.0) }
		}), true));
	rig.fields.back().children = {
		SchemaChild("/lens/fieldOfView", "fieldOfView", "Field Of View",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(60.0)),
		SchemaChild("/lens/nearPlane", "nearPlane", "Near Plane",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.1)),
		SchemaChild("/lens/farPlane", "farPlane", "Far Plane",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(1000.0)),
		SchemaChild("/lens/focusDistance", "focusDistance", "Focus Distance",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(10.0))
	};
	rig.fields.back().children[0].hasMinimum = true;
	rig.fields.back().children[0].minimum = 1.0;
	rig.fields.back().children[0].hasMaximum = true;
	rig.fields.back().children[0].maximum = 179.0;
	rig.fields.back().children[1].hasMinimum = true;
	rig.fields.back().children[1].minimum = 0.001;
	rig.fields.back().children[2].hasMinimum = true;
	rig.fields.back().children[2].minimum = 0.002;
	rig.fields.back().children[2].hasMaximum = true;
	rig.fields.back().children[2].maximum = 1000000.0;
	rig.fields.back().children[3].hasMinimum = true;
	rig.fields.back().children[3].minimum = 0.0;
	rig.fields.push_back(SchemaField("/composition", "Composition", "Rig",
		VansGameplayPropertyKind::Object, VansSerializedValue::Object({
			{ "screenX", VansSerializedValue::Float(0.5) },
			{ "screenY", VansSerializedValue::Float(0.5) },
			{ "deadZoneX", VansSerializedValue::Float(0.1) },
			{ "deadZoneY", VansSerializedValue::Float(0.1) }
		})));
	rig.fields.back().children = {
		SchemaChild("/composition/screenX", "screenX", "Screen X",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.5)),
		SchemaChild("/composition/screenY", "screenY", "Screen Y",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.5)),
		SchemaChild("/composition/deadZoneX", "deadZoneX", "Dead Zone X",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.1)),
		SchemaChild("/composition/deadZoneY", "deadZoneY", "Dead Zone Y",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.1))
	};
	for (VansGameplayPropertySchema& field : rig.fields.back().children)
	{
		field.hasMinimum = true;
		field.minimum = 0.0;
		field.hasMaximum = true;
		field.maximum = 1.0;
	}
	if (!registry.Register(std::move(rig), error)) return false;

	auto shake = CameraAssetSchema(VansAssetType::CameraShakeProfile,
		"CameraShakeProfile", ".vcamerashake", "/cameraShakeId");
	shake.fields.push_back(SchemaField("/noise", "Noise", "Shake",
		VansGameplayPropertyKind::Object, VansSerializedValue::Object({
			{ "translationAmplitude", VansSerializedValue::Object({
				{ "x", VansSerializedValue::Float(0.05) },
				{ "y", VansSerializedValue::Float(0.05) },
				{ "z", VansSerializedValue::Float(0.05) } }) },
			{ "rotationAmplitude", VansSerializedValue::Object({
				{ "x", VansSerializedValue::Float(0.5) },
				{ "y", VansSerializedValue::Float(0.5) },
				{ "z", VansSerializedValue::Float(0.5) } }) },
			{ "frequency", VansSerializedValue::Float(12.0) }
		}), true));
	shake.fields.back().children = {
		SchemaVector3Child("/noise/translationAmplitude", "translationAmplitude",
			"Translation", 0.05, 0.05, 0.05),
		SchemaVector3Child("/noise/rotationAmplitude", "rotationAmplitude",
			"Rotation", 0.5, 0.5, 0.5),
		SchemaChild("/noise/frequency", "frequency", "Frequency",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(12.0))
	};
	for (std::size_t vectorIndex : { std::size_t(0), std::size_t(1) })
		for (VansGameplayPropertySchema& axis : shake.fields.back().children[vectorIndex].children)
		{
			axis.hasMinimum = true;
			axis.minimum = 0.0;
		}
	shake.fields.back().children[2].hasMinimum = true;
	shake.fields.back().children[2].minimum = 0.001;
	shake.fields.back().children[2].hasMaximum = true;
	shake.fields.back().children[2].maximum = 1000.0;
	shake.fields.push_back(SchemaField("/envelope", "Envelope", "Shake",
		VansGameplayPropertyKind::Object, VansSerializedValue::Object({
			{ "attack", VansSerializedValue::Float(0.05) },
			{ "sustain", VansSerializedValue::Float(0.1) },
			{ "release", VansSerializedValue::Float(0.15) }
		}), true));
	shake.fields.back().children = {
		SchemaChild("/envelope/attack", "attack", "Attack",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.05)),
		SchemaChild("/envelope/sustain", "sustain", "Sustain",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.1)),
		SchemaChild("/envelope/release", "release", "Release",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.15))
	};
	for (VansGameplayPropertySchema& field : shake.fields.back().children)
	{
		field.hasMinimum = true;
		field.minimum = 0.0;
		field.hasMaximum = true;
		field.maximum = 3600.0;
	}
	shake.fields.push_back(SchemaField("/falloff", "Falloff", "Shake",
		VansGameplayPropertyKind::Object, VansSerializedValue::Object({
			{ "minimumDistance", VansSerializedValue::Float(0.0) },
			{ "maximumDistance", VansSerializedValue::Float(25.0) },
			{ "exponent", VansSerializedValue::Float(1.0) }
		})));
	shake.fields.back().children = {
		SchemaChild("/falloff/minimumDistance", "minimumDistance", "Minimum Distance",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0)),
		SchemaChild("/falloff/maximumDistance", "maximumDistance", "Maximum Distance",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(25.0)),
		SchemaChild("/falloff/exponent", "exponent", "Exponent",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(1.0))
	};
	for (VansGameplayPropertySchema& field : shake.fields.back().children)
	{
		field.hasMinimum = true;
		field.minimum = 0.0;
	}
	shake.fields.back().children[2].minimum = 0.001;
	shake.fields.push_back(SchemaField("/seed", "Seed", "Shake",
		VansGameplayPropertyKind::Int, VansSerializedValue::Int(0)));
	shake.fields.back().hasMinimum = true;
	shake.fields.back().minimum = 0.0;
	return registry.Register(std::move(shake), error);
}
}
