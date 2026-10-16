#pragma once
#include "VansGIVoxelSource.h"
#include "VansGIWorldData.h"
#include "VansGIVoxelMaterial.h"
#include <memory>

namespace VansGraphics
{
    class VansVKDevice;
    class VansComputeShader;
    struct GITemplateCaptureInput
    {
        std::vector<GIVoxelSourcePart> parts;
        uint32_t materialBase=0;
        std::vector<VansGIVoxelMaterial> materials;
    };
    struct GITemplateCaptureResult
    {
        std::shared_ptr<const std::vector<GIWorldMesh>> meshes;
        std::vector<glm::vec4> materials;
        // 保留未乘作者颜色/金属度的线性均值，后续改色无需重捕获几何或纹理。
        std::vector<glm::vec3> materialBaseColors;
        size_t retainedBytes=0;
    };
    // Prepare 只捕获元数据和原生资源寿命，不分配 GPU 读回资源。
    // Submit 在 render thread 获得预算后分配、录制并单次提交；只有 Decode 可以在后台执行。
    class VansGITemplateCapture final
    {
    public:
        VansGITemplateCapture();
        ~VansGITemplateCapture();
        VansGITemplateCapture(const VansGITemplateCapture&)=delete;
        VansGITemplateCapture& operator=(const VansGITemplateCapture&)=delete;
        // 从 firstInput 开始准备预算允许的完整模板前缀；单个模板超限仍明确失败。
        // textureShader 由 world 在初始化阶段加载；票据保留它到 GPU 使用结束。
        uint32_t Prepare(VansVKDevice& device,const std::vector<GITemplateCaptureInput>& inputs,uint32_t firstInput,size_t byteBudget,
            const std::shared_ptr<VansComputeShader>& textureShader);
        void Submit();
        bool Poll(); // No fence wait. Also releases GPU-use-only objects on completion.
        void WaitForInitialization(); // Explicit blocking path for initial scene preparation only.
        bool Ready() const;
        bool Submitted() const;
        size_t EstimatedBytes(uint32_t input) const;
        // 单次消费完成的读回，返回独立 CPU 数据并立即释放读回 buffer。
        std::vector<GITemplateCaptureResult> Decode();
    private:
        struct State;
        std::unique_ptr<State> m_State;
    };
}
