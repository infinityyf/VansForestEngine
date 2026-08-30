#include "VansShaderManager.h"
#include "../AssetCore/Importers/Shader/VansShaderArtifactCache.h"
#include "../Util/VansLog.h"

#include <algorithm>

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
    it->second.pipelineDesc = BuildPipelineDesc(entry, entry.relativePath);
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

void VansShaderManager::RegisterComputeShaderFile(
	const std::string& shaderName,
	const std::string& relativePath,
	const std::string& computeFile,
	int pushConstantSize)
{
	VansShaderEntry entry;
	entry.name = shaderName;
	entry.relativePath = relativePath;
	entry.kind = VansManagedShaderKind::Compute;
	entry.pushConstantSize = pushConstantSize;
	entry.explicitStageFiles[VK_SHADER_STAGE_COMPUTE_BIT] = computeFile;
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

const VansPipelineProgramDesc* VansShaderManager::FindPipelineDesc(const std::string& shaderName) const
{
    auto it = m_Shaders.find(shaderName);
    return it == m_Shaders.end() ? nullptr : &it->second.pipelineDesc;
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
    for (const auto& [passName, shaderName] : passMap)
    {
        auto shaderIt = m_Shaders.find(shaderName);
        if (shaderIt == m_Shaders.end())
            continue;

        auto& passes = shaderIt->second.entry.materialPasses;
        if (std::find(passes.begin(), passes.end(), passName) == passes.end())
            passes.emplace_back(passName);

        auto& descPasses = shaderIt->second.pipelineDesc.materialPasses;
        if (std::find(descPasses.begin(), descPasses.end(), passName) == descPasses.end())
            descPasses.emplace_back(passName);
    }

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
    record.pipelineDesc = BuildPipelineDesc(record.entry, fullPath);

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

	// Establish the stable program id before artifact lookup. Direct subsystem
	// shaders still fall back to their folder identity, while managed shaders and
	// Editor hot reload now address the same artifact namespace.
	shader->SetName(record.entry.name);
	shader->SetPipelineProgramDesc(record.pipelineDesc);

    bool loaded = false;
    if (record.entry.kind == VansManagedShaderKind::RayTracing)
        loaded = shader->InitRayTracingShader(device, fullPath, record.entry.explicitStageFiles);
    else
        loaded = shader->InitShader(device, fullPath, record.entry.explicitStageFiles);

    if (!loaded)
    {
        record.status = record.shader ? VansShaderStatus::Fallback : VansShaderStatus::Broken;
        record.lastError = "initial compile/load failed: " + fullPath;
        VANS_LOG_ERROR("[VansShaderManager] " << record.lastError);
        return false;
    }

    shader->SetName(record.entry.name);
    shader->SetPipelineProgramDesc(record.pipelineDesc);
	record.pipelineDesc.shaderBinaryHash = shader->GetShaderBinaryHash();
    if (record.entry.pushConstantSize > 0)
        shader->SetPushConstant(record.entry.pushConstantSize);
    if (auto* graphics = dynamic_cast<VansGraphicsShader*>(shader.get()))
        ApplyGraphicsState(*graphics, record.pipelineDesc);

    record.shader = std::move(shader);
    record.status = VansShaderStatus::Valid;
    record.lastError.clear();
    return true;
}

VansPipelineProgramDesc VansShaderManager::BuildPipelineDesc(
    const VansShaderEntry& entry,
    const std::string& fullPath) const
{
    VansPipelineProgramDesc desc{};
    desc.name = entry.name;
    desc.shaderPath = fullPath;
    desc.pushConstantSize = entry.pushConstantSize;
    desc.materialPasses = entry.materialPasses;

    switch (entry.kind)
    {
    case VansManagedShaderKind::Graphics:
        desc.kind = VansPipelineProgramKind::Graphics;
        break;
    case VansManagedShaderKind::Compute:
        desc.kind = VansPipelineProgramKind::Compute;
        break;
    case VansManagedShaderKind::RayTracing:
        desc.kind = VansPipelineProgramKind::RayTracing;
        break;
    }

    desc.graphicsState.depthTest = entry.depthTest;
    desc.graphicsState.depthWrite = entry.depthWrite;
    desc.graphicsState.depthCompareOp = entry.depthCompareOp;
    desc.graphicsState.cullMode = entry.cullMode;
    desc.graphicsState.colorAttachmentCount = entry.colorAttachmentCount;
    desc.graphicsState.polygonMode = entry.polygonMode;
    desc.graphicsState.frontFace = entry.frontFace;
    desc.graphicsState.primitiveTopology = entry.primitiveTopology;
    desc.graphicsState.patchControlPoints = entry.patchControlPoints;
    desc.graphicsState.enableAlphaBlend = entry.enableAlphaBlend;
    desc.graphicsState.enableDecalBlend = entry.enableDecalBlend;
    desc.graphicsState.enableAdditiveBlend = entry.enableAdditiveBlend;
    desc.graphicsState.additiveBlendAttachmentMask = entry.additiveBlendAttachmentMask;
    desc.graphicsState.enablePremultipliedAlphaBlend = entry.enablePremultipliedAlphaBlend;
    desc.stages = VansPipelineDescriptorBuilder::BuildStageFiles(
        fullPath,
        desc.kind,
        entry.explicitStageFiles);

    return desc;
}

void VansShaderManager::ApplyGraphicsState(VansGraphicsShader& shader, const VansPipelineProgramDesc& desc) const
{
    shader.ApplyGraphicsStateDesc(desc.graphicsState);
}

bool VansShaderManager::ConfigureGraphicsShader(
    VansGraphicsShader& shader,
    const std::string& shaderName,
    const std::string& fullPath) const
{
    const VansShaderEntry* entry = FindShaderEntry(shaderName);
    if (entry == nullptr || entry->kind != VansManagedShaderKind::Graphics)
    {
        VANS_LOG_ERROR("[VansShaderManager] Missing graphics shader registration for '" << shaderName << "'");
        return false;
    }

    return ConfigureGraphicsShader(shader, *entry, fullPath);
}

bool VansShaderManager::ConfigureGraphicsShader(
    VansGraphicsShader& shader,
    const VansShaderEntry& entry,
    const std::string& fullPath) const
{
    if (entry.kind != VansManagedShaderKind::Graphics)
    {
        VANS_LOG_ERROR("[VansShaderManager] Shader entry '" << entry.name << "' is not a graphics shader");
        return false;
    }

    VansPipelineProgramDesc desc = BuildPipelineDesc(entry, fullPath);
    shader.SetName(entry.name);
    shader.SetPipelineProgramDesc(desc);
    if (entry.pushConstantSize > 0)
        shader.SetPushConstant(entry.pushConstantSize);
    ApplyGraphicsState(shader, desc);
    return true;
}

bool VansShaderManager::ApplyCompiledShaderCandidate(
    const std::string& shaderName,
    const std::map<VkShaderStageFlagBits, std::vector<std::uint32_t>>& stageSpirv,
    std::string& error)
{
    auto it = m_Shaders.find(shaderName);
    if (it == m_Shaders.end() || !it->second.shader)
    {
        error = "shader program is not loaded: " + shaderName;
        return false;
    }

    VansShaderRecord& record = it->second;
    if (!record.shader->ReplaceShaderModulesFromSPIRV(stageSpirv, error))
    {
        record.status = VansShaderStatus::Fallback;
        record.lastError = error;
        return false;
    }
	record.pipelineDesc.shaderBinaryHash = record.shader->GetShaderBinaryHash();

    if (auto* graphics = dynamic_cast<VansGraphicsShader*>(record.shader.get()))
        graphics->TriggerReCreateGraphicsPipeline();
    else if (auto* compute = dynamic_cast<VansComputeShader*>(record.shader.get()))
        compute->TriggerReCreateComputePipeline();
    else if (auto* rayTracing = dynamic_cast<VansRayTracingShader*>(record.shader.get()))
        rayTracing->TriggerReCreateRayTracingPipeline();

    record.status = VansShaderStatus::Valid;
    record.lastError.clear();
    error.clear();
    return true;
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

bool VansShaderManager::ExportCookedShaderArtifacts(
	const std::string& destinationRoot,
	std::string& error) const
{
	std::vector<Vans::VansShaderCookProgram> programs;
	for (const auto& pair : m_Shaders)
	{
		const VansShaderRecord& record = pair.second;
		if (!record.shader || record.status != VansShaderStatus::Valid)
			continue;
		Vans::VansShaderCompileRequest request;
		request.programId = record.entry.name;
		request.sourceFolder = record.shader->GetShaderFolder();
		programs.push_back({
			record.entry.name,
			Vans::VansShaderArtifactCache::ResolveArtifactRoot(request)
		});
	}
	return Vans::VansShaderArtifactCache::Get().ExportCookedArtifacts(
		programs, destinationRoot, error);
}

void VansShaderManager::ReleaseLoadedShaderAssets()
{
    for (auto& pair : m_Shaders)
    {
        VansShaderRecord& record = pair.second;
        record.shader.reset();
        record.status = VansShaderStatus::Unloaded;
        record.lastError.clear();
    }
}

void VansShaderManager::Clear()
{
	const Vans::VansShaderArtifactCacheStats stats = Vans::VansShaderArtifactCache::Get().GetStats();
	VANS_LOG("[ShaderArtifact] Session stats: hits=" << stats.hits
		<< " misses=" << stats.misses
		<< " compiles=" << stats.compiles
		<< " failures=" << stats.compileFailures);
    m_Shaders.clear();
    m_MaterialPasses.clear();
}
}
