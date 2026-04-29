#pragma once

#include "VulkanCore/VansShader.h"
#include "VulkanCore/VansTexture.h"
#include "VulkanCore/VansVKBuffer.h"
#include "VansRuntimeRenderTextureManager.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "VulkanCore/VansDescriptorSetLayouts.h"
#include "BRDFData/VansPBR.h"
#include "BRDFData/VansLight.h"
#include "VansAsset.h"
#include <vector>
#include <unordered_map>
#include <string>

using namespace VansGraphics;
namespace VansGraphics
{
	// ============================================================
	// Well-known render-pass name constants.
	// Each engine render pass queries the material for its unique pass name.
	// ============================================================
	namespace VansPass
	{
		static constexpr const char* GBUFFER          = "gbuffer";          // opaque GBuffer fill
		static constexpr const char* SHADOW           = "shadow";           // cascade shadow map
		static constexpr const char* PUNCTUAL_SHADOW  = "punctualShadow";   // point/spot shadow
		static constexpr const char* FORWARD_TRANSPARENT = "transparent";   // forward transparent
		static constexpr const char* SKY_BOX          = "skybox";           // sky box
		static constexpr const char* DEFERRED         = "deferred";         // deferred lighting resolve
		static constexpr const char* POST_PROCESS     = "postProcess";      // post-processing
		static constexpr const char* SCREEN_SPACE     = "screenSpace";      // SSAO / SSR etc.
		// future
		static constexpr const char* VELOCITY         = "velocity";
		static constexpr const char* PRE_DEPTH        = "preDepth";
	}
	class VansPBRMaterial;
	class VansTransparentMaterial;
	class VansSkyBoxMaterial;
	class VansSkinMaterial;
	class VansClothMaterial;
	class VansHairMaterial;
	class VansSubsurfaceMaterial;
	class VansPostProcessMaterial;
	class VansDeferredMaterial;
	class VansSSAOMaterial;
	class VansEmissiveMaterial;

	enum VansMaterialType
	{
		VAN_PBR = 0,
		VAN_COAT = 1,
		VAN_TRANSPARENT = 2,
		VAN_POST_PROCESS = 3,
		VAN_SKY_BOX = 4,
		VAN_DEFERRED = 5,
		VAN_SCREEN_SPACE_AO = 6,
		VAN_SKIN = 9,
		VAN_CLOTH = 10,
		VAN_HAIR = 11,
		VAN_SUBSURFACE = 12,
		VAN_GRASS    = 13,
		VAN_EMISSIVE = 14,   // 自发光材质：albedo × intensity 直通，无 BRDF
	};

	// Lightweight push-constant payload built at draw time.
	// Each field maps to a global GPU resource index.
	struct alignas(16) VansDrawPushConstant
	{
		int materialIndex;    // index into global PBR param SSBO / bindless textures
		int transformIndex;   // index into per-object transform SSBO
		int animationEnabled; // 1 = skinned, 0 = static
	};

	class VansMaterialManager
	{
		friend class VansMaterial;
		friend class VansRenderNode;
		//用于存储材质数据buffer的描述符
		//实际buffer由material持有
	private:

		void InitMaterialDataDescriptors();

