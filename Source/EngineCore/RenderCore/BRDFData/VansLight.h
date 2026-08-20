#pragma once
#include "../../ScriptCore/VansCommonUtils.h"
#include "../VulkanCore/VansVKBuffer.h"
#include "../ShadowCore/VansPunctualShadowManager.h"
#include <vector>
using namespace VansGraphics;

namespace VansGraphics
{
	enum class VansLightType
	{
		DIRECTIONAL = 0,
		POINT = 1,
		SPOT = 2,
		RECT = 3
	};

	//保证16字节对齐
	//CPU和GPU上保持一致
	struct alignas(16) VansDirectionalLight
	{
		glm::vec3				m_Direction;
		alignas(16) glm::vec3	m_Color;
		alignas(16) float		m_Intensity;
		float					padding[3];
		glm::mat4x4				m_ShadowMatrix[4];      // one per cascade
		glm::vec4				m_CascadeSplits;        // view-space far distances per cascade
		glm::vec4				m_CascadeTexelSize;     // world units per shadow texel
		glm::vec4				m_CascadeDepthScale;    // 1 / light-space depth range
		glm::vec4				m_CascadeNormalBias;    // world normal bias per cascade
		glm::vec4				m_CascadeFilterRadius;  // maximum PCSS search/filter radius in texels
	};

	struct VansCascadeCameraData
	{
		glm::vec3 position;
		glm::vec3 forward;
		glm::vec3 up;
		float verticalFovRadians;
		float aspectRatio;
		float nearPlane;
		float farPlane;
		uint32_t viewportWidth = 1920;
		uint32_t viewportHeight = 1080;
	};

	struct VansCelestialLightingState
	{
		glm::vec3 sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 moonDirection = glm::vec3(0.0f, -1.0f, 0.0f);
		glm::vec3 direction = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 color = glm::vec3(1.0f);
		float intensity = 1.0f;
		float skyDiffuseScale = 1.0f;
		float skySpecularScale = 1.0f;
		float moonBlend = 0.0f;
	};

	struct alignas(16) VansPointLight
	{
		glm::vec3				m_Position;
		alignas(16) glm::vec3	m_Color;
		alignas(16) float		m_Intensity;
		float					m_Radius;
		uint32_t			m_ShadowMetaIndex;
		float					m_IESProfileIndex;
	};

	struct alignas(16) VansSpotLight
	{
		glm::vec3				m_Position;
		alignas(16) glm::vec3	m_Direction;
		alignas(16) glm::vec3	m_Color;
		alignas(16) float		m_Intensity;
		float					m_Radius;
		float					m_InnerCutOff;
		float					m_OuterCutOff;
		uint32_t			m_ShadowMetaIndex;
		float					m_IESProfileIndex;
		float					m_IESIntensityScale;
		float					m_pad0;
	};

	// ── RectLight (area light, evaluated via LTC) ────────────────────────────
	// 矩形面光源。Position 为矩形中心，(Right,Up) 在矩形所在平面，Normal 为光线传播方向。
	// 阴影设置与运行时分配由 VansPunctualShadowManager 管理；本结构只保留 GPU 光照参数。
	struct alignas(16) VansRectLight
	{
		glm::vec3				m_Position;       // 矩形中心世界坐标
		float					m_HalfWidth;      // 沿 Right 方向半边长
		glm::vec3				m_Normal;         // 光线传播方向（与 SpotLight 的 -m_Direction 同义）
		float					m_HalfHeight;     // 沿 Up 方向半边长
		glm::vec3				m_Right;          // 世界空间 Right (rotation * +X)
		float					m_Range;          // 影响半径
		glm::vec3				m_Up;             // 世界空间 Up    (rotation * +Y)
		float					m_Intensity;
		glm::vec3				m_Color;
		float					m_TwoSided;       // 0/1
		uint32_t			m_ShadowMetaIndex;
		float					m_AttenuationExp; // 距离衰减指数（默认 2.0）
		float					m_TextureSlot;    // -1 = 无发光贴图；>=0 = rectLightEmissive 层索引
		float					m_TexLodBias;     // 发光贴图 LOD 偏移量（默认 0.0）
	};

	static_assert(sizeof(VansPointLight) == 48, "VansPointLight must match GLSL std430 layout");
	static_assert(sizeof(VansSpotLight) == 80, "VansSpotLight must match GLSL std430 layout");
	static_assert(sizeof(VansRectLight) == 96, "VansRectLight must match GLSL std430 layout");

