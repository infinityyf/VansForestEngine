#pragma once
#include "../../ScriptCore/VansCommonUtils.h"
#include "VansLightFrameTypes.h"
#include "../VansRenderSceneSnapshot.h"
#include <cstddef>
#include <cstdint>
#include <vector>
using namespace VansGraphics;

namespace VansGraphics
{
	class VansLightManager
	{
		friend class VansRenderNode;
	private:
		std::vector<VansDirectionalLight> m_DirectionalLights;
		std::vector<VansPointLight> m_PointLights;
		std::vector<VansSpotLight> m_SpotLights;
		std::vector<VansRectLight> m_RectLights;
		std::vector<VansPunctualShadowRegistration> m_PointShadowRegistrations;
		std::vector<VansPunctualShadowRegistration> m_SpotShadowRegistrations;
		std::vector<VansPunctualShadowRegistration> m_RectShadowRegistrations;
		VansCelestialLightingState m_MainCelestialLightingState;
		uint32_t m_NextStableLightId = 1;

		float m_LightFrameSequence = 0.0f;

		std::vector<VansDirectionalLight> BuildPreparedDirectionalLights();
	public:
		static std::size_t GetLightBufferPayloadSize();
		static bool BuildRenderLightBufferPayload(
			const VansRenderLightFrameData& frameData,
			const std::vector<VansPunctualShadowGPU>& shadowData,
			const std::vector<VansPunctualShadowViewGPU>& shadowViews,
			std::vector<std::uint8_t>& outPayload);

		void AddDirectionalLight(const VansDirectionalLight& light);

		void AddPointLight(const VansPointLight& light, const VansPunctualShadowSettings& shadowSettings = {});

		void AddSpotLight(const VansSpotLight& light, const VansPunctualShadowSettings& shadowSettings = {});

		void AddRectLight(const VansRectLight& light, const VansPunctualShadowSettings& shadowSettings = {});

		bool RemovePointLight(uint32_t index);

		bool RemoveSpotLight(uint32_t index);

		bool RemoveRectLight(uint32_t index);

		void UpdateLightShadowMatrixData(const VansCascadeCameraData& cameraData);

		void UpdateLightShadowMatrixData(const glm::vec3& cameraPosition);
		VansRenderPunctualShadowFrameInput BuildPunctualShadowFrameInput(
			const VansCascadeCameraData& cameraData);

		// CPU-only refresh for editor/timeline mutations that need derived sky
		// state immediately. It never publishes or uploads a render frame.
		void RefreshDerivedLightingState();

		// Main 只构建结构化值快照；shadow meta 与 GPU ABI 打包归 backend。
		VansRenderLightFrameData BuildRenderLightFrameData();

		// CPU 预计算大气仰角衰减后的太阳颜色
		// 公式与 VolumetricFog.comp AtmSunColor / CloudCommon CalcCloudSunAbsorbLight 保持一致
		// sunDir 可以未归一化；baseColor 为美术设置的原始颜色
		static glm::vec3 ComputeAtmosphereSunColor(const glm::vec3& sunDir, const glm::vec3& baseColor);

		static VansCelestialLightingState ComputeCelestialLightingState(const VansDirectionalLight& light);
		const VansCelestialLightingState& GetMainCelestialLightingState() const { return m_MainCelestialLightingState; }
		float GetSkyDiffuseScale() const { return m_MainCelestialLightingState.skyDiffuseScale; }
		float GetSkySpecularScale() const { return m_MainCelestialLightingState.skySpecularScale; }

		std::vector<VansDirectionalLight>& GetDirectionLights() { return m_DirectionalLights; }

		std::vector<VansPointLight>& GetPointLights() { return m_PointLights; }

		std::vector<VansSpotLight>& GetSpotLight() { return m_SpotLights; }

		std::vector<VansRectLight>& GetRectLights() { return m_RectLights; }

		std::vector<VansPunctualShadowRegistration>& GetPointShadowRegistrations() { return m_PointShadowRegistrations; }

		std::vector<VansPunctualShadowRegistration>& GetSpotShadowRegistrations() { return m_SpotShadowRegistrations; }

		std::vector<VansPunctualShadowRegistration>& GetRectShadowRegistrations() { return m_RectShadowRegistrations; }

		uint32_t GetMaxPointLightCount() const { return VANS_MAX_POINT_LIGHTS; }

		uint32_t GetMaxSpotLightCount() const { return VANS_MAX_SPOT_LIGHTS; }

		uint32_t GetMaxRectLightCount() const { return VANS_MAX_RECT_LIGHTS; }

		// 场景切换只清空 Main-owned 灯光状态；backend frame resource 跨场景复用。
		void ClearLights();
	};
}
