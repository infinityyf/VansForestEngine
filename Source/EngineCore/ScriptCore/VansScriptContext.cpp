#include "VansScriptContext.h"
#include "../RuntimeCore/VansFramePhase.h"
#include "../RuntimeCore/VansThreadContract.h"
#include "../Configration/VansConfigration.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../Util/VansLog.h"
#include "../VansTimer.h"
#include "../RenderCore/VansScene.h"
#include "../RenderCore/VansCamera.h"
#include "VansTransform.h"
#include "../PhysicsCore/VansPhysics.h"
#include "../PhysicsCore/VansPhysicsNode.h"
#include "../PhysicsCore/VansClothNode.h"
#include "../PhysicsCore/VansCharacterControllerNode.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../AudioCore/VansAudioManager.h"
#include "../RenderCore/VansVideoManager.h"
#include "../Util/VansProfiler.h"
#include "../RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include <cstdlib>
#include <fstream>
#include <string>
#include <algorithm>
#include <cctype>
#include <glm/glm.hpp>
#include "../../../../ForestExporter/VansEngineBridge.h"
#include "../../../../ForestExporter/VansInputBridge.h"
#include "../../../../ForestExporter/VansPhysicsEventInfo.h"

// Defined in VansScriptBridge.cpp
extern void VansInitEngineBridge();
extern VansEngineBridge* VansGetEngineBridgePtr();
extern void VansInitInputBridge();
extern VansInputBridge* VansGetInputBridgePtr();
namespace py = pybind11;

// Singleton instance
VansScriptContext* VansScriptContext::s_Instance = nullptr;

namespace
{
py::module LoadScriptModuleFromFile(
    const std::filesystem::path& filePath,
    const std::string& moduleName,
    bool publish)
{
    py::module importlibUtil = py::module::import("importlib.util");
    py::dict modules = py::module::import("sys").attr("modules");
    const bool hadPrevious = modules.contains(moduleName.c_str());
    py::object previous;
    if (hadPrevious)
        previous = modules[py::str(moduleName)];

    try
    {
        py::object spec = importlibUtil.attr("spec_from_file_location")(
            moduleName, filePath.string());
        if (spec.is_none())
            throw std::runtime_error("could not create a module spec");

        py::object moduleObject = importlibUtil.attr("module_from_spec")(spec);
        modules[py::str(moduleName)] = moduleObject;
        spec.attr("loader").attr("exec_module")(moduleObject);

        if (!publish)
        {
            if (hadPrevious)
                modules[py::str(moduleName)] = previous;
            else
                modules.attr("pop")(moduleName, py::none());
        }
        return moduleObject.cast<py::module>();
    }
    catch (...)
    {
        if (hadPrevious)
            modules[py::str(moduleName)] = previous;
        else
            modules.attr("pop")(moduleName, py::none());
        throw;
    }
}
}

VansScriptContext::~VansScriptContext()
{
    ShutdownPython();
}

void VansScriptContext::AssertPythonThread() const
{
    VANS_ASSERT_MAIN_THREAD();
    assert(m_PythonThreadId == std::thread::id{} ||
           m_PythonThreadId == std::this_thread::get_id());
}

std::string VansScriptContext::CanonicalScriptKey(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec)
        canonical = std::filesystem::absolute(path, ec).lexically_normal();

    std::string key = canonical.generic_string();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
#endif
    return key;
}

std::string VansScriptContext::MakeScriptModuleName(const std::string& canonicalPath)
{
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char value : canonicalPath)
    {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return "_vans_script_" + std::to_string(hash);
}

VansScriptObject::~VansScriptObject()
{
    for (auto* component : m_Components)
        delete component;
    m_Components.clear();

    if (m_OwnsTransform)
        VansGraphics::VansTransformStore::FreeTransform(m_TransformID);
}

// ---------------------------------------------------------------------------
// VansScriptRagdollComponent — 运行时布娃娃控制接口
// ---------------------------------------------------------------------------
void VansScriptRagdollComponent::SetDriveMode(int mode)
{
    SetDriveModeWithVelocity(mode, 0.0f, 0.0f, 0.0f);
}

void VansScriptRagdollComponent::SetDriveModeWithVelocity(int mode, float vx, float vy, float vz)
{
    if (m_AnimNode == nullptr)
    {
        VANS_LOG_WARN("[RagdollComp] SetDriveMode 失败：m_AnimNode 为空");
        return;
    }

    VansEngine::RagdollDriveMode driveMode = VansEngine::RagdollDriveMode::Animation;
    if (mode == 1)
        driveMode = VansEngine::RagdollDriveMode::Physics;
    else if (mode == 2)
        driveMode = VansEngine::RagdollDriveMode::Blend;

    VansEngine::VansRagdollSystem::GetInstance().SetDriveMode(
        m_AnimNode, driveMode, glm::vec3(vx, vy, vz));
}

int VansScriptRagdollComponent::GetDriveMode() const
{
    if (m_AnimNode == nullptr)
        return 0;

    VansEngine::RagdollDriveMode mode =
        VansEngine::VansRagdollSystem::GetInstance().GetDriveMode(m_AnimNode);
    return static_cast<int>(mode);
}

void VansScriptRagdollComponent::SetBlendWeight(float weight)
{
    if (m_AnimNode == nullptr)
        return;
    VansEngine::VansRagdollSystem::GetInstance().SetBlendWeight(m_AnimNode, weight);
}

float VansScriptRagdollComponent::GetBlendWeight() const
{
    if (m_AnimNode == nullptr)
        return 0.0f;
    return VansEngine::VansRagdollSystem::GetInstance().GetBlendWeight(m_AnimNode);
}

bool VansScriptRagdollComponent::HasRuntimeRagdoll() const
{
    if (m_AnimNode == nullptr)
        return false;
    return VansEngine::VansRagdollSystem::GetInstance().HasRagdoll(m_AnimNode);
}

int VansScriptRagdollComponent::GetRuntimeBodyCount() const
{
    if (m_AnimNode == nullptr)
        return 0;
    return VansEngine::VansRagdollSystem::GetInstance().GetBodyCount(m_AnimNode);
}

int VansScriptRagdollComponent::GetRuntimeJointCount() const
{
    if (m_AnimNode == nullptr)
        return 0;
    return VansEngine::VansRagdollSystem::GetInstance().GetJointCount(m_AnimNode);
}

void VansScriptRagdollComponent::ApplyImpulse(const std::string& boneName, float ix, float iy, float iz)
{
    if (m_AnimNode == nullptr)
        return;

    VansEngine::VansRagdollSystem::GetInstance().ApplyImpulse(
        m_AnimNode, boneName, glm::vec3(ix, iy, iz));
}

