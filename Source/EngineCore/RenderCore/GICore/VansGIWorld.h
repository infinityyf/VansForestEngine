#pragma once
#include "VansGIWorldData.h"
#include "VansGIVoxelSource.h"
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
        double pagePlanMilliseconds=0,retirementMilliseconds=0;
    };

    // 只在开关打开后创建。所有资源由该对象持有，在既有安全重建点释放。
    class VansGIWorld final : public IVansLayoutConstraints
    {
    public:
        VansGIWorld() = default;
        ~VansGIWorld();
        VansGIWorld(const VansGIWorld&)=delete;
        VansGIWorld& operator=(const VansGIWorld&)=delete;
        void Initialize(VansVKDevice& device,VansScene& scene,const GIWorldSettings& settings,bool hardware,uint32_t rayCapacity);
        void RecordUpdates(VansVKCommandBuffer& command);
        // 上帧反馈完成之后、安排本帧探针之前发布 CPU 索引；GPU 上传在同帧 trace 之前。
        const GIWorldDirtyRegions* PrepareUpdates();
        bool QueueSourceChanges(GIVoxelSourceChanges changes);
        GIWorldSourceUpdateStats SourceUpdateStats() const { return m_SourceUpdateStats; }
        GIWorldCpuWorkStats CpuWorkStats() const { return m_CpuWorkStats; }
        size_t SourceTemplateCount() const { return m_SourceTemplates.size(); }
        void RecordScatterRanges(VansVKCommandBuffer& command,const std::array<uint32_t,8>& rayCounts);
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
        uint64_t Revision() const { return m_Revision; }
        uint64_t AllocatedBytes() const;
        uint32_t PendingBricks() const { return m_PendingBricks; }
        bool HasPendingUpdates() const { return m_SourceDirty || m_InstanceGeneration!=m_PublishedInstanceGeneration || m_BuildFuture.valid() || m_ReadyBuild || m_RetiredBuild || m_PendingBricks; }
        bool CoarseReady() const { return m_Builder.LevelCount() && !m_PendingCoarseBricks &&
            (uint32_t(m_Parameters.grid.w)&(1u<<(m_Builder.LevelCount()-1u)))!=0u; }
    private:
        std::unique_ptr<VansComputeShader> LoadShader(const std::string& file,uint32_t pushBytes=0);
        std::vector<glm::vec4> CaptureTexture(VansTexture* texture,uint32_t& width,uint32_t& height,uint32_t maximum=512,bool footprint=false);
        void CaptureGeometry(const std::vector<GIVoxelSource>& sources,std::vector<GIWorldInstance>& instances);
        void MakeBuffer(VansVKBuffer& buffer,const void* data,size_t bytes,VkBufferUsageFlags usage);
        VansVKDevice* m_Device=nullptr;
        GIWorldSettings m_Settings;
        GIWorldParameters m_Parameters;
        glm::vec3 m_ViewCenter{0};
        bool m_SourceDirty=false;
        struct SourceTemplate { std::shared_ptr<const void> identity; std::vector<GIVoxelSourcePart> parts; std::shared_ptr<const GIWorldTemplate> model; };
        std::vector<SourceTemplate> m_SourceTemplates;
        std::shared_ptr<const GIWorldTemplate> FindTemplate(const GIVoxelSource& source) const;
        struct InstanceSource { std::shared_ptr<const GIWorldTemplate> model; std::vector<glm::mat4> transforms; };
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
        std::vector<glm::uvec2> m_PendingHeightSpans,m_PendingRangeSpans,m_PendingColorSpans;
        void RecordTerrainUpdates(VansVKCommandBuffer& command);
        VansGIWorldBuilder m_Builder;
        // 工作线程只修改 CPU 构建游标；未完成时渲染线程不等待、不重置来源或页表。
        struct BuildResult
        {
            std::unique_ptr<VansGIWorldBuilder> replacement;
            std::vector<GIWorldBrick> bricks;
            bool sourcesChanged=false;
            uint64_t instanceGeneration=0;
            uint64_t preparedInstances=0;
            GIWorldDirtyRegions changed;
            LayoutIndex layoutIndex;
            SourceChunks chunks;
            SourceChanges retiredChanges;
            std::vector<GIWorldPageGPU> pages;
            std::map<GIWorldCell,uint32_t> pageSlots;
            GIWorldParameters parameters;
            uint32_t pendingCoarseBricks=0;
            GIWorldCpuWorkStats cpu;
        };
        std::future<BuildResult> m_BuildFuture;
        std::optional<BuildResult> m_ReadyBuild;
        // 最多一份待退役事务，转移到下一后台批次先释放；不另开无界任务队列。
        std::unique_ptr<BuildResult> m_RetiredBuild;
        GIWorldCpuWorkStats m_CpuWorkStats;
        uint32_t m_PendingBricks = 0;
        uint32_t m_PendingCoarseBricks = 0;
        void QueueBuild();
        LayoutIndex m_LayoutIndex;
        std::vector<glm::vec4> m_Materials;
        std::vector<GIWorldPageGPU> m_Pages;
        std::map<GIWorldCell,uint32_t> m_PageSlots;
        std::array<VansVKBuffer,8> m_Buffers;
        VkDescriptorSetLayout m_Layout=VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_Sets;
        std::string m_ShaderFolder;
        std::filesystem::path m_ShaderArtifactRoot;
        std::unique_ptr<VansComputeShader> m_Trace,m_Lighting,m_Visibility,m_Bias,m_Atlas,m_State;
        uint64_t m_Revision=1;
    };
}
