#pragma once

#include "VansAssetGuid.h"
#include "Serialization/VansSerializedValue.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Vans
{
inline constexpr std::uint32_t VansProjectRenderTextureSlotCount = 16;
enum class VansRenderPassSource { Scene, ProjectInstances };
enum class VansRenderTargetLifetime { FrameLocal, Persistent };
enum class VansRenderTargetLoad { Clear, Load, CopyPrevious };
enum class VansRenderTargetFormat { R8, RGBA8, RGBA16F, RGBA32F, D32 };
enum VansRenderTargetUsage : std::uint32_t
{
    RenderTargetColorAttachment = 1u,
    RenderTargetDepthAttachment = 2u,
    RenderTargetSampled = 4u,
    RenderTargetTransferSource = 8u,
    RenderTargetTransferDestination = 16u
};

struct VansRenderTargetAsset
{
    std::string name;
    std::string exportChannel;
    std::uint32_t exportSlot = UINT32_MAX;
    VansRenderTargetFormat format = VansRenderTargetFormat::RGBA8;
    VansRenderTargetLifetime lifetime = VansRenderTargetLifetime::FrameLocal;
    VansRenderTargetLoad load = VansRenderTargetLoad::Clear;
    std::uint32_t usage = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float resolutionScale = 1.0f;
    bool followsRenderResolution = false;
    std::array<float, 4> clear = {};
};

struct VansRenderPassBufferAsset
{
    std::string name;
    std::string role;
    std::string layout;
    std::uint32_t binding = 0;
    std::uint32_t stride = 0;
    std::uint32_t maxElements = 0;
};

struct VansRenderPassParameterAsset
{
    std::string name;
    std::uint32_t offset = 0;
    std::vector<float> initialValue;
};

// 静态采样输入进入同一 GUID 依赖闭包；描述符和生命周期仍由 RenderCore 管理。
struct VansRenderPassTextureAsset
{
    std::string name;
    VansAssetGuid asset;
    std::uint32_t binding = 0;
};

// 作者态只保存资源声明和 GUID；GPU handle、帧槽、历史图像由运行时 owner 管理。
struct VansRenderPassAsset
{
    std::string passId;
    std::int32_t order = 0;
    bool enabled = true;
    VansRenderPassSource source = VansRenderPassSource::Scene;
    std::string instanceStream;
    VansAssetGuid instanceMesh;
    std::vector<VansAssetGuid> shaders;
    std::vector<std::string> layers;
    bool cameraVisibleOnly = false;
    bool previousFrameHistory = false;
    // 可选 vec4(minX,minZ,rangeX,rangeZ) 参数；由最终渲染相机驱动并对齐世界 texel 网格。
    std::string cameraWorldRect;
    std::uint64_t budgetBytes = 64u * 1024u * 1024u;
    std::uint32_t maxDraws = 65536;
    std::vector<VansRenderTargetAsset> targets;
    std::vector<VansRenderPassBufferAsset> buffers;
    std::vector<VansRenderPassTextureAsset> textures;
    std::vector<VansRenderPassParameterAsset> parameters;
};

// 失败不修改输出；所有消费者共享同一个解码和资源依赖合同。
bool ReadRenderPassAsset(const VansSerializedValue& root, VansRenderPassAsset& asset, std::string& error);
VansSerializedValue WriteRenderPassAsset(const VansRenderPassAsset& asset);
std::vector<VansAssetGuid> CollectRenderPassAssetDependencies(const VansRenderPassAsset& asset);
}