	struct VansPunctualShadowRegistration
	{
		uint32_t stableLightId = 0;
		VansPunctualShadowSettings settings;
	};

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
		VansPunctualShadowManager m_PunctualShadowManager;
		VansCelestialLightingState m_MainCelestialLightingState;
		uint32_t m_NextStableLightId = 1;
		uint64_t m_ShadowFrameIndex = 0;

		uint32_t m_LightCounts[4];
		float m_SoftShadowParams[4];

		VansVKBuffer m_LightBuffer;

		const uint32_t m_MaxDirectionLightCount = 1;
		const uint32_t m_MaxPointLightCount = 64;
		const uint32_t m_MaxSpotLightCount = 64;
		const uint32_t m_MaxRectLightCount = 32;
	public:

		//Descriptor set layout
		VkDescriptorSetLayout m_LightDataDescriptorSetLayout;
		std::vector<VkDescriptorSet> m_LightDataDescriptorSets;

	public:
		VansVKBuffer& GetLightBuffer() { return m_LightBuffer; }

		void AddDirectionalLight(const VansDirectionalLight& light);

		void AddPointLight(const VansPointLight& light, const VansPunctualShadowSettings& shadowSettings = {});

		void AddSpotLight(const VansSpotLight& light, const VansPunctualShadowSettings& shadowSettings = {});

		void AddRectLight(const VansRectLight& light, const VansPunctualShadowSettings& shadowSettings = {});

		bool RemovePointLight(uint32_t index);

		bool RemoveSpotLight(uint32_t index);

		bool RemoveRectLight(uint32_t index);

		void UpdateLightShadowMatrixData(const VansCascadeCameraData& cameraData);

		void UpdateLightShadowMatrixData(const glm::vec3& cameraPosition);

		void UpdateLightCPUData();

		void SyncLightGPUData(const VansCascadeCameraData& cameraData);

		void SyncLightGPUData(const glm::vec3& cameraPosition);

		// CPU 预计算大气仰角衰减后的太阳颜色
		// 公式与 VolumetricFog.comp AtmSunColor / CloudCommon CalcCloudSunAbsorbLight 保持一致
		// sunDir 可以未归一化；baseColor 为美术设置的原始颜色
		static glm::vec3 ComputeAtmosphereSunColor(const glm::vec3& sunDir, const glm::vec3& baseColor);

		static VansCelestialLightingState ComputeCelestialLightingState(const VansDirectionalLight& light);
		const VansCelestialLightingState& GetMainCelestialLightingState() const { return m_MainCelestialLightingState; }
		float GetSkyDiffuseScale() const { return m_MainCelestialLightingState.skyDiffuseScale; }
		float GetSkySpecularScale() const { return m_MainCelestialLightingState.skySpecularScale; }

		//Create light gpu data
		void CreateLightUniformData(VkDevice& logic_device);

		std::vector<VansDirectionalLight>& GetDirectionLights() { return m_DirectionalLights; }

		std::vector<VansPointLight>& GetPointLights() { return m_PointLights; }

		std::vector<VansSpotLight>& GetSpotLight() { return m_SpotLights; }

		std::vector<VansRectLight>& GetRectLights() { return m_RectLights; }

		std::vector<VansPunctualShadowRegistration>& GetPointShadowRegistrations() { return m_PointShadowRegistrations; }

		std::vector<VansPunctualShadowRegistration>& GetSpotShadowRegistrations() { return m_SpotShadowRegistrations; }

		std::vector<VansPunctualShadowRegistration>& GetRectShadowRegistrations() { return m_RectShadowRegistrations; }

		VansPunctualShadowManager& GetPunctualShadowManager() { return m_PunctualShadowManager; }

		const VansPunctualShadowManager& GetPunctualShadowManager() const { return m_PunctualShadowManager; }

		uint32_t GetMaxPointLightCount() const { return m_MaxPointLightCount; }

		uint32_t GetMaxSpotLightCount() const { return m_MaxSpotLightCount; }

		uint32_t GetMaxRectLightCount() const { return m_MaxRectLightCount; }

		// ── 场景切换时清空灯光数据 ────────────────────────────────────
		// 仅清空 CPU 侧灯光列表和计数器，保留 GPU buffer 和 descriptor
		// 以便下一次 CreateLightUniformData 时复用。
		void ClearLights();

		// 销毁 GPU buffer 和 descriptor（用于完全卸载场景）
		void DestroyGPUResources(VkDevice device);

		~VansLightManager();
	};
}
