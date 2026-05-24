#include "VansScriptContext.h"
#include "../Configration/VansConfigration.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../EditorCore/Windows/VansConsole.h"
#include "../Util/VansLog.h"
#include "../VansTimer.h"
#include "../RenderCore/VansScene.h"
#include "../PhysicsCore/VansPhysics.h"
#include "../AudioCore/VansAudioManager.h"
#include "../RenderCore/VansVideoManager.h"
#include "../Util/VansProfiler.h"
#include "../RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include <cstdlib>
#include <fstream>
#include <string>
#include <algorithm>
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
            VansConsole::Get().LogPython(msg);
        });
        // Make it importable
        py::module::import("sys").attr("modules")["_vans_console"] = m;
    }

    py::module::import("_engine_redirect");
}

// ---------------------------------------------------------------------------
// Track a Python module for hot-reload (store its file path + write time)
// Keyed by the relative script path so each file is tracked exactly once.
// ---------------------------------------------------------------------------
void VansScriptContext::TrackPyModule(const std::string& scriptPath,
                                      const std::string& moduleName,
                                      py::module mod,
                                      const std::filesystem::path& absPath)
{
    try
    {
        if (std::filesystem::exists(absPath))
        {
            PyModuleInfo info;
            info.module        = mod;
            info.moduleName    = moduleName;
            info.filePath      = absPath;
            info.lastWriteTime = std::filesystem::last_write_time(absPath);
            m_TrackedPyModules[scriptPath] = info;
        }
    }
    catch (const std::exception& e)
    {
        VANS_LOG_ERROR("TrackPyModule(" << scriptPath << "): " << e.what());
    }
}

// ---------------------------------------------------------------------------
// Check every tracked .py file; if it has been modified on disk, reload it.
// ---------------------------------------------------------------------------
void VansScriptContext::CheckAndReloadPyScripts()
{
    py::module importlib = py::module::import("importlib");

    for (auto& [scriptPath, info] : m_TrackedPyModules)
    {
        try
        {
            if (!std::filesystem::exists(info.filePath))
                continue;

            auto currentTime = std::filesystem::last_write_time(info.filePath);
            if (currentTime != info.lastWriteTime)
            {
                info.lastWriteTime = currentTime;

                // Reload the module in-place (importlib.reload uses __file__
                // which was set correctly by spec_from_file_location)
                info.module = importlib.attr("reload")(info.module).cast<py::module>();

                VansConsole::Get().LogPython("[Hot-Reload] Reloaded " + scriptPath);

                // Re-instantiate any VanPyScriptComponents using this script path
                OnPyModuleReloaded(scriptPath);
            }
        }
        catch (const py::error_already_set& e)
        {
            VansConsole::Get().LogPython("[Hot-Reload] Error reloading " + scriptPath + ": " + e.what());
        }
    }
}

// ---------------------------------------------------------------------------
// 场景切换时清空已跟踪的 Python 模块引用，防止跨场景累积
// ---------------------------------------------------------------------------
void VansScriptContext::ClearTrackedModules()
{
    m_TrackedPyModules.clear();
    m_FileCheckAccumulator = 0.0f;
    VANS_LOG("[VansScriptContext] Tracked Python modules cleared for scene switch");
}

// ---------------------------------------------------------------------------
// Explicit reload of all tracked .py modules (called from editor UI)
// ---------------------------------------------------------------------------
void VansScriptContext::ReloadAllPyScripts()
{
    py::module importlib = py::module::import("importlib");

    // Collect script paths of modules successfully reloaded so we can
    // re-instantiate their VanPyScriptComponents afterwards.
    std::vector<std::string> reloadedPaths;

    for (auto& [scriptPath, info] : m_TrackedPyModules)
    {
        try
        {
            info.module = importlib.attr("reload")(info.module).cast<py::module>();

            if (std::filesystem::exists(info.filePath))
                info.lastWriteTime = std::filesystem::last_write_time(info.filePath);

            VansConsole::Get().LogPython("[Reload] Reloaded " + scriptPath);
            reloadedPaths.push_back(scriptPath);
        }
        catch (const py::error_already_set& e)
        {
            VansConsole::Get().LogPython("[Reload] Error reloading " + scriptPath + ": " + e.what());
        }
    }

    // Teardown + re-instantiate every VanPyScriptComponent whose script was
    // reloaded so they pick up the new class definitions (new on_enable,
    // update, on_disable, etc.).
    for (const auto& path : reloadedPaths)
    {
        OnPyModuleReloaded(path);
    }
}

