#include "VansSkinProfileJsonCodec.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace Vans
{
namespace
{
SkinProfileJson Vec3Json(const glm::vec3& value)
{
	return SkinProfileJson::array({ value.x, value.y, value.z });
}

glm::vec3 ReadVec3(const SkinProfileJson& object, const char* key, const glm::vec3& fallback)
{
	if (!object.contains(key) || !object[key].is_array() || object[key].size() < 3)
		return fallback;
	return glm::vec3(
		object[key][0].get<float>(),
		object[key][1].get<float>(),
		object[key][2].get<float>());
}

float ReadFloat(const SkinProfileJson& object, const char* key, float fallback)
{
	return object.contains(key) && object[key].is_number()
		? object[key].get<float>()
		: fallback;
}

int32_t ReadInt(const SkinProfileJson& object, const char* key, int32_t fallback)
{
	return object.contains(key) && object[key].is_number()
		? static_cast<int32_t>(object[key].get<float>())
		: fallback;
}

std::string ReadString(const SkinProfileJson& object, const char* key, const std::string& fallback)
{
	return object.contains(key) && object[key].is_string()
		? object[key].get<std::string>()
		: fallback;
}

const SkinProfileJson& SectionOrEmpty(const SkinProfileJson& root, const char* key)
{
	static const SkinProfileJson empty = SkinProfileJson::object();
	return root.contains(key) && root[key].is_object() ? root[key] : empty;
}
}

SkinProfileJson VansSkinProfileJsonCodec::Encode(const VansSkinProfile& profile)
{
	SkinProfileJson root;
	root["version"] = VansSkinProfile::PROFILE_VERSION;
	root["name"] = profile.name;
	root["description"] = profile.description;
	root["basePreset"] = profile.basePreset;

	root["scattering"]["scatterColor"] = Vec3Json(profile.scatterColor);
	root["scattering"]["scatterAmount"] = profile.scatterAmount;
	root["scattering"]["diffusionRadiusScale"] = profile.diffusionRadiusScale;
	root["scattering"]["ambientScatterScale"] = profile.ambientScatterScale;
	root["scattering"]["scatterRadiusScale"] = Vec3Json(profile.scatterRadiusScale);
	root["scattering"]["boundaryColorBleed"] = profile.boundaryColorBleed;
	root["scattering"]["profileLutLayer"] = std::clamp(profile.profileLutLayer, -1, 15);

	root["surface"]["roughness"] = profile.roughness;
	root["surface"]["normalStrength"] = profile.normalStrength;
	root["surface"]["specularScale"] = profile.specularScale;
	root["surface"]["primaryRoughnessScale"] = profile.primaryRoughnessScale;
	root["surface"]["secondaryRoughnessScale"] = profile.secondaryRoughnessScale;
	root["surface"]["skinIor"] = profile.skinIor;
	root["surface"]["specularLobeMix"] = profile.specularLobeMix;

	root["transmission"]["transmissionScale"] = profile.transmissionScale;
	root["transmission"]["thinnessScale"] = profile.thinnessScale;
	root["transmission"]["transmissionDepthScale"] = profile.transmissionDepthScale;
	return root;
}

bool VansSkinProfileJsonCodec::Decode(
	const SkinProfileJson& root,
	const std::filesystem::path& filePath,
	VansSkinProfile& profile,
	std::string& error)
{
	try
	{
		VansSkinProfile decoded;
		decoded.name = ReadString(root, "name", decoded.name);
		decoded.description = ReadString(root, "description", decoded.description);
		decoded.basePreset = ReadString(root, "basePreset", decoded.basePreset);

		const SkinProfileJson& scattering = SectionOrEmpty(root, "scattering");
		const SkinProfileJson& surface = SectionOrEmpty(root, "surface");
		const SkinProfileJson& transmission = SectionOrEmpty(root, "transmission");

		glm::vec3 legacyScatterColor = ReadVec3(
			scattering,
			"subsurfaceColor",
			ReadVec3(scattering, "sssColor",
				ReadVec3(root, "subsurfaceColor",
					ReadVec3(root, "sssColor", decoded.scatterColor))));
		decoded.scatterColor = ReadVec3(
			scattering,
			"scatterColor",
			ReadVec3(root, "scatterColor", legacyScatterColor));
		float legacyScatterAmount = ReadFloat(
			scattering,
			"subsurfaceAmount",
			ReadFloat(scattering, "sssAmount",
				ReadFloat(root, "subsurfaceAmount",
					ReadFloat(root, "sssAmount", decoded.scatterAmount))));
		decoded.scatterAmount = ReadFloat(
			scattering,
			"scatterAmount",
			ReadFloat(root, "scatterAmount", legacyScatterAmount));
		decoded.diffusionRadiusScale = ReadFloat(
			scattering,
			"diffusionRadiusScale",
			ReadFloat(root, "diffusionRadiusScale", decoded.diffusionRadiusScale));
		decoded.ambientScatterScale = ReadFloat(
			scattering,
			"ambientScatterScale",
			ReadFloat(root, "ambientScatterScale", decoded.ambientScatterScale));
		glm::vec3 scatterRadiusScale = ReadVec3(root, "scatterRadiusScale", decoded.scatterRadiusScale);
		scatterRadiusScale = ReadVec3(root, "scatterRadiusRGB", scatterRadiusScale);
		scatterRadiusScale = ReadVec3(root, "profileScatterRadius", scatterRadiusScale);
		scatterRadiusScale = ReadVec3(scattering, "scatterRadiusScale", scatterRadiusScale);
		scatterRadiusScale = ReadVec3(scattering, "scatterRadiusRGB", scatterRadiusScale);
		decoded.scatterRadiusScale = ReadVec3(scattering, "profileScatterRadius", scatterRadiusScale);

		float boundaryColorBleed = ReadFloat(root, "boundaryColorBleed", decoded.boundaryColorBleed);
		boundaryColorBleed = ReadFloat(root, "skinBoundaryBleed", boundaryColorBleed);
		boundaryColorBleed = ReadFloat(scattering, "boundaryColorBleed", boundaryColorBleed);
		decoded.boundaryColorBleed = ReadFloat(scattering, "skinBoundaryBleed", boundaryColorBleed);

		int32_t profileLutLayer = ReadInt(root, "profileLutLayer", decoded.profileLutLayer);
		profileLutLayer = ReadInt(root, "skinProfileLutLayer", profileLutLayer);
		profileLutLayer = ReadInt(root, "skinLutLayer", profileLutLayer);
		profileLutLayer = ReadInt(scattering, "profileLutLayer", profileLutLayer);
		profileLutLayer = ReadInt(scattering, "skinProfileLutLayer", profileLutLayer);
		decoded.profileLutLayer = std::clamp(ReadInt(scattering, "skinLutLayer", profileLutLayer), -1, 15);

		decoded.roughness = ReadFloat(surface, "roughness", ReadFloat(root, "roughness", decoded.roughness));
		decoded.normalStrength = ReadFloat(
			surface,
			"normalStrength",
			ReadFloat(root, "normalStrength", decoded.normalStrength));
		decoded.specularScale = ReadFloat(
			surface,
			"specularScale",
			ReadFloat(root, "specularScale", decoded.specularScale));
		decoded.primaryRoughnessScale = ReadFloat(
			surface,
			"primaryRoughnessScale",
			ReadFloat(root, "primaryRoughnessScale", decoded.primaryRoughnessScale));
		decoded.secondaryRoughnessScale = ReadFloat(
			surface,
			"secondaryRoughnessScale",
			ReadFloat(root, "secondaryRoughnessScale", decoded.secondaryRoughnessScale));
		decoded.skinIor = ReadFloat(surface, "skinIor", ReadFloat(root, "skinIor", decoded.skinIor));
		decoded.specularLobeMix = ReadFloat(
			surface,
			"specularLobeMix",
			ReadFloat(root, "specularLobeMix", decoded.specularLobeMix));

		decoded.transmissionScale = ReadFloat(
			transmission,
			"transmissionScale",
			ReadFloat(root, "transmissionScale", decoded.transmissionScale));
		decoded.thinnessScale = ReadFloat(
			transmission,
			"thinnessScale",
			ReadFloat(root, "thinnessScale", decoded.thinnessScale));
		decoded.transmissionDepthScale = ReadFloat(
			transmission,
			"transmissionDepthScale",
			ReadFloat(root, "transmissionDepthScale", decoded.transmissionDepthScale));

		profile = std::move(decoded);
		return true;
	}
	catch (const std::exception& exception)
	{
		error = "Invalid skin profile JSON " + filePath.string() + ": " + exception.what();
		return false;
	}
}
}