// ---------------------------------------------------------------------------
// VansScriptCharacterControllerComponent — Ragdoll 接管接口
// ---------------------------------------------------------------------------
void VansScriptCharacterControllerComponent::BindFollowRagdoll(
    VansScriptRagdollComponent* ragdollComp, const std::string& rootBone)
{
    if (!m_ControllerNode || !ragdollComp || !ragdollComp->m_AnimNode) return;
    m_ControllerNode->SetFollowRagdoll(ragdollComp->m_AnimNode, rootBone);
}

void VansScriptCharacterControllerComponent::ClearFollowRagdoll()
{
    if (m_ControllerNode) m_ControllerNode->ClearFollowRagdoll();
}

bool VansScriptCharacterControllerComponent::IsFollowRagdollEnabled() const
{
    return m_ControllerNode && m_ControllerNode->IsFollowRagdollEnabled();
}

// ---------------------------------------------------------------------------
// VansScriptAudioComponent — SwitchSource
// ---------------------------------------------------------------------------
bool VansScriptAudioComponent::SwitchSource(const std::string& name)
{
	VANS_LOG("[AudioComp] SwitchSource 进入，目标='" << name << "' 当前节点=" << (m_AudioNode ? m_AudioNode->GetName() : "null"));

	if (!m_AudioManager)
	{
		VANS_LOG_WARN("[AudioComp] SwitchSource 失败：m_AudioManager 未绑定");
		return false;
	}

	VansEngine::VansAudioNode* newNode = m_AudioManager->Get(name);
	if (!newNode)
	{
		VANS_LOG_WARN("[AudioComp] SwitchSource 失败：资源 '" << name << "' 未在 AudioManager 中找到");
		return false;
	}

	VANS_LOG("[AudioComp] SwitchSource newNode 已找到='" << name << "' IsBound=" << newNode->IsBound());

	// 安全停止当前播放（Manager 拥有生命周期，不销毁旧节点）
	if (m_AudioNode && m_AudioNode->IsBound())
	{
		VANS_LOG("[AudioComp] SwitchSource 停止旧节点='" << m_AudioNode->GetName()
			<< "' IsPlaying=" << m_AudioNode->IsPlaying());
		if (m_AudioNode->IsPlaying() || m_AudioNode->IsPaused())
			m_AudioNode->Stop();
		VANS_LOG("[AudioComp] SwitchSource 旧节点已停止");
	}

	m_AudioNode = newNode;
	VANS_LOG("[AudioComp] SwitchSource → '" << name << "'");
	return true;
}

