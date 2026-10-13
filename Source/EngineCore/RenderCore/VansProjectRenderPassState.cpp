#include "VansProjectRenderPassState.h"
#include "../RuntimeCore/VansThreadContract.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace VansGraphics
{
namespace
{
bool Error(std::string& error, const char* message) { error = message; return false; }
bool Finite(const VansProjectDrawInstance& instance)
{
    for (int column = 0; column < 4; ++column) for (int row = 0; row < 4; ++row)
        if (!std::isfinite(instance.model[column][row])) return false;
    for (int component = 0; component < 4; ++component)
        if (!std::isfinite(instance.color[component]) || !std::isfinite(instance.data[component])) return false;
    return true;
}
VansProjectRenderPassFrameInput::Pass InitialInput(const Vans::VansRenderPassAsset& definition)
{
    VansProjectRenderPassFrameInput::Pass input;
    input.enabled = definition.enabled;
    std::size_t parameterSize = 16;
    for (const auto& parameter : definition.parameters)
        parameterSize = std::max(parameterSize, parameter.offset + parameter.initialValue.size() * sizeof(float));
    input.parameters.resize((parameterSize + 15) & ~std::size_t(15));
    for (const auto& parameter : definition.parameters)
        std::memcpy(input.parameters.data() + parameter.offset, parameter.initialValue.data(), parameter.initialValue.size() * sizeof(float));
    input.buffers.resize(definition.buffers.size());
    return input;
}
}
bool VansProjectRenderPassState::Configure(const Vans::VansSceneResourceBuildPlan& resources, std::string& error)
{
    VANS_ASSERT_MAIN_THREAD();
    auto config = std::make_shared<VansProjectRenderPassConfiguration>();
    std::vector<VansProjectRenderPassFrameInput::Pass> inputs;
    std::unordered_set<std::string> ids, channels, guids;
    std::unordered_set<std::uint32_t> exportSlots;
    if (resources.projectRenderPasses.size() > 64) return Error(error, "Project Pass count exceeds 64");
    for (const auto& request : resources.projectRenderPasses)
    {
        VansProjectRenderPassConfiguration::Pass pass;
        pass.guid = request.guid;
        if (!pass.guid.IsValid() || !Vans::ReadRenderPassAsset(Vans::WriteRenderPassAsset(request.definition), pass.definition, error) ||
            !ids.insert(pass.definition.passId).second || !guids.insert(pass.guid.ToString()).second) return Error(error, "Invalid project Pass definition or duplicate passId");
        for (const auto& shader : pass.definition.shaders)
        {
            const auto found = std::find_if(resources.shaders.begin(), resources.shaders.end(), [&](const auto& s) { return s.assetGuid == shader.ToString(); });
            if (found == resources.shaders.end() || found->kind != "graphics" || found->pushConstantSize != 0)
                return Error(error, "Project Pass requires a resolved graphics Shader without push constants");
            if (found->name.empty() || std::count_if(resources.shaders.begin(), resources.shaders.end(),
                [&](const auto& candidate) { return candidate.name == found->name; }) != 1)
                return Error(error, "Project Shader runtime name must resolve uniquely");
            pass.shaderNames.push_back(found->name);
        }
        for (const auto& texture : pass.definition.textures)
        {
            const auto found = std::find_if(resources.textures.begin(), resources.textures.end(),
                [&](const auto& t) { return t.assetGuid == texture.asset.ToString(); });
            if (found == resources.textures.end() || found->textureType != 0 || found->name.empty() ||
                std::count_if(resources.textures.begin(), resources.textures.end(),
                    [&](const auto& t) { return t.name == found->name; }) != 1)
                return Error(error, "Project Pass requires a uniquely resolved 2D Texture");
            pass.textureNames.push_back(found->name);
        }
        if (pass.definition.source == Vans::VansRenderPassSource::ProjectInstances)
        {
            const auto mesh = std::find_if(resources.meshes.begin(), resources.meshes.end(), [&](const auto& m) { return m.assetGuid == pass.definition.instanceMesh.ToString(); });
            if (mesh == resources.meshes.end() || mesh->loadMultiMesh) return Error(error, "Project instances require one resolved mesh geometry");
            pass.meshName = mesh->name;
        }
        for (const auto& target : pass.definition.targets)
            if (!target.exportChannel.empty() && (!channels.insert(target.exportChannel).second || !exportSlots.insert(target.exportSlot).second))
                return Error(error, "Duplicate project output channel or Shader slot");
        config->passes.push_back(std::move(pass));
    }
    std::sort(config->passes.begin(), config->passes.end(), [](const auto& a, const auto& b) {
        return a.definition.order == b.definition.order ? a.definition.passId < b.definition.passId : a.definition.order < b.definition.order;
    });
    for (const auto& pass : config->passes)
        inputs.push_back(InitialInput(pass.definition));
    config->revision = ++m_Revision;
    m_Configuration = std::move(config);
    m_Inputs = std::move(inputs);
    error.clear();
    return true;
}
void VansProjectRenderPassState::ResetForScene()
{
    VANS_ASSERT_MAIN_THREAD();
    // Play/Stop 与场景切换保留项目配置，清空旧场景的输入并恢复作者初值。
    m_Inputs.clear();
    if (m_Configuration) for (const auto& pass : m_Configuration->passes)
        m_Inputs.push_back(InitialInput(pass.definition));
}
void VansProjectRenderPassState::Clear() { m_Configuration.reset(); m_Inputs.clear(); ++m_Revision; }
std::size_t VansProjectRenderPassState::FindPass(const std::string& id) const
{
    if (m_Configuration) for (std::size_t i = 0; i < m_Configuration->passes.size(); ++i)
        if (m_Configuration->passes[i].definition.passId == id) return i;
    return SIZE_MAX;
}
bool VansProjectRenderPassState::SetEnabled(const std::string& id, bool enabled, std::string& error)
{
    VANS_ASSERT_MAIN_THREAD();
    const auto i = FindPass(id);
    if (i == SIZE_MAX) return Error(error, "Unknown project Pass");
    m_Inputs[i].enabled = enabled;
    error.clear(); return true;
}
bool VansProjectRenderPassState::TryGetEnabled(const std::string& id, bool& enabled) const
{
    const auto i = FindPass(id);
    if (i == SIZE_MAX) return false;
    enabled = m_Inputs[i].enabled; return true;
}
bool VansProjectRenderPassState::SetParameter(const std::string& id, const std::string& name, const std::vector<float>& values, std::string& error)
{
    VANS_ASSERT_MAIN_THREAD();
    const auto i = FindPass(id);
    if (i == SIZE_MAX) return Error(error, "Unknown project Pass");
    const auto& parameters = m_Configuration->passes[i].definition.parameters;
    if (name == m_Configuration->passes[i].definition.cameraWorldRect)
        return Error(error, "Camera world rectangle is render-owned; edit its asset range instead");
    const auto p = std::find_if(parameters.begin(), parameters.end(), [&](const auto& value) { return value.name == name; });
    if (p == parameters.end() || p->initialValue.size() != values.size() ||
        std::any_of(values.begin(), values.end(), [](float value) { return !std::isfinite(value); })) return Error(error, "Parameter shape or values do not match declaration");
    std::memcpy(m_Inputs[i].parameters.data() + p->offset, values.data(), values.size() * sizeof(float));
    error.clear(); return true;
}
bool VansProjectRenderPassState::SetBuffer(const std::string& id, const std::string& name, const std::vector<std::uint8_t>& bytes, std::string& error)
{
    VANS_ASSERT_MAIN_THREAD();
    const auto i = FindPass(id);
    if (i == SIZE_MAX) return Error(error, "Unknown project Pass");
    const auto& buffers = m_Configuration->passes[i].definition.buffers;
    const auto b = std::find_if(buffers.begin(), buffers.end(), [&](const auto& value) { return value.name == name; });
    if (b == buffers.end() || b->role == "instanceInput" || bytes.size() % b->stride != 0 || bytes.size() / b->stride > b->maxElements)
        return Error(error, "Input buffer must match declared stride/capacity and role");
    m_Inputs[i].buffers[b - buffers.begin()] = bytes;
    error.clear(); return true;
}
bool VansProjectRenderPassState::SubmitInstances(const std::string& id, const std::vector<VansProjectDrawInstance>& instances, std::string& error)
{
    VANS_ASSERT_MAIN_THREAD();
    const auto i = FindPass(id);
    if (i == SIZE_MAX) return Error(error, "Unknown project Pass");
    const auto& pass = m_Configuration->passes[i].definition;
    const auto b = std::find_if(pass.buffers.begin(), pass.buffers.end(), [](const auto& value) { return value.role == "instanceInput"; });
    if (pass.source != Vans::VansRenderPassSource::ProjectInstances || b == pass.buffers.end() ||
        instances.size() > b->maxElements - m_Inputs[i].instanceCount ||
        std::any_of(instances.begin(), instances.end(), [](const auto& value) { return !Finite(value); })) return Error(error, "Instance stream unavailable, nonfinite or capacity exceeded (rejectNew)");
    auto& bytes = m_Inputs[i].buffers[b - pass.buffers.begin()];
    const auto offset = bytes.size();
    bytes.resize(offset + instances.size() * sizeof(VansProjectDrawInstance));
    if (!instances.empty()) std::memcpy(bytes.data() + offset, instances.data(), instances.size() * sizeof(VansProjectDrawInstance));
    m_Inputs[i].instanceCount += static_cast<std::uint32_t>(instances.size());
    error.clear(); return true;
}
VansProjectRenderPassFrameInput VansProjectRenderPassState::CaptureFrame(const glm::vec3& cameraPosition)
{
    VANS_ASSERT_MAIN_THREAD();
    VansProjectRenderPassFrameInput frame{m_Configuration, m_Inputs};
    if (m_Configuration) for (std::size_t i = 0; i < frame.passes.size(); ++i)
    {
        const auto& definition = m_Configuration->passes[i].definition;
        if (definition.cameraWorldRect.empty()) continue;
        const auto parameter = std::find_if(definition.parameters.begin(), definition.parameters.end(),
            [&](const auto& p) { return p.name == definition.cameraWorldRect; });
        auto rectangle = parameter->initialValue;
        auto& input = frame.passes[i];
        Vans::VansWorldTextureWindow window;
        window.worldRect={rectangle[0],rectangle[1],rectangle[2],rectangle[3]};
        window.resolution={definition.targets.front().width,definition.targets.front().height};
        window.cameraCentered=true;
        glm::vec4 resolved;
        input.worldWindowValid=Vans::ResolveWorldTextureWindow(window,cameraPosition,resolved,input.worldTexelOrigin);
        if (input.worldWindowValid)
            std::memcpy(input.parameters.data() + parameter->offset, &resolved, 4 * sizeof(float));
    }
    for (auto& input : m_Inputs) { for (auto& buffer : input.buffers) buffer.clear(); input.instanceCount = 0; }
    return frame;
}
bool VansProjectRenderPassState::GetOutputKey(const std::string& id, const std::string& target, std::string& key) const
{
    const auto i = FindPass(id);
    if (i == SIZE_MAX) return false;
    const auto& pass = m_Configuration->passes[i];
    if (std::none_of(pass.definition.targets.begin(), pass.definition.targets.end(), [&](const auto& value) { return value.name == target; })) return false;
    key = "project:" + pass.guid.ToString() + "/" + target;
    return true;
}
bool VansProjectRenderPassState::GetOutputWindow(const std::string& id, const std::string& target, Vans::VansWorldTextureWindow& window) const
{
    const auto i=FindPass(id); if (i==SIZE_MAX) return false;
    const auto& pass=m_Configuration->passes[i].definition;
    const auto t=std::find_if(pass.targets.begin(),pass.targets.end(),[&](const auto& v){return v.name==target;});
    const auto p=std::find_if(pass.parameters.begin(),pass.parameters.end(),[&](const auto& v){return v.name==pass.cameraWorldRect;});
    if (t==pass.targets.end() || pass.cameraWorldRect.empty() || p==pass.parameters.end()) return false;
    window.worldRect={p->initialValue[0],p->initialValue[1],p->initialValue[2],p->initialValue[3]};
    window.resolution={t->width,t->height}; window.cameraCentered=true; return true;
}
}
