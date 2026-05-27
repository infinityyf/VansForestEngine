#include "VansPostProcessProfile.h"
#include "VansGraphicsDevice.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace VansGraphics
{
	// ============================================================
	// GPU 参数打包
	// ============================================================

	VansPostProcessParamsGPU VansPostProcessProfile::ToGPUParams(float time) const
	{
		VansPostProcessParamsGPU p;
		p.m_ExposureCompensation = m_ExposureCompensation;
		p.m_BloomIntensity       = m_EnableBloom ? m_BloomIntensity : 0.0f;
		p.m_BloomScatter         = m_BloomScatter;
		p.m_ToneMapperType       = m_ToneMapperType;
		p.m_WhitePoint           = m_WhitePoint;
		p.m_EnableColorGrading   = m_EnableColorGrading ? 1 : 0;
		p.m_Contrast             = m_Contrast;
		p.m_Saturation           = m_Saturation;
		p.m_HueShift             = m_HueShift;
		p.m_Temperature          = m_Temperature;
		p.m_Tint                 = m_Tint;
		p.m_EnableVignette       = m_EnableVignette ? 1 : 0;
		p.m_VignetteIntensity    = m_VignetteIntensity;
		p.m_VignetteSmoothness   = m_VignetteSmoothness;
		p.m_EnableFilmGrain      = m_EnableFilmGrain ? 1 : 0;
		p.m_FilmGrainIntensity   = m_FilmGrainIntensity;
		p.m_Time                 = time;
		p.m_EnableDithering      = m_EnableDithering ? 1 : 0;
		return p;
	}

	VansExposureAdaptParamsGPU VansPostProcessProfile::ToExposureAdaptParams(float deltaTime) const
	{
		VansExposureAdaptParamsGPU p;
		p.m_MinEV100              = m_MinEV100;
		p.m_MaxEV100              = m_MaxEV100;
		p.m_AdaptationSpeedUp     = m_AdaptationSpeedUp;
		p.m_AdaptationSpeedDown   = m_AdaptationSpeedDown;
		p.m_DeltaTime             = deltaTime;
		p.m_ExposureCompensation  = m_ExposureCompensation;
		p.m_EnableAutoExposure    = m_EnableAutoExposure ? 1 : 0;
		return p;
	}

	VansBloomParamsGPU VansPostProcessProfile::ToBloomParams() const
	{
		VansBloomParamsGPU p;
		p.m_Threshold = m_BloomThreshold;
		p.m_Knee      = m_BloomKnee;
		p.m_Intensity = m_BloomIntensity;
		p.m_Scatter   = m_BloomScatter;
		return p;
	}

	// ============================================================
	// 序列化
	// ============================================================

	bool VansPostProcessProfile::SaveToFile(const std::string& filePath) const
	{
		json j;
		j["version"] = PROFILE_VERSION;

		j["general"]["enablePostProcess"] = m_EnablePostProcess;
		j["general"]["enableHDR"]         = m_EnableHDR;

		j["exposure"]["enableAutoExposure"]    = m_EnableAutoExposure;
		j["exposure"]["exposureCompensation"]  = m_ExposureCompensation;
		j["exposure"]["minEV100"]              = m_MinEV100;
		j["exposure"]["maxEV100"]              = m_MaxEV100;
		j["exposure"]["adaptationSpeedUp"]     = m_AdaptationSpeedUp;
		j["exposure"]["adaptationSpeedDown"]   = m_AdaptationSpeedDown;

		j["bloom"]["enable"]    = m_EnableBloom;
		j["bloom"]["threshold"] = m_BloomThreshold;
		j["bloom"]["knee"]      = m_BloomKnee;
		j["bloom"]["intensity"] = m_BloomIntensity;
		j["bloom"]["scatter"]   = m_BloomScatter;
		j["bloom"]["clamp"]     = m_BloomClamp;

		j["toneMapping"]["type"]       = m_ToneMapperType;
		j["toneMapping"]["whitePoint"] = m_WhitePoint;

		j["colorGrading"]["enable"]      = m_EnableColorGrading;
		j["colorGrading"]["contrast"]    = m_Contrast;
		j["colorGrading"]["saturation"]  = m_Saturation;
		j["colorGrading"]["hueShift"]    = m_HueShift;
		j["colorGrading"]["temperature"] = m_Temperature;
		j["colorGrading"]["tint"]        = m_Tint;

		j["dof"]["enable"]        = m_EnableDOF;
		j["dof"]["focusDistance"] = m_FocusDistance;
		j["dof"]["focusRange"]    = m_FocusRange;
		j["dof"]["aperture"]      = m_Aperture;
		j["dof"]["maxCoC"]        = m_MaxCoC;

		j["motionBlur"]["enable"]   = m_EnableMotionBlur;
		j["motionBlur"]["shutter"]  = m_ShutterScale;
		j["motionBlur"]["samples"]  = m_MotionBlurSamples;

		j["vignette"]["enable"]      = m_EnableVignette;
		j["vignette"]["intensity"]   = m_VignetteIntensity;
		j["vignette"]["smoothness"]  = m_VignetteSmoothness;

		j["chromaticAberration"]["enable"]    = m_EnableChromaticAberration;
		j["chromaticAberration"]["intensity"] = m_ChromaticAberrationIntensity;

		j["filmGrain"]["enable"]    = m_EnableFilmGrain;
		j["filmGrain"]["intensity"] = m_FilmGrainIntensity;

		j["lensDirt"]["enable"]    = m_EnableLensDirt;
		j["lensDirt"]["intensity"] = m_LensDirtIntensity;

		j["aa"]["enableFXAA"]   = m_EnableFXAA;
		j["aa"]["enableSharpen"] = m_EnableSharpen;
		j["aa"]["sharpenIntensity"] = m_SharpenIntensity;

		j["dithering"]["enable"] = m_EnableDithering;

		fs::path outPath(filePath);
		if (outPath.has_parent_path())
		{
			fs::create_directories(outPath.parent_path());
		}

		std::ofstream out(filePath);
		if (!out.is_open())
		{
			return false;
		}
		out << j.dump(4);
		return true;
	}

	// ============================================================
	// 反序列化
	// ============================================================

	bool VansPostProcessProfile::LoadFromFile(const std::string& filePath)
	{
		std::ifstream in(filePath);
		if (!in.is_open())
		{
			return false;
		}

		json j;
		try
		{
			j = json::parse(in);
		}
		catch (const json::exception&)
		{
			return false;
		}

		// 缺字段时回退到默认值
		m_EnablePostProcess      = j["general"].value("enablePostProcess", true);
		m_EnableHDR              = j["general"].value("enableHDR",         true);

		m_EnableAutoExposure     = j["exposure"].value("enableAutoExposure",   true);
		m_ExposureCompensation   = j["exposure"].value("exposureCompensation", 0.0f);
		m_MinEV100               = j["exposure"].value("minEV100",             -6.0f);
		m_MaxEV100               = j["exposure"].value("maxEV100",             16.0f);
		m_AdaptationSpeedUp      = j["exposure"].value("adaptationSpeedUp",    3.0f);
		m_AdaptationSpeedDown    = j["exposure"].value("adaptationSpeedDown",  1.0f);

		m_EnableBloom            = j["bloom"].value("enable",    true);
		m_BloomThreshold         = j["bloom"].value("threshold", 1.0f);
		m_BloomKnee              = j["bloom"].value("knee",      0.5f);
		m_BloomIntensity         = j["bloom"].value("intensity", 0.12f);
		m_BloomScatter           = j["bloom"].value("scatter",   0.7f);
		m_BloomClamp             = j["bloom"].value("clamp",     64.0f);

		m_ToneMapperType         = j["toneMapping"].value("type",       1);
		m_WhitePoint             = j["toneMapping"].value("whitePoint", 11.2f);

		m_EnableColorGrading     = j["colorGrading"].value("enable",      true);
		m_Contrast               = j["colorGrading"].value("contrast",    1.0f);
		m_Saturation             = j["colorGrading"].value("saturation",  1.0f);
		m_HueShift               = j["colorGrading"].value("hueShift",    0.0f);
		m_Temperature            = j["colorGrading"].value("temperature", 0.0f);
		m_Tint                   = j["colorGrading"].value("tint",        0.0f);

		m_EnableDOF              = j["dof"].value("enable",        false);
		m_FocusDistance          = j["dof"].value("focusDistance", 5.0f);
		m_FocusRange             = j["dof"].value("focusRange",    2.0f);
		m_Aperture               = j["dof"].value("aperture",      2.8f);
		m_MaxCoC                 = j["dof"].value("maxCoC",        12.0f);

		m_EnableMotionBlur       = j["motionBlur"].value("enable",  false);
		m_ShutterScale           = j["motionBlur"].value("shutter", 0.5f);
		m_MotionBlurSamples      = j["motionBlur"].value("samples", 12);

		m_EnableVignette         = j["vignette"].value("enable",     true);
		m_VignetteIntensity      = j["vignette"].value("intensity",  0.2f);
		m_VignetteSmoothness     = j["vignette"].value("smoothness", 0.5f);

		m_EnableChromaticAberration    = j["chromaticAberration"].value("enable",    false);
		m_ChromaticAberrationIntensity = j["chromaticAberration"].value("intensity", 0.02f);

		m_EnableFilmGrain        = j["filmGrain"].value("enable",    true);
		m_FilmGrainIntensity     = j["filmGrain"].value("intensity", 0.04f);

		m_EnableLensDirt         = j["lensDirt"].value("enable",    false);
		m_LensDirtIntensity      = j["lensDirt"].value("intensity", 0.5f);

		m_EnableFXAA             = j["aa"].value("enableFXAA",       false);
		m_EnableSharpen          = j["aa"].value("enableSharpen",    true);
		m_SharpenIntensity       = j["aa"].value("sharpenIntensity", 0.15f);

		m_EnableDithering        = j["dithering"].value("enable", true);

		m_IsDirty = true;
		return true;
	}

	// ============================================================
	// 重置默认值
	// ============================================================

	void VansPostProcessProfile::ResetToDefaults()
	{
		*this = VansPostProcessProfile{};
		m_IsDirty = true;
	}
}