// ---------------------------------------------------------------------------
// VansScriptVideoComponent — SwitchSource
// ---------------------------------------------------------------------------
bool VansScriptVideoComponent::SwitchSource(const std::string& name)
{
	if (!m_VideoManager)
	{
		VANS_LOG_WARN("[VideoComp] SwitchSource 失败：m_VideoManager 未绑定");
		return false;
	}

	VansGraphics::VansVideoTexture* newTex = m_VideoManager->Get(name);
	if (!newTex)
	{
		VANS_LOG_WARN("[VideoComp] SwitchSource 失败：资源 '" << name << "' 未在 VideoManager 中找到");
		return false;
	}

	// Pause 旧视频（而非 Stop），保留 FFmpeg 流状态，便于切回时快速恢复
	if (m_VideoTex && m_VideoTex->IsPlaying())
		m_VideoTex->Pause();

	// 替换当前视频指针
	m_VideoTex  = newTex;
	m_VideoName = name;

	// ── 同步 Bindless GPU 描述符槽位 ──────────────────────────────────────
	// m_BindlessFirstSlot 由 LoadSceneForRendering 在 PreparePBRMaterialData 之后写入。
	// 切换时将该槽位对应的所有 5 个 bindless 槽更新为新视频的 GPU 贴图句柄。
	// 全局描述符集（Set 0）的 bindless 绑定已启用 VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT，
	// 允许在 GPU 执行期间安全地调用 vkUpdateDescriptorSets 更新该槽位。
	if (m_BindlessFirstSlot >= 0 && m_MaterialManagerRef && newTex->GetTexture())
	{
		VansGraphics::VansTexture* newGpuTex = newTex->GetTexture();

		const int kSlotsPerMat = 5;
		int totalSlots = static_cast<int>(m_MaterialManagerRef->m_GlobalPBRTextures.size());

		// 更新 CPU 端指针数组，保持与 GPU 侧一致
		for (int s = 0; s < kSlotsPerMat && (m_BindlessFirstSlot + s) < totalSlots; ++s)
			m_MaterialManagerRef->m_GlobalPBRTextures[m_BindlessFirstSlot + s] = &newGpuTex->GetImage();

		// 直接调用 vkUpdateDescriptorSets 更新对应 bindless 槽
		VkDescriptorImageInfo imgInfo{};
		imgInfo.sampler     = newGpuTex->GetImage().GetSampler();
		imgInfo.imageView   = newGpuTex->GetImage().GetImageView();
		imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		std::vector<VkDescriptorImageInfo> imgInfos(kSlotsPerMat, imgInfo);

		if (m_MaterialManagerRef->m_VideoBindlessDescriptorSet != VK_NULL_HANDLE)
		{
			VansGraphics::VansVKDescriptorManager::GetInstance()->DirectUpdateImageDescriptors(
				m_MaterialManagerRef->m_VideoBindlessDescriptorSet,
				VansGraphics::GLOBAL_BINDING_BINDLESS_TEXTURES,
				static_cast<uint32_t>(m_BindlessFirstSlot),
				imgInfos,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

			VANS_LOG("[VideoComp] Bindless 槽 " << m_BindlessFirstSlot << "~"
				<< m_BindlessFirstSlot + kSlotsPerMat - 1 << " 已更新 → '" << name << "'");
		}
	}

	VANS_LOG("[VideoComp] SwitchSource → '" << name << "'");
	return true;
}

// ---------------------------------------------------------------------------
// VansScriptParticleComponent — 粒子组件接口实现
// ---------------------------------------------------------------------------

void VansScriptParticleComponent::Play()
{
	if (!m_Runtime) return;
	m_IsPlaying          = true;
	m_Runtime->m_IsPlaying = true;
}

void VansScriptParticleComponent::Stop()
{
	if (!m_Runtime) return;
	m_IsPlaying            = false;
	m_PlayTime             = 0.f;
	m_Runtime->m_IsPlaying = false;
	m_Runtime->m_PlayTime  = 0.f;
    m_Runtime->m_AliveInstanceCount.store(0, std::memory_order_release);
    for (auto& buffer : m_Runtime->m_InstanceBuffers)
    {
        buffer.clear();
    }

    // 清空粒子池并重置发射器运行时状态，确保一次性 Burst 可被 Restart 重新触发
	if (m_ParticleAsset)
	{
		for (auto& emitter : m_ParticleAsset->m_Emitters)
		{
            if (!emitter) continue;

            emitter->m_ParticlePool.m_AliveCount = 0;
            emitter->m_SpawnAccum = 0.f;
            for (auto& burst : emitter->m_SpawnConfig.m_Bursts)
            {
                burst.cyclesDone = 0;
                burst.nextTime   = -1.f;
            }
		}
	}
}

void VansScriptParticleComponent::Pause()
{
	if (!m_Runtime) return;
	m_IsPlaying            = false;
	m_Runtime->m_IsPlaying = false;
}

void VansScriptParticleComponent::Restart()
{
	Stop();
	Play();
}

void VansScriptParticleComponent::SetWorldPosition(float x, float y, float z)
{
    m_HasWorldPositionOverride = true;
    m_WorldPositionOverride    = glm::vec3(x, y, z);
}

void VansScriptParticleComponent::ClearWorldPositionOverride()
{
    m_HasWorldPositionOverride = false;
}

bool VansScriptParticleComponent::LoadAsset(const std::string& path)
{
	auto newAsset = std::make_unique<VansGraphics::VansParticleAsset>();
	if (!newAsset->LoadFromFile(path))
		return false;

	m_ParticleAssetPath = path;
	m_ParticleAsset     = std::move(newAsset);

	// 重新创建 Runtime
	m_Runtime           = std::make_unique<VansGraphics::VansParticleRuntime>();
	m_Runtime->m_Asset  = m_ParticleAsset.get();
	return true;
}

void VansScriptParticleComponent::OnUpdate(float deltaTime)
{
	if (!m_IsPlaying || !m_Runtime) return;
	m_PlayTime += deltaTime;
	m_Runtime->m_LocalToWorld = glm::mat4(1.f); // 由调用方写入实际变换
}


static void InstallPythonOutputRedirect()
{
    // Register the tiny C++ helper module that Python redirect classes call into
    {
        auto m = py::module::create_extension_module("_vans_console", "", new py::module::module_def);
        m.def("_log_python", [](const std::string& msg) {
            VANS_LOG_PYTHON(msg);
        });
        // Make it importable
        py::module::import("sys").attr("modules")["_vans_console"] = m;
    }

    py::module::import("_engine_redirect");
}

bool VansScriptContext::ResolveScriptClass(
    const std::string& scriptPath,
    const std::string& className,
    const std::filesystem::path& absPath,
    std::string& moduleName,
    py::object& scriptClass)
{
    if (!m_Interpreter)
        return false;
    AssertPythonThread();
    const std::string key = CanonicalScriptKey(absPath);
    auto [it, inserted] = m_TrackedPyModules.try_emplace(key);
    PyModuleInfo& info = it->second;

    if (inserted)
    {
        info.scriptPath = scriptPath;
        info.filePath = absPath;
        info.moduleName = MakeScriptModuleName(key);
        if (std::filesystem::exists(absPath))
            info.lastWriteTime = std::filesystem::last_write_time(absPath);
    }

    moduleName = info.moduleName;
    if (info.loadFailed)
        return false;

    try
    {
        if (!info.module)
            info.module = LoadScriptModuleFromFile(info.filePath, info.moduleName, true);

        auto classIt = info.classes.find(className);
        if (classIt == info.classes.end())
        {
            py::object resolvedClass = info.module.attr(className.c_str());
            classIt = info.classes.emplace(className, std::move(resolvedClass)).first;
        }
        scriptClass = classIt->second;
        return true;
    }
    catch (const py::error_already_set& e)
    {
        info.loadFailed = !info.module;
        VANS_LOG_PYTHON("[PyScript] Failed to load " + scriptPath +
            "::" + className + ": " + e.what());
    }
    catch (const std::exception& e)
    {
        info.loadFailed = !info.module;
        VANS_LOG_PYTHON("[PyScript] Failed to load " + scriptPath +
            "::" + className + ": " + e.what());
    }
    return false;
}

// ---------------------------------------------------------------------------
// Check every tracked .py file; if it has been modified on disk, reload it.
// ---------------------------------------------------------------------------
void VansScriptContext::CheckAndReloadPyScripts()
{
    AssertPythonThread();
    for (auto& [canonicalPath, info] : m_TrackedPyModules)
    {
        try
        {
            if (!std::filesystem::exists(info.filePath))
                continue;

            auto currentTime = std::filesystem::last_write_time(info.filePath);
            if (currentTime != info.lastWriteTime)
            {
                // Record the observed fingerprint before attempting reload so a
                // broken file is not retried and logged every polling interval.
                info.lastWriteTime = currentTime;
                ReloadPyModule(info);
            }
        }
        catch (const py::error_already_set& e)
        {
            VANS_LOG_PYTHON("[Hot-Reload] Error checking " + info.scriptPath + ": " + e.what());
        }
        catch (const std::exception& e)
        {
            VANS_LOG_PYTHON("[Hot-Reload] Error checking " + info.scriptPath + ": " + e.what());
        }
    }
}

bool VansScriptContext::ReloadPyModule(PyModuleInfo& moduleInfo)
{
    AssertPythonThread();
    py::dict modules;
    py::object previousModule;
    bool candidatePublished = false;
    try
    {
        py::module candidate = LoadScriptModuleFromFile(
            moduleInfo.filePath, moduleInfo.moduleName, false);

        modules = py::module::import("sys").attr("modules");
        previousModule = moduleInfo.module;
        modules[py::str(moduleInfo.moduleName)] = candidate;
        candidatePublished = true;

        struct CandidateInstance
        {
            VanPyScriptComponent* component = nullptr;
            py::object instance;
        };
        std::vector<CandidateInstance> candidates;

        for (const ScheduledScript& scheduled : m_ScheduledScripts)
        {
            auto* component = scheduled.component;
            if (!component || component->m_ScriptModuleName != moduleInfo.moduleName)
            {
                continue;
            }

            py::object scriptClass = candidate.attr(component->m_ScriptClassName.c_str());
            py::object instance;
            std::string error;
            if (!component->BuildPythonInstance(scriptClass, instance, error))
            {
                VANS_LOG_PYTHON("[Hot-Reload] Kept previous module for " +
                    moduleInfo.scriptPath + ": " + error);
                if (previousModule)
                    modules[py::str(moduleInfo.moduleName)] = previousModule;
                else
                    modules.attr("pop")(moduleInfo.moduleName, py::none());
                return false;
            }
            candidates.push_back({ component, std::move(instance) });
        }

        moduleInfo.module = candidate;
        moduleInfo.classes.clear();
        moduleInfo.loadFailed = false;
        candidatePublished = false;

        for (auto& candidateInstance : candidates)
            candidateInstance.component->CommitPythonInstance(
                std::move(candidateInstance.instance));

        VANS_LOG_PYTHON("[Hot-Reload] Reloaded " + moduleInfo.scriptPath);
        return true;
    }
    catch (const py::error_already_set& e)
    {
        if (candidatePublished)
        {
            if (previousModule)
                modules[py::str(moduleInfo.moduleName)] = previousModule;
            else
                modules.attr("pop")(moduleInfo.moduleName, py::none());
        }
        VANS_LOG_PYTHON("[Hot-Reload] Kept previous module for " +
            moduleInfo.scriptPath + ": " + e.what());
    }
    catch (const std::exception& e)
    {
        if (candidatePublished)
        {
            if (previousModule)
                modules[py::str(moduleInfo.moduleName)] = previousModule;
            else
                modules.attr("pop")(moduleInfo.moduleName, py::none());
        }
        VANS_LOG_PYTHON("[Hot-Reload] Kept previous module for " +
            moduleInfo.scriptPath + ": " + e.what());
    }
    return false;
}

// ---------------------------------------------------------------------------
// 场景切换时清空已跟踪的 Python 模块引用，防止跨场景累积
// ---------------------------------------------------------------------------
void VansScriptContext::ClearTrackedModules()
{
    if (m_Interpreter)
    {
        AssertPythonThread();
        py::dict modules = py::module::import("sys").attr("modules");
        for (const auto& [canonicalPath, info] : m_TrackedPyModules)
            modules.attr("pop")(info.moduleName, py::none());
    }
    m_TrackedPyModules.clear();
    m_ScheduledScripts.clear();
    m_EventSubscribers.clear();
    m_FileCheckAccumulator = 0.0f;
    VANS_LOG("[VansScriptContext] Tracked Python modules cleared for scene switch");
}

void VansScriptContext::ShutdownPython()
{
    if (!m_Interpreter)
        return;

    AssertPythonThread();
    ClearTrackedModules();
    m_ProjectPythonPaths.clear();
    m_ActiveProjectRoot.clear();
    s_Instance = nullptr;
    m_Interpreter.reset();
    m_PythonThreadId = {};
}

// ---------------------------------------------------------------------------
// Explicit reload of all tracked .py modules (called from editor UI)
// ---------------------------------------------------------------------------
void VansScriptContext::ReloadAllPyScripts()
{
    if (!m_Interpreter)
        return;
    AssertPythonThread();
    for (auto& [canonicalPath, info] : m_TrackedPyModules)
    {
        try
        {
            if (std::filesystem::exists(info.filePath))
                info.lastWriteTime = std::filesystem::last_write_time(info.filePath);
            ReloadPyModule(info);
        }
        catch (const std::exception& e)
        {
            VANS_LOG_PYTHON("[Reload] Error reloading " + info.scriptPath + ": " + e.what());
        }
    }
}

// ---------------------------------------------------------------------------
// Native extension modules remain loaded for the interpreter lifetime. The
// editor action reports the required restart instead of accumulating DLL/type
// copies in one process.
// ---------------------------------------------------------------------------
void VansScriptContext::ReloadPydModule(const std::string& moduleName)
{
    VANS_LOG_PYTHON("[Native module] " + moduleName +
        " changed. Restart the editor to load the rebuilt extension safely.");
}

void VansScriptContext::ConfigureProjectPythonPaths(const std::string& projectRoot)
{
    if (!m_Interpreter)
        return;
    AssertPythonThread();
    const std::string canonicalRoot = CanonicalScriptKey(projectRoot);
    if (canonicalRoot == m_ActiveProjectRoot)
        return;

    py::list path = py::module::import("sys").attr("path");
    for (const std::string& oldPath : m_ProjectPythonPaths)
    {
        while (path.attr("__contains__")(oldPath).cast<bool>())
            path.attr("remove")(oldPath);
    }
    m_ProjectPythonPaths.clear();
    m_ActiveProjectRoot = canonicalRoot;

    auto appendPath = [&](const std::filesystem::path& candidate) {
        if (!std::filesystem::exists(candidate))
            return;
        const std::string value = candidate.lexically_normal().string();
        if (!path.attr("__contains__")(value).cast<bool>())
        {
            path.attr("append")(value);
            m_ProjectPythonPaths.push_back(value);
        }
    };

    const std::filesystem::path root(projectRoot);
    // Project paths are appended so they cannot shadow the standard library or
    // engine bindings. Exact script files are still loaded by absolute path.
    appendPath(root);
    appendPath(root / "Scripts");
    appendPath(root / ".vans" / "python" / "site-packages");
}

// ---------------------------------------------------------------------------
// Explicit editor operation: install project dependencies into a project-local
// directory. Runtime startup never invokes pip.
// ---------------------------------------------------------------------------
void VansScriptContext::SetupProjectVenv(const std::string& projectRoot)
{
	if (!m_Interpreter)
	{
		VANS_LOG_ERROR("[PyDeps] Python runtime is not initialized");
		return;
	}
	try
	{
		AssertPythonThread();
		// ── 引擎内嵌 Python 可执行文件路径 ──────────────────────────────
		auto* cfg = VansConfigration::GetInstance();
		std::string engineRoot = cfg->GetProjectRootPath();
		std::string pythonExe  = engineRoot + "External/Python-3.13.3/PCbuild/amd64/python.exe";
		std::replace(pythonExe.begin(), pythonExe.end(), '/', '\\');

		// ── 规范化项目根路径 ──────────────────────────────────────────
		std::string projRoot = projectRoot;
		if (!projRoot.empty() && projRoot.back() != '/' && projRoot.back() != '\\')
			projRoot += '/';

		std::string requirementsPath = projRoot + "Scripts/requirements.txt";

		VANS_LOG_PYTHON(
			"[PyDeps] Checking requirements: " + requirementsPath);

		// ── 1. 若 requirements.txt 不存在，生成默认模板 ───────────────
		if (!std::filesystem::exists(requirementsPath))
		{
			std::ofstream reqFile(requirementsPath);
			if (reqFile.is_open())
			{
				reqFile << "# 在此处列出项目依赖的 Python 第三方库，每行一个\n";
				reqFile << "# 示例:\n";
				reqFile << "# matplotlib\n";
				reqFile << "# numpy\n";
				reqFile.close();
				VANS_LOG_PYTHON(
					"[PyDeps] Created default requirements.txt: " + requirementsPath);
			}
			else
			{
				VANS_LOG_WARN("[PyDeps] Cannot create requirements.txt at: " << requirementsPath);
			}
			return; // 模板刚创建，无内容可安装
		}

		// ── 2. 读取 requirements.txt，跳过注释和空行 ───────────────
		{
			bool hasRequirements = false;
			std::ifstream reqIn(requirementsPath);
			std::string   line;
			while (std::getline(reqIn, line))
			{
				auto first = line.find_first_not_of(" \t\r\n");
				if (first != std::string::npos && line[first] != '#')
				{
					hasRequirements = true;
					break;
				}
			}

			if (!hasRequirements)
			{
				VANS_LOG_PYTHON("[PyDeps] requirements.txt has no packages, skipping install.");
				return;
			}
		}

		// ── 3. 安装到项目私有目录，避免污染引擎 Python 与其他项目 ─────
		std::filesystem::path dependencyPath =
			std::filesystem::path(projRoot) / ".vans" / "python" / "site-packages";
		std::filesystem::create_directories(dependencyPath);
		VANS_LOG_PYTHON(
			"[PyDeps] Installing project dependencies from: " + requirementsPath);

		py::module subprocess = py::module::import("subprocess");
		py::list   pipCmd;
		pipCmd.append(pythonExe);
		pipCmd.append("-m");
		pipCmd.append("pip");
		pipCmd.append("install");
		pipCmd.append("-r");
		pipCmd.append(requirementsPath);
		pipCmd.append("--target");
		pipCmd.append(dependencyPath.string());
		pipCmd.append("--quiet");
		pipCmd.append("--no-warn-script-location");

		py::object result = subprocess.attr("run")(
			pipCmd,
			py::arg("capture_output") = true);

		int rc = result.attr("returncode").cast<int>();
		if (rc != 0)
		{
			std::string err = result.attr("stderr")
				.attr("decode")("utf-8", "replace").cast<std::string>();
			VANS_LOG_PYTHON("[PyDeps] pip install failed: " + err);
		}
		else
		{
			VANS_LOG_PYTHON("[PyDeps] Project dependencies installed successfully.");
			m_ActiveProjectRoot.clear();
			ConfigureProjectPythonPaths(projRoot);
			// 刷新当前解释器的导入缓存，使子进程刚安装的包立即可见
			py::module::import("importlib").attr("invalidate_caches")();
		}
	}
	catch (const py::error_already_set& e)
	{
		VANS_LOG_PYTHON("[PyDeps] Python error: " + std::string(e.what()));
	}
	catch (const std::exception& e)
	{
		VANS_LOG_ERROR("[PyDeps] " << e.what());
	}
}

// ---------------------------------------------------------------------------
void VansScriptContext::VansScriptSetup()
{
    VANS_ASSERT_MAIN_THREAD();
    if (m_Interpreter)
        return;

    auto vansConfigration = VansConfigration::GetInstance();
    std::string projectRoot = vansConfigration->GetProjectRootPath();
    
    // Set PYTHONHOME to the External Python directory in the project
    std::string pythonHome = projectRoot + "External/Python-3.13.3";
    std::string pythonHomeEnv = "PYTHONHOME=" + pythonHome;
    _putenv(pythonHomeEnv.c_str());

    // CPython 源码树中 Tcl/Tk 脚本库位于 externals/tcltk-*/amd64/lib/，
    // 必须在任何 tkinter/_tkinter 导入前设置 TCL_LIBRARY / TK_LIBRARY，
    // 否则 TkAgg 后端（matplotlib）初始化时报 "Can't find a usable init.tcl"。
    std::string tcltkBase = pythonHome + "/externals/tcltk-8.6.15.0/amd64/lib";
    _putenv(("TCL_LIBRARY=" + tcltkBase + "/tcl8.6").c_str());
    _putenv(("TK_LIBRARY="  + tcltkBase + "/tk8.6").c_str());

    m_Interpreter = std::make_unique<py::scoped_interpreter>();
    m_PythonThreadId = std::this_thread::get_id();
    s_Instance = this;

    // Add your script directory to sys.path
    // EngineExported holds the .pyd modules and user scripts
    py::module sys = py::module::import("sys");
    m_ScriptDir = projectRoot + "../ForestExporter/EngineExported";
    sys.attr("path").attr("insert")(0, m_ScriptDir);

    // CPython 源码树结构下，内置 C 扩展（_socket.pyd、_ssl.pyd 等）
    // 编译产物在 PCbuild/amd64/，而非已安装 Python 的 DLLs/ 目录。
    // PYTHONHOME 指向源码根时 sys.path 只包含 DLLs/（可能为空），
    // 必须显式将 PCbuild/amd64 插入，否则 socket/ssl 等模块无法加载。
    std::string pcbuildPath = pythonHome + "/PCbuild/amd64";
    sys.attr("path").attr("insert")(1, pcbuildPath);

    // pybind11 的 scoped_interpreter 以 Py_NoSiteFlag 启动，不会自动运行 site.py，
    // 因此 Lib/site-packages 不在 sys.path 中，需手动注入。
    {
        std::string sitePackagesPath = pythonHome + "/Lib/site-packages";

        // 1) site.addsitedir：将路径加入 sys.path 并处理 .pth 文件
        py::module siteModule = py::module::import("site");
        siteModule.attr("addsitedir")(sitePackagesPath);

        // 2) 双保险：直接插入 sys.path（addsitedir 在路径已存在时可能跳过）
        sys.attr("path").attr("insert")(1, sitePackagesPath);

        // 3) Windows 嵌入式 Python：pip wheel（delvewheel）把编译包依赖的 DLL
        //    存放在 site-packages/<pkg>.libs/ 顶层目录（例如 numpy.libs/）。
        //    嵌入式模式不自动将这些目录注册到 Windows DLL 搜索路径，
        //    必须手动调用 os.add_dll_directory()，否则 .pyd 扩展加载时报 ImportError。
        try
        {
            py::module osMod = py::module::import("os");
            std::filesystem::path siteFsPath(sitePackagesPath);
            if (std::filesystem::exists(siteFsPath))
            {
                for (auto& entry : std::filesystem::directory_iterator(siteFsPath))
                {
                    if (!entry.is_directory()) continue;
                    // delvewheel 规范：DLL 目录名以 ".libs" 结尾，位于 site-packages 顶层
                    // 例如 numpy.libs/、matplotlib.libs/ 等
                    const std::string dirName = entry.path().filename().string();
                    if (dirName.size() > 5 &&
                        dirName.compare(dirName.size() - 5, 5, ".libs") == 0)
                    {
                        osMod.attr("add_dll_directory")(entry.path().string());
                    }
                }
            }
        }
        catch (const py::error_already_set& ex)
        {
            VANS_LOG_WARN("[ScriptSetup] DLL dir scan Python error: " << ex.what());
        }
        catch (const std::exception& ex)
        {
            VANS_LOG_WARN("[ScriptSetup] DLL dir scan failed: " << ex.what());
        }
    }

    // If a user project is loaded, also add the project root to sys.path
    // so that user scripts (e.g. Scripts/test.py) can be discovered.
    auto& projectMgr = Vans::VansProjectManager::Get();
    if (projectMgr.IsProjectLoaded())
    {
        std::string projectScriptDir = projectMgr.GetProjectRootPath();
        ConfigureProjectPythonPaths(projectScriptDir);
    }

    // Install stdout/stderr redirect so print() goes to console window
    // (must be after sys.path setup so _engine_redirect.py is findable)
    InstallPythonOutputRedirect();

    // ── 初始化并安装引擎桥接到 vanscomponent ─────────────────────────────
    VansInitEngineBridge();
    try
    {
        py::module vc = py::module::import("vanscomponent");
        vc.attr("_install_bridge")(py::capsule(
            VansGetEngineBridgePtr(), VANS_ENGINE_BRIDGE_CAPSULE_NAME));
        VANS_LOG_PYTHON("[Bridge] Engine bridge installed into vanscomponent");
    }
    catch (const py::error_already_set& e)
    {
        VANS_LOG_PYTHON(std::string("[Bridge] Warning: ") + e.what());
        VANS_LOG_ERROR("Failed to install engine bridge: " << e.what());
    }

    // ── 初始化并安装输入桥接到 vaninput ───────────────────────────────────
    VansInitInputBridge();
    try
    {
        py::module vi = py::module::import("vaninput");
        vi.attr("_install_bridge")(py::capsule(
            VansGetInputBridgePtr(), VANS_INPUT_BRIDGE_CAPSULE_NAME));
        VANS_LOG_PYTHON("[Bridge] Input bridge installed into vaninput");
    }
    catch (const py::error_already_set& e)
    {
        VANS_LOG_PYTHON(std::string("[Bridge] Warning (vaninput): ") + e.what());
        VANS_LOG_ERROR("Failed to install input bridge: " << e.what());
    }

    RebuildScriptSchedule();
}

void VansScriptContext::VansScriptUpdate()
{
    VANS_PROFILE_SCOPE("Script::VansScriptUpdate", Vans::ProfileCategory::Script);

    VansScriptPreUpdate();
    UpdateScriptComponents(false, false);

    return;
}

void VansScriptContext::VansScriptUpdateNonCameraScripts()
{
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::GameLogic);
    VANS_PROFILE_SCOPE("Script::VansScriptUpdateNonCameraScripts", Vans::ProfileCategory::Script);

    VansScriptPreUpdate();
    UpdateScriptComponents(false, true);

    return;
}

