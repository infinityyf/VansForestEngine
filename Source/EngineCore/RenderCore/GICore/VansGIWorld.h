#pragma once
#include "VansGIWorldData.h"
#include "VansGIFrameWork.h"
#include "VansGIMemorySnapshot.h"
#include "VansGIQueryChanges.h"
#include "VansGIVoxelSource.h"
#include "VansGITemplateCapture.h"
#include "VansGIVoxelMaterial.h"
#include "VansLayoutConstraints.h"
#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansShader.h"
#include <memory>
#include <future>
namespace Vans { struct VansShaderCookProgram; }

namespace VansGraphics
{
    class VansScene;
    struct GIVoxelSource;
    class VansVKDevice;
    class VansVKCommandBuffer;
    class VansTexture;
    struct alignas(16) GIWorldParameters
    {
        glm::vec4 terrain{0};
        glm::uvec4 height{0};
        glm::uvec4 table{0};
        glm::vec4 center{0};
        glm::vec4 grid{0};
        glm::vec4 voxelMin{0};
        glm::vec4 voxelMax{0};
        std::array<glm::uvec4,16> heightLevels{};
    };
    // data：体素首址、就绪标记、64 位 2×2×2 子块占用掩码。
    struct alignas(16) GIWorldPageGPU { glm::ivec4 key{0}; glm::uvec4 data{~0u,0u,0u,0u}; };
    static_assert(sizeof(GIWorldParameters)==368 && sizeof(GIWorldPageGPU)==32);
    struct GIWorldSourceUpdateStats
    {
        uint64_t updatedChunks=0,removedChunks=0,submittedInstances=0,preparedInstances=0;
    };

    struct GIWorldCpuWorkStats
    {
        uint64_t pagePlanJobs=0,pagesPlanned=0,retirementJobs=0;
        uint64_t submissionThreadPagePlans=0,submissionThreadRetirements=0;
        uint64_t templateBakes=0,submissionThreadTemplateBakes=0;
        uint64_t captureDecodes=0,submissionThreadCaptureDecodes=0;
        uint64_t captureSubmissions=0,peakCaptureInputBytes=0;
        double pagePlanMilliseconds=0,retirementMilliseconds=0;
        double templateBakeMilliseconds=0;
        double captureDecodeMilliseconds=0;
    };