	public:
		static constexpr const char* RT_SSAO_RESULT = "Runtime.SSAO.Result";
		static constexpr const char* RT_SSGI_RESULT = "Runtime.SSGI.Result";
		static constexpr const char* RT_SSGI_TEMPORAL_A = "Runtime.SSGI.TemporalA";
		static constexpr const char* RT_SSGI_TEMPORAL_B = "Runtime.SSGI.TemporalB";
		static constexpr const char* RT_HZB_RESULT = "Runtime.HZB.Result";
		static constexpr const char* RT_SSR_HIT_INFO = "Runtime.SSR.HitInfo";
		static constexpr const char* RT_SSR_RAY_PDF = "Runtime.SSR.RayPDF";
		static constexpr const char* RT_SSR_RESULT = "Runtime.SSR.Result";
		static constexpr const char* RT_SSRAA_RESULT_A = "Runtime.SSR.AA.ResultA";
		static constexpr const char* RT_SSRAA_RESULT_B = "Runtime.SSR.AA.ResultB";
		static constexpr const char* RT_SSRAA_RESULT = "Runtime.SSR.AA.Result";
		static constexpr const char* RT_SSGI_FILTER_RESULT = "Runtime.SSGI.FilterResult";
		static constexpr const char* RT_SSAO_FILTER_RESULT = "Runtime.SSAO.FilterResult";
		static constexpr const char* RT_SH_R_RESULT = "Runtime.RayTracing.SH.R";
		static constexpr const char* RT_SH_G_RESULT = "Runtime.RayTracing.SH.G";
		static constexpr const char* RT_SH_B_RESULT = "Runtime.RayTracing.SH.B";
		static constexpr const char* RT_GI_VISIBILITY = "Runtime.RayTracing.GI.Visibility";
		static constexpr const char* RT_VOLUMETRIC_FOG_RESULT = "Runtime.VolumetricFog.Result";
		static constexpr const char* RT_FOG_VOXEL_INJECTION = "Runtime.Fog.VoxelInjection";
		static constexpr const char* RT_FOG_VOXEL_INJECTION_HISTORY = "Runtime.Fog.VoxelInjectionHistory";
		static constexpr const char* RT_FOG_VOXEL_RAYMARCH  = "Runtime.Fog.VoxelRayMarch";

		bool RegisterRuntimeRenderTexture(const std::string& name, VansTexture* texture, bool replaceExisting = true);

		VansTexture* GetRuntimeRenderTexture(const std::string& name) const;

		bool HasRuntimeRenderTexture(const std::string& name) const;

		bool RemoveRuntimeRenderTexture(const std::string& name);

		// 仅从注册表移除条目，不 delete 纹理（所有权归外部系统，如 VansRayTracing）
		bool UnregisterRuntimeRenderTexture(const std::string& name);

		void ClearRuntimeRenderTextures();

		// ── 场景切换时清空场景级 PBR 数据 ─────────────────────────────
		// 清除 GlobalPBR 数组和对应 GPU buffer / descriptor。
		// 不释放 PreConv/LUT 等项目级资源。
		void ClearScenePBRData(VkDevice device);

		VkDescriptorSetLayout m_MaterialAtmosphereDataLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_MaterialAtmosphereDataDescriptorSets;

		//PBR预卷积
		VkDescriptorSetLayout m_BRDFInterationTexSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_BRDFInterationTextDescriptorSets;

		//SSGI
		VkDescriptorSetLayout m_SSGITexSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_SSGIDescriptorSets;

		//HIZ
		std::vector<VkDescriptorSetLayout> m_HZBTexSetLayouts;
		std::vector<VkDescriptorSet> m_HZBDescriptorSets;

		//SSR
		VkDescriptorSetLayout m_SSRTraceSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_SSRTraceDescriptorSets;

		VkDescriptorSetLayout m_SSRResolveSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_SSRResolveDescriptorSets;

		VkDescriptorSetLayout m_SSRAASetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_SSRAADescriptorSets;

		VkDescriptorSetLayout m_BilateralFilterSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_BilateralFilterDescriptorSets;

		VkDescriptorSetLayout m_VolumetricFogSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_VolumetricFogDescriptorSets;
		VansVKBuffer m_FogParamsCBBuffer;

		// --- Voxel Fog (Light Injection + Ray March) ---
		VkDescriptorSetLayout m_FogLightInjectionSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_FogLightInjectionDescriptorSets;

		VkDescriptorSetLayout m_FogRayMarchSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_FogRayMarchDescriptorSets;

		VansVKBuffer m_FogVolumeParamsCBBuffer;   // FogVolumeParams UBO (density, anisotropy, scatter, ambient)
		uint32_t     m_FogTemporalFrame = 0;       // ping-pong frame index for fog injection


		//全局pbr参数buffer，不每个pbr材质自己持有
		VansVKBuffer m_GlobalPBRDataBuffer;
		std::vector<VansPBRMaterial*> m_GlobalPBRMaterial;
		std::vector<VansBasePBRParam> m_GlobalPBRParamData;
		VkDescriptorSetLayout m_GlobalPBRDataSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_GlobalPBRDataDescriptorSets;