void VansScriptContext::VansScriptUpdateCameraScripts()
{
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::GameLogic);
    VANS_PROFILE_SCOPE("Script::VansScriptUpdateCameraScripts", Vans::ProfileCategory::Script);

    UpdateScriptComponents(true, false);

    return;
}

void VansScriptContext::VansScriptPreUpdate()
{
    if (!m_Scene) return;

    // Periodically check for .py file changes
    m_FileCheckAccumulator += static_cast<float>(VansGraphics::VansTimer::GetLastFrameDelta());
    if (m_FileCheckAccumulator >= FILE_CHECK_INTERVAL)
    {
        VANS_PROFILE_SCOPE("Script::CheckHotReload", Vans::ProfileCategory::Script);
        m_FileCheckAccumulator = 0.0f;
        CheckAndReloadPyScripts();
    }

    // ── 调度物理事件（在 CallUpdate 之前） ───────────────────────────
    {
        VANS_PROFILE_SCOPE("Script::DispatchPhysicsEvents", Vans::ProfileCategory::Script);
        DispatchPhysicsEvents();
    }
}

void VansScriptContext::SetScene(VansGraphics::VansScene* scene)
{
    if (m_Scene == scene)
        return;
    m_Scene = scene;
    RebuildScriptSchedule();
}