    // 只在开关打开后创建。所有资源由该对象持有，在既有安全重建点释放。
    class VansGIWorld final : public IVansLayoutConstraints
    {
    public:
        VansGIWorld() = default;
        ~VansGIWorld();
        VansGIWorld(const VansGIWorld&)=delete;
        VansGIWorld& operator=(const VansGIWorld&)=delete;
        void Initialize(VansVKDevice& device,VansScene& scene,const GIWorldSettings& settings,bool hardware,uint32_t rayCapacity,
            const std::shared_ptr<const VansGIVoxelMaterialInputs>& materials);
        VansGIWorldUploadWork RecordUpdates(VansVKCommandBuffer& command);
        // 上帧反馈完成之后、安排本帧探针之前发布 CPU 索引；GPU 上传在同帧 trace 之前。
        const GIWorldDirtyRegions* PrepareUpdates();
        bool QueueSourceChanges(GIVoxelSourceChanges changes);
        void ApplyMaterials(const std::shared_ptr<const VansGIVoxelMaterialInputs>& inputs);
        GIWorldSourceUpdateStats SourceUpdateStats() const { return m_SourceUpdateStats; }
        GIWorldCpuWorkStats CpuWorkStats() const { return m_CpuWorkStats; }
        size_t SourceTemplateCount() const { return m_SourceTemplates.size(); }
        uint32_t RecordScatterRanges(VansVKCommandBuffer& command,const std::array<uint32_t,kGIMaximumRegionCount>& rayCounts);
        const VansVKBuffer& ScatterBuffer() const { return m_Buffers[7]; }
        bool ApplyHeightPatch(uint32_t x,uint32_t z,uint32_t w,uint32_t h,
            const std::vector<uint8_t>& pixels,GIWorldHeightData::Patch& changed);
        bool ApplyColorPatch(uint32_t map,uint32_t x,uint32_t y,uint32_t w,uint32_t h,const std::vector<uint8_t>& pixels);
        std::optional<GIWorldBounds> TakeLightingChanges()
        {auto changed=m_PendingLightingBounds;m_PendingLightingBounds.reset();return changed;}
        bool IsPositionValid(glm::vec3 position, float clearance) const override;
        VansGeometrySurfaceMeasure MeasureSurface(
            glm::vec3 minimum, glm::vec3 maximum) const override;
        void SetViewCenter(glm::vec3 center) { m_ViewCenter=center; }
        void InvalidateSources() { m_SourceDirty=true; }
        bool SourcesDirty() const { return m_SourceDirty; }
        static bool CookShaders(const std::string& shaderRoot,
            const std::filesystem::path& artifactRoot,
            std::vector<Vans::VansShaderCookProgram>& programs,std::string& error);
        VkDescriptorSetLayout Layout() const { return m_Layout; }
        VkDescriptorSet Descriptor() const { return m_Sets.empty()?VK_NULL_HANDLE:m_Sets[0]; }
        VansComputeShader& Trace() { return *m_Trace; }
        VansComputeShader& Lighting() { return *m_Lighting; }
        VansComputeShader& Visibility() { return *m_Visibility; }
        VansComputeShader& Bias() { return *m_Bias; }
        VansComputeShader& Atlas() { return *m_Atlas; }
        VansComputeShader& State() { return *m_State; }
        const VansGIWorldQueryPublication& QueryPublication() const { return m_QueryPublication; }
        uint64_t BufferCapacityBytes() const;
        VansGIMemoryUsage GetMemoryUsage() const;
        uint32_t PendingBricks() const { return m_PendingBricks; }
        bool HasPendingUpdates() const { return m_SourceDirty || m_InstanceGeneration!=m_PublishedInstanceGeneration || !m_CaptureBatches.empty() || m_BuildFuture.valid() || m_ReadyBuild || m_RetiredBuild || m_PendingBricks || m_PendingMaterialBegin!=SIZE_MAX || m_MaterialRefreshPending; }
        bool CoarseReady() const { return m_Builder.LevelCount() && !m_PendingCoarseBricks &&
            (uint32_t(m_Parameters.grid.w)&(1u<<(m_Builder.LevelCount()-1u)))!=0u; }
    private:
        std::unique_ptr<VansComputeShader> LoadShader(const std::string& file,uint32_t pushBytes=0);
        std::vector<glm::vec4> CaptureTexture(VansTexture* texture,uint32_t& width,uint32_t& height,uint32_t maximum=512,bool footprint=false);
        void CaptureGeometry(const std::vector<GIVoxelSource>& sources,std::vector<GIWorldInstance>& instances);
        void MakeBuffer(VansVKBuffer& buffer,const void* data,size_t bytes,VkBufferUsageFlags usage,size_t capacity=0);
        VansVKDevice* m_Device=nullptr;
        GIWorldSettings m_Settings;
        GIWorldParameters m_Parameters;
        glm::vec3 m_ViewCenter{0};
        bool m_SourceDirty=false;
        struct SourceTemplate
        {
            std::shared_ptr<const void> identity;
            std::vector<GIVoxelSourcePart> parts; // 仅作缓存身份比较；后台不解引用。
            std::vector<glm::vec3> materialBaseColors;
            std::vector<VansGIVoxelMaterial> materialInputs;
            // 覆盖候选复用原材质槽；旧模板只保留到来源事务发布/退役，不形成编辑历史链。
            std::shared_ptr<SourceTemplate> previous;
            std::shared_ptr<const GIWorldTemplate> model;
            std::shared_ptr<const std::vector<GIWorldMesh>> captured;
            std::shared_ptr<VansGITemplateCapture> captureBatch;
            uint32_t captureIndex=0,materialBase=0;
            size_t capturedBytes=0;
        };
        std::vector<std::shared_ptr<SourceTemplate>> m_SourceTemplates;
        std::shared_ptr<const VansGIVoxelMaterialInputs> m_MaterialInputs;
        bool UpdateTemplateMaterials(const SourceTemplate& definition);
        std::vector<VansGIVoxelMaterial> CaptureMaterialInputs(const std::vector<GIVoxelSourcePart>& parts) const;
        bool MaterialCoverageChanged(const SourceTemplate& definition) const;
        bool m_MaterialRefreshPending=false;
        void QueueMaterialRefresh();
        bool RollbackMaterialRefresh(std::shared_ptr<VansGITemplateCapture> failedCapture);
        std::vector<std::shared_ptr<VansGITemplateCapture>> PrepareCaptures(const std::vector<std::shared_ptr<SourceTemplate>>& definitions);
        // 启动时加载一次；捕获票据持有共享租约直至 GPU 完成，不在来源变更时重新读盘/反射。
        std::shared_ptr<VansComputeShader> m_TextureCaptureShader;
        std::vector<std::shared_ptr<VansGITemplateCapture>> m_CaptureBatches;
        size_t m_TemplateCells=0,m_PendingTemplateBytes=0;
        void SubmitCapture(const std::shared_ptr<VansGITemplateCapture>& capture,const std::vector<std::shared_ptr<SourceTemplate>>& definitions);
        std::shared_ptr<SourceTemplate> FindTemplate(const GIVoxelSource& source) const;
        std::shared_ptr<SourceTemplate> CaptureTemplate(const GIVoxelSource& source,uint32_t materialBase,
            std::vector<glm::vec4>& materials,size_t byteBudget);
        struct InstanceSource { std::shared_ptr<SourceTemplate> definition; std::vector<glm::mat4> transforms; };
        struct CapturedSource { std::shared_ptr<const InstanceSource> source; std::vector<GIWorldInstance> instances; };
        using SourceChunks=std::map<std::string,std::shared_ptr<const CapturedSource>>;
        // 从最后一次发布起累计变更；空指针是已确认删除。提交线程只复制这些键。
        using SourceChanges=std::map<std::string,std::shared_ptr<const InstanceSource>>;
        SourceChanges m_SourceChanges;
        SourceChunks m_SourceChunks;
        uint64_t m_InstanceGeneration=0;
        uint64_t m_PublishedInstanceGeneration=0;
        GIWorldSourceUpdateStats m_SourceUpdateStats;
        struct BuildResult;
        void PlanPages(BuildResult& result) const;
        using LayoutIndex=std::map<GIWorldCell,std::vector<uint32_t>>;
        static LayoutIndex BuildLayoutIndex(const std::vector<GIWorldInstance>& instances);
        GIWorldHeightData m_Height;
        GIWorldTerrainColorData m_TerrainColors;
        std::optional<GIWorldBounds> m_PendingLightingBounds;
        std::optional<GIWorldBounds> m_PendingGeometryBounds;
        std::vector<glm::uvec2> m_PendingHeightSpans,m_PendingRangeSpans,m_PendingColorSpans;
        void RecordTerrainUpdates(VansVKCommandBuffer& command,VansGIWorldUploadWork& work);
        VansGIWorldBuilder m_Builder;
        // 工作线程只修改 CPU 构建游标；未完成时渲染线程不等待、不重置来源或页表。
        struct BuildResult
        {
            struct TemplateBake { std::shared_ptr<SourceTemplate> definition; std::shared_ptr<const GIWorldTemplate> model; };
            struct TemplateCapture {std::shared_ptr<SourceTemplate> definition;GITemplateCaptureResult output;};
            std::vector<TemplateCapture> captures;
            std::shared_ptr<VansGITemplateCapture> processedCapture;
            bool captureDecoded=false,sourceSnapshot=false;
            std::vector<TemplateBake> templates;
            std::vector<std::shared_ptr<SourceTemplate>> unusedCaptures;
            std::vector<std::shared_ptr<const std::vector<GIWorldMesh>>> retiredCaptures;
            std::vector<std::shared_ptr<SourceTemplate>> retiredTemplates;
            std::string error;
            std::unique_ptr<VansGIWorldBuilder> replacement;
            std::vector<GIWorldBrick> bricks;
            bool sourcesChanged=false;
            uint64_t instanceGeneration=0;
            uint64_t preparedInstances=0;
            GIWorldDirtyRegions changed;
            VansGIWorldQueryChanges queryChanges;
            LayoutIndex layoutIndex;
            SourceChunks chunks;
            SourceChanges retiredChanges;
            std::vector<GIWorldPageGPU> pages;
            std::map<GIWorldCell,uint32_t> pageSlots;
            GIWorldParameters parameters;
            uint32_t pendingCoarseBricks=0,readyBricks=0;
            GIWorldCpuWorkStats cpu;
            bool HasRetainedInputs() const {return processedCapture || !retiredCaptures.empty() || !captures.empty() || !retiredTemplates.empty();}
        };
        std::future<BuildResult> m_BuildFuture;
        std::optional<BuildResult> m_ReadyBuild;
        // 最多一份待退役事务，转移到下一后台批次先释放；不另开无界任务队列。
        std::unique_ptr<BuildResult> m_RetiredBuild;
        GIWorldCpuWorkStats m_CpuWorkStats;
        uint32_t m_PendingBricks = 0;
        uint32_t m_PendingCoarseBricks = 0;
        uint32_t m_ReadyBricks = 0;
        void QueueBuild();
        LayoutIndex m_LayoutIndex;
        std::vector<glm::vec4> m_Materials;
        size_t m_PendingMaterialBegin=SIZE_MAX,m_PendingMaterialEnd=0;
        std::vector<GIWorldPageGPU> m_Pages;
        std::map<GIWorldCell,uint32_t> m_PageSlots;
        std::array<VansVKBuffer,8> m_Buffers;
        VkDescriptorSetLayout m_Layout=VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_Sets;
        std::string m_ShaderFolder;
        std::filesystem::path m_ShaderArtifactRoot;
        std::unique_ptr<VansComputeShader> m_Trace,m_Lighting,m_Visibility,m_Bias,m_Atlas,m_State;
        VansGIWorldQueryPublication m_QueryPublication;
        void PublishQueryChanges(VansGIWorldQueryChanges changes);
    };
}
