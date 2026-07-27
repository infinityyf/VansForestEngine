#include "VansPostProcessProfileLegacyJsonCodec.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <utility>

namespace VansGraphics
{
namespace
{
const PostProcessProfileJson& SectionOrEmpty(const PostProcessProfileJson& root, const char* key)
{
	static const PostProcessProfileJson empty = PostProcessProfileJson::object();
	const auto it = root.find(key);
	return it != root.end() && it->is_object() ? *it : empty;
}
}

PostProcessProfileJson VansPostProcessProfileLegacyJsonCodec::Encode(const VansPostProcessProfile& profile)
{
	const VansPostProcessProfile& p = profile;
	PostProcessProfileJson root;
	root["version"] = VansPostProcessProfile::PROFILE_VERSION;

	root["general"]["enablePostProcess"] = p.m_EnablePostProcess;
	root["general"]["enableHDR"] = p.m_EnableHDR;

	root["exposure"]["enableAutoExposure"] = p.m_EnableAutoExposure;
	root["exposure"]["exposureCompensation"] = p.m_ExposureCompensation;
	root["exposure"]["minEV100"] = p.m_MinEV100;
	root["exposure"]["maxEV100"] = p.m_MaxEV100;
	root["exposure"]["adaptationSpeedUp"] = p.m_AdaptationSpeedUp;
	root["exposure"]["adaptationSpeedDown"] = p.m_AdaptationSpeedDown;

	root["bloom"]["enable"] = p.m_EnableBloom;
	root["bloom"]["threshold"] = p.m_BloomThreshold;
	root["bloom"]["knee"] = p.m_BloomKnee;
	root["bloom"]["intensity"] = p.m_BloomIntensity;
	root["bloom"]["scatter"] = p.m_BloomScatter;
	root["bloom"]["clamp"] = p.m_BloomClamp;

	root["toneMapping"]["type"] = p.m_ToneMapperType;
	root["toneMapping"]["whitePoint"] = p.m_WhitePoint;

	root["colorGrading"]["enable"] = p.m_EnableColorGrading;
	root["colorGrading"]["contrast"] = p.m_Contrast;
	root["colorGrading"]["saturation"] = p.m_Saturation;
	root["colorGrading"]["hueShift"] = p.m_HueShift;
	root["colorGrading"]["temperature"] = p.m_Temperature;
	root["colorGrading"]["tint"] = p.m_Tint;

	root["dof"]["enable"] = p.m_EnableDOF;
	root["dof"]["focusDistance"] = p.m_FocusDistance;
	root["dof"]["focusRange"] = p.m_FocusRange;
	root["dof"]["aperture"] = p.m_Aperture;
	root["dof"]["maxCoC"] = p.m_MaxCoC;

	root["motionBlur"]["enable"] = p.m_EnableMotionBlur;
	root["motionBlur"]["shutter"] = p.m_ShutterScale;
	root["motionBlur"]["samples"] = p.m_MotionBlurSamples;

	root["chromaticAberration"]["enable"] = p.m_EnableChromaticAberration;
	root["chromaticAberration"]["intensity"] = p.m_ChromaticAberrationIntensity;

	root["aa"]["enableSharpen"] = p.m_EnableSharpen;
	root["aa"]["sharpenIntensity"] = p.m_SharpenIntensity;
	return root;
}

bool VansPostProcessProfileLegacyJsonCodec::Decode(
	const PostProcessProfileJson& root,
	const std::filesystem::path& filePath,
	VansPostProcessProfile& profile,
	std::string& error)
{
	try
	{
		VansPostProcessProfile decoded;
		const PostProcessProfileJson& general = SectionOrEmpty(root, "general");
		const PostProcessProfileJson& exposure = SectionOrEmpty(root, "exposure");
		const PostProcessProfileJson& bloom = SectionOrEmpty(root, "bloom");
		const PostProcessProfileJson& toneMapping = SectionOrEmpty(root, "toneMapping");
		const PostProcessProfileJson& colorGrading = SectionOrEmpty(root, "colorGrading");
		const PostProcessProfileJson& dof = SectionOrEmpty(root, "dof");
		const PostProcessProfileJson& motionBlur = SectionOrEmpty(root, "motionBlur");
		const PostProcessProfileJson& chromaticAberration =
			SectionOrEmpty(root, "chromaticAberration");
		const PostProcessProfileJson& aa = SectionOrEmpty(root, "aa");

		decoded.m_EnablePostProcess = general.value("enablePostProcess", true);
		decoded.m_EnableHDR = general.value("enableHDR", true);

		decoded.m_EnableAutoExposure = exposure.value("enableAutoExposure", true);
		decoded.m_ExposureCompensation = exposure.value("exposureCompensation", 0.0f);
		decoded.m_MinEV100 = exposure.value("minEV100", -6.0f);
		decoded.m_MaxEV100 = exposure.value("maxEV100", 16.0f);
		decoded.m_AdaptationSpeedUp = exposure.value("adaptationSpeedUp", 3.0f);
		decoded.m_AdaptationSpeedDown = exposure.value("adaptationSpeedDown", 1.0f);

		decoded.m_EnableBloom = bloom.value("enable", true);
		decoded.m_BloomThreshold = bloom.value("threshold", 1.0f);
		decoded.m_BloomKnee = bloom.value("knee", 0.5f);
		decoded.m_BloomIntensity = bloom.value("intensity", 0.12f);
		decoded.m_BloomScatter = bloom.value("scatter", 0.7f);
		decoded.m_BloomClamp = bloom.value("clamp", 64.0f);

		decoded.m_ToneMapperType = toneMapping.value("type", 1);
		decoded.m_WhitePoint = toneMapping.value("whitePoint", 11.2f);

		decoded.m_EnableColorGrading = colorGrading.value("enable", true);
		decoded.m_Contrast = colorGrading.value("contrast", 1.0f);
		decoded.m_Saturation = colorGrading.value("saturation", 1.0f);
		decoded.m_HueShift = colorGrading.value("hueShift", 0.0f);
		decoded.m_Temperature = colorGrading.value("temperature", 0.0f);
		decoded.m_Tint = colorGrading.value("tint", 0.0f);

		decoded.m_EnableDOF = dof.value("enable", false);
		decoded.m_FocusDistance = dof.value("focusDistance", 5.0f);
		decoded.m_FocusRange = dof.value("focusRange", 2.0f);
		decoded.m_Aperture = dof.value("aperture", 2.8f);
		decoded.m_MaxCoC = dof.value("maxCoC", 12.0f);

		decoded.m_EnableMotionBlur = motionBlur.value("enable", false);
		decoded.m_ShutterScale = motionBlur.value("shutter", 0.5f);
		decoded.m_MotionBlurSamples = motionBlur.value("samples", 12);

		decoded.m_EnableChromaticAberration = chromaticAberration.value("enable", false);
		decoded.m_ChromaticAberrationIntensity =
			chromaticAberration.value("intensity", 0.02f);

		decoded.m_EnableSharpen = aa.value("enableSharpen", true);
		decoded.m_SharpenIntensity = aa.value("sharpenIntensity", 0.15f);
		decoded.m_IsDirty = true;

		profile = std::move(decoded);
		return true;
	}
	catch (const std::exception& exception)
	{
		error = "Invalid post-process profile JSON " + filePath.string() + ": " + exception.what();
		return false;
	}
}
}