void VansScriptContext::RegisterScriptComponent(
    VansScriptObject* owner, VanPyScriptComponent* component)
{
    if (!owner || !component)
        return;

    auto existing = std::find_if(
        m_ScheduledScripts.begin(), m_ScheduledScripts.end(),
        [component](const ScheduledScript& scheduled) {
            return scheduled.component == component;
        });
    if (existing != m_ScheduledScripts.end())
        return;

    const bool cameraScript =
        owner->GetComponent<VansScriptCameraComponent>() != nullptr;
    m_ScheduledScripts.push_back({ owner, component, cameraScript });
    m_EventSubscribers[owner->m_TransformID].push_back(component);
}

void VansScriptContext::UnregisterScriptComponent(VanPyScriptComponent* component)
{
    if (!component)
        return;

    m_ScheduledScripts.erase(
        std::remove_if(
            m_ScheduledScripts.begin(), m_ScheduledScripts.end(),
            [component](const ScheduledScript& scheduled) {
                return scheduled.component == component;
            }),
        m_ScheduledScripts.end());

    for (auto it = m_EventSubscribers.begin(); it != m_EventSubscribers.end();)
    {
        auto& subscribers = it->second;
        subscribers.erase(
            std::remove(subscribers.begin(), subscribers.end(), component),
            subscribers.end());
        if (subscribers.empty())
            it = m_EventSubscribers.erase(it);
        else
            ++it;
    }
}