		//全局pbr贴图的bindless descriptor set, 不每个材质自己持有
		std::vector<VansVKImage*> m_GlobalPBRTextures;
		VkDescriptorSetLayout m_GlobalPBRTexSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_GlobalPBRTexDescriptorSets;

		//保存全局的一些texture数据
		VansTexture* m_PreConvDiffuse = nullptr;

		VansTexture* m_PreConvSpecular = nullptr;

		VansTexture* m_BRDFIntegralLUT = nullptr;

		VansTexture* m_SkinBSDFLUT = nullptr;

		VansTexture* m_ClothBRDFLUT = nullptr;

		// LTC LUTs (Linearly Transformed Cosines, area-light BRDF)
		// 64x64, RGBA16F, runtime-uploaded from embedded float arrays (LTCData.h).
		VansTexture* m_LTC1 = nullptr;

		VansTexture* m_LTC2 = nullptr;

		uint32_t     m_SSGITemporalFrame = 0;

		VansMaterialManager();

		~VansMaterialManager();

	public:

		VansComputeShader* m_SSGIShader;

		// SSGI temporal accumulation shader & descriptors
		VansComputeShader* m_SSGITemporalShader;
		VkDescriptorSetLayout m_SSGITemporalSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_SSGITemporalDescriptorSets; // [0]=frameA, [1]=frameB
		VansVKBuffer m_SSGITemporalCBBuffer;

		VansVKBuffer m_SSGICBBuffer;

		VansVKBuffer m_SkySHResultBuffer;

		uint32_t m_HIZMipCount;

		VansComputeShader* m_HZBShader;

		// HIZ Seed：将 GBuffer 线性深度写入 mip 0
		VansComputeShader* m_HIZSeedShader;
		VkDescriptorSetLayout m_HIZSeedSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_HIZSeedDescriptorSets;

		VansComputeShader* m_SSRTraceShader;

		VansComputeShader* m_SSRResolveShader;

		VansComputeShader* m_SSRTemporalAAShader;

		struct BilateralFilterPushConst
		{
			float sigmaSpace;
			float sigmaDepth;
			int radius;
			float depthThreshold;
		};

		BilateralFilterPushConst m_BilateralFilterPushConstant;

		VansComputeShader* m_BilateralFilterShader;

		VansComputeShader* m_VolumetrcFogShader;

		VansComputeShader* m_FogLightInjectionShader;
		VansComputeShader* m_FogRayMarchShader;

		// ---- TileLight Build Pass ----
		VansVKBuffer m_TileLightHeaderBuffer;
		VansVKBuffer m_TileLightIndexBuffer;
		VkDescriptorSetLayout          m_TileLightBuildSetLayout      = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_TileLightBuildDescriptorSets;
		VansVKBuffer m_TileLightBuildParamsCBBuffer;
		VansComputeShader* m_TileLightBuildShader = nullptr;
		uint32_t m_TileLightGridX = 0;
		uint32_t m_TileLightGridY = 0;

	public:

		VansVKBuffer m_AtmospherePBRDataBuffer;

		
	public:

		//梭有材质公用
		void UpdatePBRLutDescriptorSets();

		void UpdateAtmosphereDescriptorSets();

		VansRuntimeRenderTextureManager m_RuntimeRenderTextureManager;

	};

	// ============================================================
	// Base Material — asset name (from VansAsset), type tag, shader
	// No texture or parameter data lives here.
	// ============================================================
	class VansMaterial : public VansAsset
	{
		friend class VansScene;

	public:
		VansMaterialType    m_MaterialType;

		// pass name → shader (populated at scene load from Material Pass Table)
		std::unordered_map<std::string, VansGraphicsShader*> m_PassShaders;

		// Lookup shader for a specific pass. Returns nullptr if this material
		// does not participate in that pass.
		VansGraphicsShader* GetPassShader(const std::string& passName) const;
		bool                HasPass(const std::string& passName) const;

		virtual ~VansMaterial() = default;
	};

