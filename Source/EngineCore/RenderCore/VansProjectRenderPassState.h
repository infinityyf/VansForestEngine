#pragma once
#include "../SceneCore/VansSceneResourcePlan.h"
#include "VansRenderProxyHandle.h"
#include "../Util/VansWorldTextureWindow.h"
#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace VansGraphics
{
struct alignas(16) VansProjectDrawInstance
{
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};
    glm::vec4 data{0.0f};
};
static_assert(sizeof(VansProjectDrawInstance) == 96, "projectDrawInstance std430 ABI");

struct VansProjectRenderPassConfiguration
{
    struct Pass
    {
        Vans::VansAssetGuid guid;
        Vans::VansRenderPassAsset definition;
        std::vector<std::string> shaderNames; // Asset GUID resolution performed once by the resource plan.
        std::string meshName;
        std::vector<std::string> textureNames;
    };
    std::uint64_t revision = 0;
    std::vector<Pass> passes;
};

enum class VansProjectSceneBucket : std::uint8_t {Opaque, ForwardOpaque, Transparent, Hair, Decal, PostProcess};
struct VansProjectSceneDrawCandidate
{
    VansRenderProxyHandle proxy;
    VansProjectSceneBucket bucket = VansProjectSceneBucket::Opaque;
    std::uint32_t nodeIndex = 0;
    std::uint32_t shaderIndex = 0;
};
struct VansProjectRenderPassFilterStats
{
    std::uint32_t candidates = 0, inactive = 0, unmatchedShader = 0, excludedLayer = 0;
    std::uint32_t missingProxy = 0, unsupportedDynamic = 0, capacityExceeded = 0, cameraCulled = 0;
};
struct VansProjectRenderPassFrameInput
{
    struct Pass
    {
        bool enabled = true;
        std::vector<std::uint8_t> parameters;
        std::vector<std::vector<std::uint8_t>> buffers;
        std::vector<VansProjectSceneDrawCandidate> sceneDraws;
        std::uint32_t instanceCount = 0;
        VansProjectRenderPassFilterStats filter;
        std::array<std::int64_t, 2> worldTexelOrigin{};
        bool worldWindowValid = true;
    };
    std::shared_ptr<const VansProjectRenderPassConfiguration> configuration;
    std::vector<Pass> passes;
};

// Main-owned API state. GPU resources and descriptor handles belong to the Vulkan owner.
class VansProjectRenderPassState final
{
public:
    bool Configure(const Vans::VansSceneResourceBuildPlan& resources, std::string& error);
    void ResetForScene();
    void Clear();
    bool SetEnabled(const std::string& passId, bool enabled, std::string& error);
    bool SetParameter(const std::string& passId, const std::string& name, const std::vector<float>& values, std::string& error);
    bool SetBuffer(const std::string& passId, const std::string& name, const std::vector<std::uint8_t>& bytes, std::string& error);
    bool SubmitInstances(const std::string& passId, const std::vector<VansProjectDrawInstance>& instances, std::string& error);
    VansProjectRenderPassFrameInput CaptureFrame(const glm::vec3& cameraPosition = {});
    const std::shared_ptr<const VansProjectRenderPassConfiguration>& Configuration() const { return m_Configuration; }
    bool TryGetEnabled(const std::string& passId, bool& enabled) const;
    bool GetOutputKey(const std::string& passId, const std::string& target, std::string& key) const;
    bool GetOutputWindow(const std::string& passId, const std::string& target, Vans::VansWorldTextureWindow& window) const;
private:
    std::size_t FindPass(const std::string& passId) const;
    std::shared_ptr<const VansProjectRenderPassConfiguration> m_Configuration;
    std::vector<VansProjectRenderPassFrameInput::Pass> m_Inputs;
    std::uint64_t m_Revision = 0;
};
}