// ---------------------------------------------------------------------------
// Reload a .pyd C++ extension module at runtime.
// Strategy: copy the .pyd to a temp name (Windows locks loaded DLLs),
// remove the old entry from sys.modules, and re-import from the copy.
// After reloading the .pyd, also reload dependent .py scripts.
// ---------------------------------------------------------------------------
void VansScriptContext::ReloadPydModule(const std::string& moduleName)
{
    try
    {
        py::module importlib      = py::module::import("importlib");
        py::module importlib_util = py::module::import("importlib.util");
        py::module sys            = py::module::import("sys");
        py::module shutil_mod     = py::module::import("shutil");

        // Find the source .pyd in EngineExported/
        // (skip any previous _hot copies)
        std::filesystem::path pydPath;
        for (auto& entry : std::filesystem::directory_iterator(m_ScriptDir))
        {
            std::string fname = entry.path().filename().string();
            if (fname.find(moduleName) == 0 &&
                entry.path().extension() == ".pyd" &&
                fname.find("_hot") == std::string::npos)
            {
                pydPath = entry.path();
                break;
            }
        }

        if (pydPath.empty())
        {
            VansConsole::Get().LogPython("[PYD-Reload] No " + moduleName + ".pyd found in " + m_ScriptDir);
            return;
        }

        m_PydReloadCounter++;
        std::string tempName = moduleName + "_hot" + std::to_string(m_PydReloadCounter) + ".pyd";
        std::filesystem::path tempPath = std::filesystem::path(m_ScriptDir) / tempName;

        // Clean up previous temp copy (best-effort, ignore errors)
        if (m_PydReloadCounter > 1)
        {
            std::string prevName = moduleName + "_hot" + std::to_string(m_PydReloadCounter - 1) + ".pyd";
            std::filesystem::path prevPath = std::filesystem::path(m_ScriptDir) / prevName;
            std::error_code ec;
            std::filesystem::remove(prevPath, ec);
        }

        // Copy the rebuilt .pyd to a unique temp file
        std::filesystem::copy_file(pydPath, tempPath, std::filesystem::copy_options::overwrite_existing);

        // Remove old module from sys.modules
        py::dict modules = sys.attr("modules");
        if (modules.contains(moduleName.c_str()))
            modules.attr("__delitem__")(moduleName.c_str());

        // Load from the temp copy
        py::object spec = importlib_util.attr("spec_from_file_location")(
            moduleName, tempPath.string());
        py::object mod = importlib_util.attr("module_from_spec")(spec);
        modules[py::str(moduleName)] = mod;
        spec.attr("loader").attr("exec_module")(mod);

        VansConsole::Get().LogPython("[PYD-Reload] Reloaded " + moduleName + " (v" +
            std::to_string(m_PydReloadCounter) + ")");

        // Re-install the correct bridge depending on which module was reloaded
        if (moduleName == "vaninput")
        {
            mod.attr("_install_bridge")(reinterpret_cast<uintptr_t>(VansGetInputBridgePtr()));
        }
        else
        {
            mod.attr("_install_bridge")(reinterpret_cast<uintptr_t>(VansGetEngineBridgePtr()));
        }

        // Reload dependent .py scripts so they pick up the new .pyd
        ReloadAllPyScripts();
    }
    catch (const py::error_already_set& e)
    {
        VansConsole::Get().LogPython("[PYD-Reload] Error: " + std::string(e.what()));
    }
    catch (const std::exception& e)
    {
        VansConsole::Get().LogPython("[PYD-Reload] Error: " + std::string(e.what()));
    }
}