void VansScriptContext::RebuildScriptSchedule()
{
    m_ScheduledScripts.clear();
    m_EventSubscribers.clear();
    if (!m_Scene)
        return;

    for (auto* owner : m_Scene->GetSceneObjects())
    {
        if (!owner)
            continue;
        for (auto* baseComponent : owner->m_Components)
        {
            if (auto* component = dynamic_cast<VanPyScriptComponent*>(baseComponent))
                RegisterScriptComponent(owner, component);
        }
    }
}

void VansScriptContext::UpdateScriptComponents(bool cameraScriptsOnly, bool skipCameraScripts)
{
    if (!m_Scene) return;

    for (const ScheduledScript& scheduled : m_ScheduledScripts)
    {
        if (cameraScriptsOnly && !scheduled.cameraScript)
            continue;
        if (skipCameraScripts && scheduled.cameraScript)
            continue;

        auto* component = scheduled.component;
        if (!component)
            continue;

        if (component->m_State == VansPythonScriptState::Unloaded &&
            !component->m_ScriptPath.empty() &&
            !component->m_ScriptClassName.empty())
        {
            component->m_OwnerObject = scheduled.owner;
            component->Instantiate();
            // Enable also records the requested state when instantiation failed,
            // allowing a later file-change reload to recover correctly.
            component->Enable();
        }

        component->CallUpdate();
    }
}

// ===========================================================================
//  VanPyScriptComponent — lifecycle implementations
// ===========================================================================