	// ============================================================
	// VansPBRMaterial 鈥?opaque PBR surface (type 0)
	// ============================================================
	class VansPBRMaterial : public VansMaterial
	{
	public:
		VansTexture* m_BaseColorTexture  = nullptr;
		VansTexture* m_NormalTexture     = nullptr;
		VansTexture* m_MetalTexture      = nullptr;
		VansTexture* m_RoughnessTexture  = nullptr;
		VansTexture* m_AoTexture         = nullptr;

		VansBasePBRParam m_BasePBRParam;

		// Index into the global PBR param SSBO / bindless texture array.
		// Assigned during PreparePBRMaterialData; used by draw push-constant.
		int m_MaterialIndex = -1;
	};

	// ============================================================
	// VansEmissiveMaterial — 自发光表面 (type 14)
	// 复用 VansBasePBRParam 的 GPU SSBO 槽位布局：
	//   m_BasePBRParam.m_albedo    → 发光颜色 (RGB)
	//   m_BasePBRParam.m_roughness → 发光强度 (scalar, 支持 HDR >1.0)
	// Deferred.frag 读取 color.rgb * roughness 直接输出，跳过全部 BRDF/光照/阴影
	// ============================================================
	class VansEmissiveMaterial : public VansMaterial
	{
	public:
		// 发光颜色和强度，映射到全局 PBR SSBO 的 albedo/roughness 槽
		VansBasePBRParam m_BasePBRParam;

		// 可选自发光纹理（使用 Bindless slot 0，与 PBR albedo 规则一致）
		// 未设置时绑定 defaultAlbedo（纯白 1×1），乘法中性
		VansTexture* m_EmissiveTexture = nullptr;

		// 全局 PBR SSBO 中的材质索引，由 PreparePBRMaterialData 赋值
		int m_MaterialIndex = -1;
	};

	// ============================================================
	// VansTransparentMaterial 鈥?multi-texture transparent pass (type 2)
	// ============================================================
	class VansTransparentMaterial : public VansMaterial
	{
	public:
		~VansTransparentMaterial() override;

		// Flat ordered texture list; binding index == position in vector
		std::vector<VansTexture*>                          m_TransparentTextures;
		// (slot label, asset name) 鈥?for debugging / tooling
		std::vector<std::pair<std::string, std::string>>   m_TransparentTextureMap;

		// Owned descriptor set layout / set for this transparent material
		VkDescriptorSetLayout          m_TransparentOwnedLayout  = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_TransparentOwnedDescSets;

