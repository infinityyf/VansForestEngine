#include "VansShaderManager.h"
#include "../../EngineCore/EditorCore/AssetsSystem/VansAssetsFileWatcher.h"
#include "../Util/VansLog.h"

namespace VansGraphics
{
const std::unordered_map<std::string, std::string> VansShaderManager::s_EmptyPassMap;

VansShaderManager& VansShaderManager::Get()
{
    static VansShaderManager instance;
    return instance;
}

void VansShaderManager::RegisterShader(VansShaderEntry entry)
{
    if (entry.name.empty())
    {
        VANS_LOG_ERROR("[VansShaderManager] Cannot register unnamed shader");
        return;
    }

    auto [it, inserted] = m_Shaders.emplace(entry.name, VansShaderRecord{});
    if (!inserted)
    {
        VANS_LOG_WARN("[VansShaderManager] Shader '" << entry.name << "' already registered. Ignoring duplicate.");
        return;
    }
    it->second.entry = std::move(entry);
}

void VansShaderManager::RegisterGraphicsShader(const std::string& shaderName, VansShaderEntry entry)
{
    entry.name = shaderName;
    entry.kind = VansManagedShaderKind::Graphics;
    RegisterShader(std::move(entry));
}

void VansShaderManager::RegisterComputeShader(const std::string& shaderName, const std::string& relativePath, int pushConstantSize)
{
    VansShaderEntry entry;
    entry.name = shaderName;
    entry.relativePath = relativePath;
    entry.kind = VansManagedShaderKind::Compute;
    entry.pushConstantSize = pushConstantSize;
    RegisterShader(std::move(entry));
}

void VansShaderManager::RegisterRayTracingShader(const std::string& shaderName, const std::string& relativePath, int pushConstantSize)
{
    VansShaderEntry entry;
    entry.name = shaderName;
    entry.relativePath = relativePath;
    entry.kind = VansManagedShaderKind::RayTracing;
    entry.pushConstantSize = pushConstantSize;
    RegisterShader(std::move(entry));
}

const VansShaderEntry* VansShaderManager::FindShaderEntry(const std::string& shaderName) const
{
    auto it = m_Shaders.find(shaderName);
    return it == m_Shaders.end() ? nullptr : &it->second.entry;
}

VansShader* VansShaderManager::FindShader(const std::string& shaderName) const
{
    auto it = m_Shaders.find(shaderName);
    return it == m_Shaders.end() || !it->second.shader ? nullptr : it->second.shader.get();
}

VansGraphicsShader* VansShaderManager::FindGraphicsShader(const std::string& shaderName) const
{
    return dynamic_cast<VansGraphicsShader*>(FindShader(shaderName));
}

VansComputeShader* VansShaderManager::FindComputeShader(const std::string& shaderName) const
{
    return dynamic_cast<VansComputeShader*>(FindShader(shaderName));
}

VansRayTracingShader* VansShaderManager::FindRayTracingShader(const std::string& shaderName) const
{
    return dynamic_cast<VansRayTracingShader*>(FindShader(shaderName));
}

void VansShaderManager::RegisterMaterialPasses(VansMaterialType type, std::unordered_map<std::string, std::string> passMap)
{
    m_MaterialPasses[static_cast<int>(type)] = std::move(passMap);
}

const std::unordered_map<std::string, std::string>& VansShaderManager::GetMaterialPassMap(VansMaterialType type) const
{
    auto it = m_MaterialPasses.find(static_cast<int>(type));
    return it == m_MaterialPasses.end() ? s_EmptyPassMap : it->second;
}

bool VansShaderManager::LoadAll(const std::string& pathPrefix, VkDevice& device)
{
    bool allLoaded = true;
    for (auto& pair : m_Shaders)
    {
        if (!LoadShaderRecord(pair.second, pathPrefix, device))
            allLoaded = false;
    }
    return allLoaded;
}

bool VansShaderManager::LoadShaderRecord(VansShaderRecord& record, const std::string& pathPrefix, VkDevice& device)
{
    // Already loaded — skip re-initialisation (may be called from both
    // engine init and scene load paths).
    if (record.shader)
        return true;

    const std::string fullPath = pathPrefix + record.entry.relativePath;

    std::unique_ptr<VansShader> shader;
    switch (record.entry.kind)
    {
    case VansManagedShaderKind::Graphics:
        shader = std::make_unique<VansGraphicsShader>();
        break;
    case VansManagedShaderKind::Compute:
        shader = std::make_unique<VansComputeShader>();
        break;
    case VansManagedShaderKind::RayTracing:
        shader = std::make_unique<VansRayTracingShader>();
        break;
    }

    bool loaded = false;
    if (record.entry.kind == VansManagedShaderKind::RayTracing)
        loaded = shader->InitRayTracingShader(device, fullPath);
    else
        loaded = shader->InitShader(device, fullPath);

    if (!loaded)
    {
        record.status = record.shader ? VansShaderStatus::Fallback : VansShaderStatus::Broken;
        record.lastError = "initial compile/load failed: " + fullPath;
        VANS_LOG_ERROR("[VansShaderManager] " << record.lastError);
        return false;
    }

    shader->SetName(record.entry.name);
    if (record.entry.pushConstantSize > 0)
        shader->SetPushConstant(record.entry.pushConstantSize);
    if (auto* graphics = dynamic_cast<VansGraphicsShader*>(shader.get()))
        ApplyGraphicsState(*graphics, record.entry);

    record.shader = std::move(shader);
    record.status = VansShaderStatus::Valid;
    record.lastError.clear();
    return true;
}

void VansShaderManager::ApplyGraphicsState(VansGraphicsShader& shader, const VansShaderEntry& entry)
{
    shader.SetDrawStateData(entry.depthTest, entry.depthWrite, entry.depthCompareOp, entry.cullMode);
    if (entry.enableAlphaBlend)
        shader.SetEnableAlphaBlend(VK_TRUE);
    if (entry.enableDecalBlend)
        shader.SetEnableDecalBlend(VK_TRUE);
    if (entry.colorAttachmentCount > 0)
        shader.SetColorAttachmentCount(entry.colorAttachmentCount);
}

bool VansShaderManager::ReloadShader(const std::string& shaderName)
{
    auto it = m_Shaders.find(shaderName);
    if (it == m_Shaders.end() || !it->second.shader)
        return false;

    VansShaderRecord& record = it->second;
    if (!record.shader->RefreshShaderMoudle())
    {
        record.status = VansShaderStatus::Fallback;
        record.lastError = "reload failed, keeping last valid shader: " + shaderName;
        VANS_LOG_ERROR("[VansShaderManager] " << record.lastError);
        return false;
    }

    if (auto* graphics = dynamic_cast<VansGraphicsShader*>(record.shader.get()))
        graphics->TriggerReCreateGraphicsPipeline();
    else if (auto* compute = dynamic_cast<VansComputeShader*>(record.shader.get()))
        compute->TriggerReCreateComputePipeline();
    else if (auto* rayTracing = dynamic_cast<VansRayTracingShader*>(record.shader.get()))
        rayTracing->TriggerReCreateRayTracingPipeline();

    record.status = VansShaderStatus::Valid;
    record.lastError.clear();
    return true;
}

void VansShaderManager::ReloadUpdatedShaders(VansAssetsFileWatcher& watcher)
{
    for (auto& pair : m_Shaders)
    {
        VansShaderRecord& record = pair.second;
        if (record.shader && watcher.ConsumeUpdated(record.shader->GetShaderFolder()))
            ReloadShader(record.entry.name);
    }
}

void VansShaderManager::ForEachShader(const std::function<void(const VansShaderRecord&)>& fn) const
{
    for (const auto& pair : m_Shaders)
        fn(pair.second);
}

std::vector<VansShader*> VansShaderManager::GetLoadedShaderAssets() const
{
    std::vector<VansShader*> result;
    result.reserve(m_Shaders.size());
    for (const auto& pair : m_Shaders)
    {
        if (pair.second.shader)
            result.push_back(pair.second.shader.get());
    }
    return result;
}

void VansShaderManager::Clear()
{
    m_Shaders.clear();
    m_MaterialPasses.clear();
}
}