// ---------------------------------------------------------------------------
// 读取项目 Scripts/requirements.txt，将缺失的包安装到引擎内嵌 Python 的
// site-packages。VansScriptSetup 已通过 site.addsitedir() 将该目录加入
// sys.path，安装完后调用 importlib.invalidate_caches() 刷新缓存即可立即使用。
// 若 requirements.txt 不存在则自动创建模板。
// ---------------------------------------------------------------------------
void VansScriptContext::SetupProjectVenv(const std::string& projectRoot)
{
	try
	{
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

		VansConsole::Get().LogPython(
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
				VansConsole::Get().LogPython(
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
				VansConsole::Get().LogPython("[PyDeps] requirements.txt has no packages, skipping install.");
				return;
			}
		}

		// ── 3. 用引擎内嵌 python.exe 的 pip 安装到其自身 site-packages ─────
		// VansScriptSetup 中已通过 site.addsitedir() 将该目录加入 sys.path，
		// 安装完成后调用 importlib.invalidate_caches() 刷新缓存即可立即使用。
		VansConsole::Get().LogPython(
			"[PyDeps] Installing from: " + requirementsPath);

		py::module subprocess = py::module::import("subprocess");
		py::list   pipCmd;
		pipCmd.append(pythonExe);
		pipCmd.append("-m");
		pipCmd.append("pip");
		pipCmd.append("install");
		pipCmd.append("-r");
		pipCmd.append(requirementsPath);
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
			VansConsole::Get().LogPython("[PyDeps] pip install failed: " + err);
		}
		else
		{
			VansConsole::Get().LogPython("[PyDeps] Requirements installed successfully.");
			// 刷新当前解释器的导入缓存，使子进程刚安装的包立即可见
			py::module::import("importlib").attr("invalidate_caches")();
		}
	}
	catch (const py::error_already_set& e)
	{
		VansConsole::Get().LogPython("[PyDeps] Python error: " + std::string(e.what()));
	}
	catch (const std::exception& e)
	{
		VANS_LOG_ERROR("[PyDeps] " << e.what());
	}
}

// ---------------------------------------------------------------------------
void VansScriptContext::VansScriptSetup()
{
    s_Instance = this;

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

    static py::scoped_interpreter guard{};// 每个进程只能创建一个

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
        sys.attr("path").attr("insert")(0, projectScriptDir);

        // 将 Scripts/ 子目录也加入 sys.path，使 Scripts/ 下的模块可直接
        // 用裸名导入（import game_state），方便脚本之间互相引用。
        std::string projectScriptsSubDir = projectScriptDir + "Scripts";
        sys.attr("path").attr("insert")(0, projectScriptsSubDir);

        // ── 确保项目 venv 已创建并将 site-packages 加入 sys.path ─────
        // （解释器刚启动，此处的 Python subprocess 调用是合法的）
        SetupProjectVenv(projectScriptDir);
    }

    // Install stdout/stderr redirect so print() goes to console window
    // (must be after sys.path setup so _engine_redirect.py is findable)
    InstallPythonOutputRedirect();

    // ── 初始化并安装引擎桥接到 vanscomponent ─────────────────────────────
    VansInitEngineBridge();
    try
    {
        py::module vc = py::module::import("vanscomponent");
        vc.attr("_install_bridge")(reinterpret_cast<uintptr_t>(VansGetEngineBridgePtr()));
        VansConsole::Get().LogPython("[Bridge] Engine bridge installed into vanscomponent");
    }
    catch (const py::error_already_set& e)
    {
        VansConsole::Get().LogPython(std::string("[Bridge] Warning: ") + e.what());
        VANS_LOG_ERROR("Failed to install engine bridge: " << e.what());
    }

    // ── 初始化并安装输入桥接到 vaninput ───────────────────────────────────
    VansInitInputBridge();
    try
    {
        py::module vi = py::module::import("vaninput");
        vi.attr("_install_bridge")(reinterpret_cast<uintptr_t>(VansGetInputBridgePtr()));
        VansConsole::Get().LogPython("[Bridge] Input bridge installed into vaninput");
    }
    catch (const py::error_already_set& e)
    {
        VansConsole::Get().LogPython(std::string("[Bridge] Warning (vaninput): ") + e.what());
        VANS_LOG_ERROR("Failed to install input bridge: " << e.what());
    }

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
    VANS_PROFILE_SCOPE("Script::VansScriptUpdateNonCameraScripts", Vans::ProfileCategory::Script);

    VansScriptPreUpdate();
    UpdateScriptComponents(false, true);

    return;
}