		void BuildTransparentTextureDescriptors();
		void CreateTransparentDescriptorLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings = {});
	};

	// ============================================================
	// VansSkyBoxMaterial 鈥?sky / atmosphere (type 4)
	// ============================================================
	class VansSkyBoxMaterial : public VansMaterial
	{
	public:
		VansAtmospherePBRParam m_AtmospherePBRParam;

		void UpdateAtmosphereMaterialData(VansMaterialManager& materialManager, VansLightManager& lightManager);
	};

	// ============================================================
	// VansSkinMaterial 鈥?subsurface skin shading (type 9)
	// ============================================================
	class VansSkinMaterial : public VansMaterial
	{
	public:
		~VansSkinMaterial() override;

		VansTexture* m_BaseColorTexture = nullptr;
		VansTexture* m_NormalTexture    = nullptr;

		VkDescriptorSetLayout          m_SkinOwnedLayout  = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_SkinOwnedDescSets;

		void BuildSkinTextureDescriptors();
	};
	// ============================================================
	// VansClothMaterial — cloth shading (type 10)
	// ============================================================
	class VansClothMaterial : public VansMaterial
	{
	public:
		~VansClothMaterial() override;

		VansTexture* m_BaseColorTexture  = nullptr;
		VansTexture* m_NormalTexture     = nullptr;
		VansTexture* m_RoughnessTexture  = nullptr;
		VansTexture* m_AoTexture         = nullptr;

		float        m_SheenRoughness    = 0.5f;   // 0 = silk, 1 = rough fabric

		VkDescriptorSetLayout          m_ClothOwnedLayout   = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_ClothOwnedDescSets;

		void BuildClothTextureDescriptors();
	};
	// ============================================================
	// VansHairMaterial — card-based hair shading (type 11)
	// Textures: albedo+alpha, normal, roughness, AO, strand shift
	// ============================================================
	class VansHairMaterial : public VansMaterial
	{
	public:
		~VansHairMaterial() override;

		VansTexture* m_AlbedoAlphaTexture = nullptr;  // .rgb = albedo, .a = alpha mask
		VansTexture* m_NormalTexture      = nullptr;  // tangent-space normal
		VansTexture* m_RoughnessTexture   = nullptr;  // .r = roughness
		VansTexture* m_AoTexture          = nullptr;  // .r = ambient occlusion
		VansTexture* m_ShiftTexture       = nullptr;  // .r = strand shift (0.5 = neutral)
		VansTexture* m_AlphaTexture       = nullptr;  // .r = dedicated alpha mask
		VansTexture* m_FlowTexture        = nullptr;  // .rg = tangent-space flow direction (0.5 = neutral)

		VkDescriptorSetLayout          m_HairOwnedLayout  = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_HairOwnedDescSets;

		void BuildHairTextureDescriptors();
	};
	// ============================================================
	// VansSubsurfaceMaterial — subsurface scattering (type 12)
	// Textures: albedo, normal, thickness map
	// Parameters: subsurfacePower, subsurfaceColor
	// ============================================================
	class VansSubsurfaceMaterial : public VansMaterial
	{
	public:
		~VansSubsurfaceMaterial() override;

		VansTexture* m_BaseColorTexture  = nullptr;
		VansTexture* m_NormalTexture     = nullptr;
		VansTexture* m_ThicknessTexture  = nullptr;  // .r = normalized thickness [0,1]
		VansTexture* m_RoughnessTexture  = nullptr;  // .r = perceptual roughness

		float        m_SubsurfacePower   = 12.234f;  // forward-scatter sharpness
		float        m_Thickness         = 0.5f;      // default constant thickness
		glm::vec3    m_SubsurfaceColor   = glm::vec3(1.0f, 0.2f, 0.1f); // scatter tint

		VkDescriptorSetLayout          m_SubsurfaceOwnedLayout  = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_SubsurfaceOwnedDescSets;

		void BuildSubsurfaceTextureDescriptors();
	};

	// ============================================================
	// Pass-only materials — carry only the shader (inherited from base)
	// ============================================================
	class VansPostProcessMaterial : public VansMaterial {};
	class VansDeferredMaterial    : public VansMaterial {};
	class VansSSAOMaterial        : public VansMaterial {};

	// ============================================================
	// VansGrassMaterial — GPU-driven vegetation (type 13)
	// Textures: albedo, normal, roughness, translucency, AO
	// Parameters: GrassParams struct uploaded to global PBR SSBO
	// Only participates in GBUFFER pass (no shadows)
	// ============================================================
	struct GrassParams
	{
		glm::vec4 baseColor       = glm::vec4(0.2f, 0.6f, 0.1f, 1.0f); // sRGB base tint
		float     roughness       = 0.6f;
		float     metallic        = 0.0f;
		float     translucency    = 0.5f;   // 0..1 scatter strength
		float     scatterWidth    = 0.5f;   // wrap lighting half-angle
		float     sssDistortion   = 0.2f;   // normal distortion for back-scatter
		float     sssAmbient      = 0.1f;   // ambient scatter floor
		float     sssPower        = 3.0f;   // exponent for view-dependent scatter
		float     aoStrength      = 1.0f;   // AO contribution
	};

	class VansGrassMaterial : public VansMaterial
	{
	public:
		~VansGrassMaterial() override;

		VansTexture* m_AlbedoTexture        = nullptr;
		VansTexture* m_NormalTexture        = nullptr;
		VansTexture* m_RoughnessTexture     = nullptr;
		VansTexture* m_TranslucencyTexture  = nullptr;
		VansTexture* m_AOTexture            = nullptr;

		GrassParams  m_GrassParams;

		VkDescriptorSetLayout          m_GrassOwnedLayout  = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_GrassOwnedDescSets;

		void BuildGrassTextureDescriptors();
	};
}
