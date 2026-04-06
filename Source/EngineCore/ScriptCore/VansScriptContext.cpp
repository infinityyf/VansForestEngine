#include "VansScriptContext.h"
#include "../Configration/VansConfigration.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../EditorCore/Windows/VansConsole.h"
#include "../Util/VansLog.h"
#include "../VansTimer.h"
#include "../RenderCore/VansScene.h"
#include <cstdlib>
#include <string>
#include <algorithm>
#include "../../../../ForestExporter/VansEngineBridge.h"

// Defined in VansScriptBridge.cpp
extern void VansInitEngineBridge();
extern VansEngineBridge* VansGetEngineBridgePtr();
namespace py = pybind11;

// Singleton instance
VansScriptContext* VansScriptContext::s_Instance = nullptr;

// ---------------------------------------------------------------------------
// Runs EngineExported/_engine_redirect.py which replaces sys.stdout/stderr
// with objects that route output into the engine's VansConsole.
// ---------------------------------------------------------------------------
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

        // Re-install the engine bridge into the freshly loaded module
        mod.attr("_install_bridge")(reinterpret_cast<uintptr_t>(VansGetEngineBridgePtr()));

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
void VansScriptContext::VansScriptSetup()
{
    s_Instance = this;

    auto vansConfigration = VansConfigration::GetInstance();
    std::string projectRoot = vansConfigration->GetProjectRootPath();
    
    // Set PYTHONHOME to the External Python directory in the project
    std::string pythonHome = projectRoot + "External/Python-3.13.3";
    std::string pythonHomeEnv = "PYTHONHOME=" + pythonHome;
    _putenv(pythonHomeEnv.c_str());

    static py::scoped_interpreter guard{};// 每个进程只能创建一个

    // Add your script directory to sys.path
    // EngineExported holds the .pyd modules and user scripts
    py::module sys = py::module::import("sys");
    m_ScriptDir = projectRoot + "../ForestExporter/EngineExported";
    sys.attr("path").attr("insert")(0, m_ScriptDir);

    // If a user project is loaded, also add the project root to sys.path
    // so that user scripts (e.g. Scripts/test.py) can be discovered.
    auto& projectMgr = Vans::VansProjectManager::Get();
    if (projectMgr.IsProjectLoaded())
    {
        std::string projectScriptDir = projectMgr.GetProjectRootPath();
        sys.attr("path").attr("insert")(0, projectScriptDir);
    }

    // Install stdout/stderr redirect so print() goes to console window
    // (must be after sys.path setup so _engine_redirect.py is findable)
    InstallPythonOutputRedirect();

    // ── Initialize the engine bridge and install it into the .pyd ─────────
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

}

void VansScriptContext::VansScriptUpdate()
{
    // Periodically check for .py file changes
    m_FileCheckAccumulator += VansGraphics::VansTimer::GetDeltaTime();
    if (m_FileCheckAccumulator >= FILE_CHECK_INTERVAL)
    {
        m_FileCheckAccumulator = 0.0f;
        CheckAndReloadPyScripts();
    }

    // ── Per-object VanPyScriptComponent update ───────────────────────
    if (!m_Scene) return;

    for (auto* obj : m_Scene->m_SceneObjects)
    {
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

    return;
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