void VansScriptContext::VansScriptUpdateCameraScripts()
{
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

void VansScriptContext::UpdateScriptComponents(bool cameraScriptsOnly, bool skipCameraScripts)
{
    // ── Per-object VanPyScriptComponent update ───────────────────────
    if (!m_Scene) return;

    for (auto* obj : m_Scene->m_SceneObjects)
    {
        const bool hasCameraComponent = (obj->GetComponent<VansScriptCameraComponent>() != nullptr);
        if (cameraScriptsOnly && !hasCameraComponent)
            continue;
        if (skipCameraScripts && hasCameraComponent)
            continue;

        for (auto* comp : obj->m_Components)
        {
            auto* pyComp = dynamic_cast<VanPyScriptComponent*>(comp);
            if (!pyComp) continue;

            // Lazy instantiation on first encounter
            if (!pyComp->m_IsValid && !pyComp->m_ScriptPath.empty() && !pyComp->m_ScriptClassName.empty())
            {
                pyComp->m_OwnerObject = obj;
                pyComp->Instantiate();
                pyComp->Enable();
            }

            pyComp->CallUpdate();
        }
    }
}
// ===========================================================================
//  OnPyModuleReloaded — re-instantiate script components after hot reload
// ===========================================================================
void VansScriptContext::OnPyModuleReloaded(const std::string& scriptPath)
{
    if (!m_Scene) return;

    for (auto* obj : m_Scene->m_SceneObjects)
    {
        for (auto* comp : obj->m_Components)
        {
            auto* pyComp = dynamic_cast<VanPyScriptComponent*>(comp);
            if (!pyComp || pyComp->m_ScriptPath != scriptPath) continue;

            pyComp->Teardown();
            pyComp->m_OwnerObject = obj;
            pyComp->Instantiate();
            pyComp->Enable();
        }
    }
}

// ===========================================================================
//  VanPyScriptComponent — lifecycle implementations
// ===========================================================================

void VanPyScriptComponent::Instantiate()
{
    try
    {
        // Resolve absolute path — prefer user project root, fall back to engine root
        auto vansConfigration = VansConfigration::GetInstance();
        auto& projectMgr = Vans::VansProjectManager::Get();
        std::string projectRoot = projectMgr.IsProjectLoaded()
            ? projectMgr.GetProjectRootPath()
            : vansConfigration->GetProjectRootPath();

        std::filesystem::path absPath = std::filesystem::path(projectRoot) / m_ScriptPath;

        if (!std::filesystem::exists(absPath))
        {
            VansConsole::Get().LogPython(
                "[PyScript] Script file not found: " + absPath.string());
            m_IsValid = false;
            return;
        }

        // Ensure the project root is on sys.path so user scripts can import
        // each other.  This is a lazy update — VansScriptSetup() may run
        // before the project is opened, so we add it here the first time
        // a project script is loaded.
        {
            py::module sys = py::module::import("sys");
            py::list   path = sys.attr("path");
            bool found = false;
            for (auto item : path)
            {
                if (item.cast<std::string>() == projectRoot)
                { found = true; break; }
            }
            if (!found)
                path.attr("insert")(0, projectRoot);

            // 同步确保 Scripts/ 子目录也在 sys.path 中
            std::string scriptsSubDir = projectRoot + "Scripts";
            bool foundScripts = false;
            for (auto item : path)
            {
                if (item.cast<std::string>() == scriptsSubDir)
                { foundScripts = true; break; }
            }
            if (!foundScripts)
                path.attr("insert")(0, scriptsSubDir);
        }

        // Derive a unique Python module name from the relative path
        // e.g. "Scripts/my_rotator.py" -> "Scripts.my_rotator"
        m_ScriptModuleName = m_ScriptPath;
        // Strip .py extension
        if (m_ScriptModuleName.size() > 3 &&
            m_ScriptModuleName.substr(m_ScriptModuleName.size() - 3) == ".py")
        {
            m_ScriptModuleName = m_ScriptModuleName.substr(0, m_ScriptModuleName.size() - 3);
        }
        // Replace path separators with dots
        std::replace(m_ScriptModuleName.begin(), m_ScriptModuleName.end(), '/', '.');
        std::replace(m_ScriptModuleName.begin(), m_ScriptModuleName.end(), '\\', '.');

        // Load the script from file path using importlib.util
        py::module importlib_util = py::module::import("importlib.util");
        py::module sys = py::module::import("sys");

        py::object spec = importlib_util.attr("spec_from_file_location")(
            m_ScriptModuleName, absPath.string());
        if (spec.is_none())
        {
            VansConsole::Get().LogPython(
                "[PyScript] Failed to create module spec for: " + absPath.string());
            m_IsValid = false;
            return;
        }

        py::object mod = importlib_util.attr("module_from_spec")(spec);
        sys.attr("modules")[py::str(m_ScriptModuleName)] = mod;
        spec.attr("loader").attr("exec_module")(mod);

        py::module scriptMod = mod.cast<py::module>();

        // Register this module for hot-reload tracking (idempotent)
        if (auto* ctx = VansScriptContext::GetInstance())
            ctx->TrackPyModule(m_ScriptPath, m_ScriptModuleName, scriptMod, absPath);

        py::object cls = scriptMod.attr(m_ScriptClassName.c_str());
        m_PyInstance = cls();   // call the constructor

        // Validate: the instance must be a vanspyscript subclass
        py::module vc = py::module::import("vanscomponent");
        if (!py::isinstance(m_PyInstance, vc.attr("vanspyscript")))
        {
            VansConsole::Get().LogPython(
                "[PyScript] " + m_ScriptPath + "::" + m_ScriptClassName +
                " does not inherit from vanspyscript!");
            m_IsValid = false;
            return;
        }

        // Pass the owning VansScriptObject to the Python instance via bridge
        if (m_OwnerObject)
        {
            m_PyInstance.attr("_bind_native_object")(
                reinterpret_cast<uintptr_t>(m_OwnerObject));
        }

        m_IsValid = true;
        VansConsole::Get().LogPython(
            "[PyScript] Loaded " + m_ScriptClassName + " from " + m_ScriptPath);
    }
    catch (const py::error_already_set& e)
    {
        VansConsole::Get().LogPython(
            "[PyScript] Failed to instantiate " + m_ScriptPath +
            "::" + m_ScriptClassName + ": " + e.what());
        m_IsValid = false;
    }
}

void VanPyScriptComponent::Enable()
{
    if (!m_IsValid || m_IsEnabled) return;
    try
    {
        m_PyInstance.attr("on_enable")();
        m_IsEnabled = true;
    }
    catch (const py::error_already_set& e)
    {
        VansConsole::Get().LogPython("[PyScript] on_enable error: " + std::string(e.what()));
    }
}

void VanPyScriptComponent::CallUpdate()
{
    VANS_PROFILE_SCOPE("Script::CallUpdate", Vans::ProfileCategory::Script);

    if (!m_IsValid || !m_IsEnabled) return;
    try
    {
        m_PyInstance.attr("update")();
    }
    catch (const py::error_already_set& e)
    {
        VansConsole::Get().LogPython("[PyScript] update error: " + std::string(e.what()));
    }
}

void VanPyScriptComponent::Disable()
{
    if (!m_IsValid || !m_IsEnabled) return;
    try
    {
        m_PyInstance.attr("on_disable")();
        m_IsEnabled = false;
    }
    catch (const py::error_already_set& e)
    {
        VansConsole::Get().LogPython("[PyScript] on_disable error: " + std::string(e.what()));
    }
}

void VanPyScriptComponent::Teardown()
{
    if (m_IsEnabled) Disable();
    m_PyInstance = py::none();
    m_IsValid = false;
}

// ===========================================================================
//  Physics event callback methods on VanPyScriptComponent
// ===========================================================================

void VanPyScriptComponent::CallOnCollisionEnter(const PhysicsEventInfo& info)
{
    if (!m_IsValid || !m_IsEnabled) return;
    try
    {
        m_PyInstance.attr("on_collision_enter")(info);
    }
    catch (const py::error_already_set& e)
    {
        VansConsole::Get().LogPython("[PyScript] on_collision_enter error: " + std::string(e.what()));
    }
}

void VanPyScriptComponent::CallOnCollisionExit(const PhysicsEventInfo& info)
{
    if (!m_IsValid || !m_IsEnabled) return;
    try
    {
        m_PyInstance.attr("on_collision_exit")(info);
    }
    catch (const py::error_already_set& e)
    {
        VansConsole::Get().LogPython("[PyScript] on_collision_exit error: " + std::string(e.what()));
    }
}

void VanPyScriptComponent::CallOnTriggerEnter(const PhysicsEventInfo& info)
{
    if (!m_IsValid || !m_IsEnabled) return;
    try
    {
        m_PyInstance.attr("on_trigger_enter")(info);
    }
    catch (const py::error_already_set& e)
    {
        VansConsole::Get().LogPython("[PyScript] on_trigger_enter error: " + std::string(e.what()));
    }
}

void VanPyScriptComponent::CallOnTriggerExit(const PhysicsEventInfo& info)
{
    if (!m_IsValid || !m_IsEnabled) return;
    try
    {
        m_PyInstance.attr("on_trigger_exit")(info);
    }
    catch (const py::error_already_set& e)
    {
        VansConsole::Get().LogPython("[PyScript] on_trigger_exit error: " + std::string(e.what()));
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
        const char* typeStr = "Unknown";
        switch (event.type)
        {
        case VansEngine::PhysicsEventType::CollisionEnter: typeStr = "CollisionEnter"; break;
        case VansEngine::PhysicsEventType::CollisionExit:  typeStr = "CollisionExit";  break;
        case VansEngine::PhysicsEventType::TriggerEnter:   typeStr = "TriggerEnter";   break;
        case VansEngine::PhysicsEventType::TriggerExit:    typeStr = "TriggerExit";    break;
        }
        VANS_LOG("[PhysX Dispatch] Event: type=" << typeStr
                 << " A='" << event.nameA << "' (tid=" << event.transformID_A << ")"
                 << " B='" << event.nameB << "' (tid=" << event.transformID_B << ")");

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
    bool foundObj = false;
    bool foundPyComp = false;

    for (auto* obj : m_Scene->m_SceneObjects)
    {
        if (obj->m_TransformID != selfTransformID) continue;
        foundObj = true;

        // VANS_LOG("[PhysX Dispatch] Found SceneObject for tid=" << selfTransformID
        //          << " name='" << obj->m_ObjectName << "' components=" << obj->m_Components.size());

        // 构建 Python 侧的 PhysicsEventInfo
        PhysicsEventInfo info;
        info.otherName          = otherName;
        info.otherTransformID   = otherTransformID;
        info.contactPoint       = PyVec3(contactPoint.x, contactPoint.y, contactPoint.z);
        info.contactNormal      = PyVec3(contactNormal.x, contactNormal.y, contactNormal.z);
        info.impulse            = impulse;

        // 遍历该对象上的所有 PyScript 组件并调度事件
        for (auto* comp : obj->m_Components)
        {
            auto* pyComp = dynamic_cast<VanPyScriptComponent*>(comp);
            if (!pyComp) continue;
            foundPyComp = true;

            VANS_LOG("[PhysX Dispatch] Calling callback on PyComp script='" << pyComp->m_ScriptPath
                     << "' class='" << pyComp->m_ScriptClassName
                     << "' valid=" << pyComp->m_IsValid << " enabled=" << pyComp->m_IsEnabled);

            switch (event.type)
            {
            case VansEngine::PhysicsEventType::CollisionEnter:
                pyComp->CallOnCollisionEnter(info); break;
            case VansEngine::PhysicsEventType::CollisionExit:
                pyComp->CallOnCollisionExit(info); break;
            case VansEngine::PhysicsEventType::TriggerEnter:
                pyComp->CallOnTriggerEnter(info); break;
            case VansEngine::PhysicsEventType::TriggerExit:
                pyComp->CallOnTriggerExit(info); break;
            }
        }
        break; // 每个 transformID 只对应一个 ScriptObject
    }

    // if (!foundObj)
    // {
    //     VANS_LOG_WARN("[PhysX Dispatch] No SceneObject found for selfTransformID=" << selfTransformID);
    // }
    // else if (!foundPyComp)
    // {
    //     VANS_LOG_WARN("[PhysX Dispatch] SceneObject tid=" << selfTransformID
    //                   << " has no VanPyScriptComponent");
    // }
}