bool VanPyScriptComponent::Instantiate()
{
    if (m_State == VansPythonScriptState::Destroyed)
        return false;

    m_State = VansPythonScriptState::Loading;
    m_IsValid = false;
    try
    {
        // Resolve absolute path — prefer user project root, fall back to engine root
        auto vansConfigration = VansConfigration::GetInstance();
        auto& projectMgr = Vans::VansProjectManager::Get();
        std::string projectRoot = projectMgr.IsProjectLoaded()
            ? projectMgr.GetProjectRootPath()
            : vansConfigration->GetProjectRootPath();

        std::filesystem::path absPath = std::filesystem::path(projectRoot) / m_ScriptPath;

        auto* context = VansScriptContext::GetInstance();
        if (!context)
        {
            EnterFaultedState("load", "Python runtime is not initialized");
            return false;
        }

        context->ConfigureProjectPythonPaths(projectRoot);
        py::object scriptClass;
        if (!context->ResolveScriptClass(
                m_ScriptPath, m_ScriptClassName, absPath,
                m_ScriptModuleName, scriptClass))
        {
            EnterFaultedState("load", "module or class resolution failed");
            return false;
        }

        py::object instance;
        std::string error;
        if (!BuildPythonInstance(scriptClass, instance, error))
        {
            EnterFaultedState("construct", error);
            return false;
        }

        CommitPythonInstance(std::move(instance));
        context->RegisterScriptComponent(m_OwnerObject, this);
        VANS_LOG_PYTHON(
            "[PyScript] Loaded " + m_ScriptClassName + " from " + m_ScriptPath);
        return true;
    }
    catch (const py::error_already_set& e)
    {
        EnterFaultedState("construct", e.what());
    }
    catch (const std::exception& e)
    {
        EnterFaultedState("construct", e.what());
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Component OnEnable/OnDisable — 委托给底层 VansNode
//  （必须放在 .cpp 中，因为 VansScriptContext.h 中 Node 类型只有前向声明）
// ═══════════════════════════════════════════════════════════════════════════

void VansScriptRenderComponent::OnEnable()
{
    if (m_RenderNode) m_RenderNode->SetEnabled(true);
}
void VansScriptRenderComponent::OnDisable()
{
    if (m_RenderNode) m_RenderNode->SetEnabled(false);
}

void VansScriptPhysicsComponent::OnEnable()
{
    if (m_PhysicsNode) m_PhysicsNode->SetEnabled(true);
}
void VansScriptPhysicsComponent::OnDisable()
{
    if (m_PhysicsNode) m_PhysicsNode->SetEnabled(false);
}

void VansScriptClothComponent::OnEnable()
{
    if (m_ClothNode) m_ClothNode->SetEnabled(true);
}
void VansScriptClothComponent::OnDisable()
{
    if (m_ClothNode) m_ClothNode->SetEnabled(false);
}

void VansScriptAnimationComponent::OnEnable()
{
    if (m_AnimNode) m_AnimNode->SetEnabled(true);
}
void VansScriptAnimationComponent::OnDisable()
{
    if (m_AnimNode) m_AnimNode->SetEnabled(false);
}

void VansScriptRagdollComponent::OnEnable()
{
    if (m_AnimNode) m_AnimNode->SetEnabled(true);
}
void VansScriptRagdollComponent::OnDisable()
{
    if (m_AnimNode) m_AnimNode->SetEnabled(false);
}

void VansScriptCharacterControllerComponent::OnEnable()
{
    if (m_ControllerNode) m_ControllerNode->SetEnabled(true);
}
void VansScriptCharacterControllerComponent::OnDisable()
{
    if (m_ControllerNode) m_ControllerNode->SetEnabled(false);
}

void VansScriptCameraComponent::OnEnable()
{
    if (m_Camera) m_Camera->SetEnabled(true);
}
void VansScriptCameraComponent::OnDisable()
{
    if (m_Camera) m_Camera->SetEnabled(false);
}

void VansScriptAudioComponent::OnEnable()
{
    if (m_AudioNode) m_AudioNode->SetEnabled(true);
}
void VansScriptAudioComponent::OnDisable()
{
    if (m_AudioNode) m_AudioNode->SetEnabled(false);
}

VanPyScriptComponent::~VanPyScriptComponent()
{
    Teardown();
    m_State = VansPythonScriptState::Destroyed;
}

bool VanPyScriptComponent::BuildPythonInstance(
    const py::object& scriptClass, py::object& instance, std::string& error) const
{
    try
    {
        instance = scriptClass();
        py::module componentModule = py::module::import("vanscomponent");
        if (!py::isinstance(instance, componentModule.attr("vanspyscript")))
        {
            error = m_ScriptPath + "::" + m_ScriptClassName +
                " must inherit from vanspyscript";
            instance = py::object();
            return false;
        }

        if (m_OwnerObject)
        {
            instance.attr("_bind_native_object")(
                reinterpret_cast<uintptr_t>(m_OwnerObject));
        }
        return true;
    }
    catch (const py::error_already_set& e)
    {
        error = e.what();
    }
    catch (const std::exception& e)
    {
        error = e.what();
    }
    instance = py::object();
    return false;
}

void VanPyScriptComponent::CacheCallbacks()
{
    m_OnEnableCallback = py::getattr(m_PyInstance, "on_enable", py::none());
    m_OnDisableCallback = py::getattr(m_PyInstance, "on_disable", py::none());
    m_UpdateCallback = py::getattr(m_PyInstance, "update", py::none());
    m_CollisionEnterCallback = py::getattr(m_PyInstance, "on_collision_enter", py::none());
    m_CollisionExitCallback = py::getattr(m_PyInstance, "on_collision_exit", py::none());
    m_TriggerEnterCallback = py::getattr(m_PyInstance, "on_trigger_enter", py::none());
    m_TriggerExitCallback = py::getattr(m_PyInstance, "on_trigger_exit", py::none());
}

void VanPyScriptComponent::ResetPythonObjects()
{
    m_TriggerExitCallback = py::object();
    m_TriggerEnterCallback = py::object();
    m_CollisionExitCallback = py::object();
    m_CollisionEnterCallback = py::object();
    m_UpdateCallback = py::object();
    m_OnDisableCallback = py::object();
    m_OnEnableCallback = py::object();
    m_PyInstance = py::object();
}

void VanPyScriptComponent::CommitPythonInstance(py::object instance)
{
    const bool shouldEnable = m_EnableRequested;
    if (m_Enabled && m_IsValid)
        SetEnabled(false);
    ResetPythonObjects();

    m_PyInstance = std::move(instance);
    CacheCallbacks();
    m_IsValid = true;
    m_Enabled = false;
    m_State = VansPythonScriptState::Disabled;
    if (shouldEnable)
        SetEnabled(true);
}

void VanPyScriptComponent::EnterFaultedState(
    const char* phase, const std::string& error)
{
    m_IsValid = false;
    m_Enabled = false;
    m_State = VansPythonScriptState::Faulted;
    VANS_LOG_PYTHON("[PyScript] Faulted " + m_ScriptPath + "::" +
        m_ScriptClassName + " during " + phase + ": " + error);
}

void VanPyScriptComponent::Enable()
{
    m_EnableRequested = true;
    if (m_IsValid && m_State != VansPythonScriptState::Faulted && !m_Enabled)
        SetEnabled(true);
}

void VanPyScriptComponent::Disable()
{
    m_EnableRequested = false;
    if (m_Enabled)
        SetEnabled(false);
    else if (m_State != VansPythonScriptState::Faulted &&
             m_State != VansPythonScriptState::Destroyed)
        m_State = VansPythonScriptState::Disabled;
}

void VanPyScriptComponent::OnEnable()
{
    if (!m_IsValid) return;
    m_EnableRequested = true;
    try
    {
        if (m_OnEnableCallback && !m_OnEnableCallback.is_none())
            m_OnEnableCallback();
        m_State = VansPythonScriptState::Active;
    }
    catch (const py::error_already_set& e)
    {
        EnterFaultedState("on_enable", e.what());
    }
}

void VanPyScriptComponent::OnDisable()
{
    if (!m_IsValid) return;
    m_EnableRequested = false;
    try
    {
        if (m_OnDisableCallback && !m_OnDisableCallback.is_none())
            m_OnDisableCallback();
        m_State = VansPythonScriptState::Disabled;
    }
    catch (const py::error_already_set& e)
    {
        EnterFaultedState("on_disable", e.what());
    }
}

void VanPyScriptComponent::CallUpdate()
{
    VANS_PROFILE_SCOPE("Script::CallUpdate", Vans::ProfileCategory::Script);

    if (!m_IsValid || m_State != VansPythonScriptState::Active) return;
    try
    {
        if (m_UpdateCallback && !m_UpdateCallback.is_none())
            m_UpdateCallback();
    }
    catch (const py::error_already_set& e)
    {
        EnterFaultedState("update", e.what());
    }
}

void VanPyScriptComponent::Teardown()
{
    if (m_State == VansPythonScriptState::Destroyed)
        return;
    if (auto* context = VansScriptContext::GetInstance())
        context->UnregisterScriptComponent(this);
    if (m_Enabled && m_IsValid)
        SetEnabled(false);
    ResetPythonObjects();
    m_IsValid = false;
    m_EnableRequested = false;
    m_Enabled = false;
    m_State = VansPythonScriptState::Unloaded;
}

// ===========================================================================
//  Physics event callback methods on VanPyScriptComponent
// ===========================================================================

void VanPyScriptComponent::CallOnCollisionEnter(const PhysicsEventInfo& info)
{
    if (!m_IsValid || m_State != VansPythonScriptState::Active) return;
    try
    {
        if (m_CollisionEnterCallback && !m_CollisionEnterCallback.is_none())
            m_CollisionEnterCallback(info);
    }
    catch (const py::error_already_set& e)
    {
        EnterFaultedState("on_collision_enter", e.what());
    }
}

void VanPyScriptComponent::CallOnCollisionExit(const PhysicsEventInfo& info)
{
    if (!m_IsValid || m_State != VansPythonScriptState::Active) return;
    try
    {
        if (m_CollisionExitCallback && !m_CollisionExitCallback.is_none())
            m_CollisionExitCallback(info);
    }
    catch (const py::error_already_set& e)
    {
        EnterFaultedState("on_collision_exit", e.what());
    }
}

void VanPyScriptComponent::CallOnTriggerEnter(const PhysicsEventInfo& info)
{
    if (!m_IsValid || m_State != VansPythonScriptState::Active) return;
    try
    {
        if (m_TriggerEnterCallback && !m_TriggerEnterCallback.is_none())
            m_TriggerEnterCallback(info);
    }
    catch (const py::error_already_set& e)
    {
        EnterFaultedState("on_trigger_enter", e.what());
    }
}

void VanPyScriptComponent::CallOnTriggerExit(const PhysicsEventInfo& info)
{
    if (!m_IsValid || m_State != VansPythonScriptState::Active) return;
    try
    {
        if (m_TriggerExitCallback && !m_TriggerExitCallback.is_none())
            m_TriggerExitCallback(info);
    }
    catch (const py::error_already_set& e)
    {
        EnterFaultedState("on_trigger_exit", e.what());
    }
}

// ===========================================================================
//  DispatchPhysicsEvents — 从事件队列取出 PhysX 事件并分发到 Python 脚本
// ===========================================================================

void VansScriptContext::DispatchPhysicsEvents()
{
    if (!m_Scene) return;

    auto& physics = VansEngine::VansPhysicsSystem::GetInstance();
    std::vector<VansEngine::PhysicsEventData> events;
    physics.GetEventQueue().SwapEvents(events);

    // if (!events.empty())
    // {
    //     VANS_LOG("[PhysX Dispatch] Dispatching " << events.size() << " physics event(s)");
    // }

    for (const auto& event : events)
    {
        // 对 A 方分发（other = B）
        DispatchEventToObject(event, event.transformID_A, event.transformID_B,
                              event.nameB, event.contactPoint,
                              event.contactNormal, event.impulse);

        // 对 B 方分发（other = A），仅碰撞事件双向分发
        if (event.type == VansEngine::PhysicsEventType::CollisionEnter ||
            event.type == VansEngine::PhysicsEventType::CollisionExit)
        {
            DispatchEventToObject(event, event.transformID_B, event.transformID_A,
                                  event.nameA, event.contactPoint,
                                  -event.contactNormal, event.impulse);
        }
    }
}

void VansScriptContext::DispatchEventToObject(
    const VansEngine::PhysicsEventData& event,
    uint32_t selfTransformID, uint32_t otherTransformID,
    const std::string& otherName,
    const glm::vec3& contactPoint, const glm::vec3& contactNormal, float impulse)
{
    auto subscriberIt = m_EventSubscribers.find(selfTransformID);
    if (subscriberIt == m_EventSubscribers.end())
        return;

    PhysicsEventInfo info;
    info.otherName        = otherName;
    info.otherTransformID = otherTransformID;
    info.contactPoint     = PyVec3(contactPoint.x, contactPoint.y, contactPoint.z);
    info.contactNormal    = PyVec3(contactNormal.x, contactNormal.y, contactNormal.z);
    info.impulse          = impulse;

    for (auto* component : subscriberIt->second)
    {
        if (!component)
            continue;
        switch (event.type)
        {
        case VansEngine::PhysicsEventType::CollisionEnter:
            component->CallOnCollisionEnter(info); break;
        case VansEngine::PhysicsEventType::CollisionExit:
            component->CallOnCollisionExit(info); break;
        case VansEngine::PhysicsEventType::TriggerEnter:
            component->CallOnTriggerEnter(info); break;
        case VansEngine::PhysicsEventType::TriggerExit:
            component->CallOnTriggerExit(info); break;
        }
    }
}

