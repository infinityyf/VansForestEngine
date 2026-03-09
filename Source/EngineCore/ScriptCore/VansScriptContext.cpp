#include "VansScriptContext.h"
#include "../Configration/VansConfigration.h"
#include "../EditorCore/Windows/VansConsole.h"
#include "../Util/VansLog.h"
#include "../VansTimer.h"
#include <cstdlib>
#include <string>
#include <algorithm>
#include "../../../ForestExporter/VansPyExporter.h"
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
// ---------------------------------------------------------------------------
void VansScriptContext::TrackPyModule(const std::string& name, py::module mod)
{
    try
    {
        // Python module's __file__ attribute gives us the .py path
        py::object fileAttr = mod.attr("__file__");
        if (!fileAttr.is_none())
        {
            std::filesystem::path filePath(fileAttr.cast<std::string>());
            if (std::filesystem::exists(filePath))
            {
                PyModuleInfo info;
                info.module = mod;
                info.filePath = filePath;
                info.lastWriteTime = std::filesystem::last_write_time(filePath);
                m_TrackedPyModules[name] = info;
            }
        }
    }
    catch (const py::error_already_set& e)
    {
        VANS_LOG_ERROR("TrackPyModule(" << name << "): " << e.what());
    }
}

// ---------------------------------------------------------------------------
// Check every tracked .py file; if it has been modified on disk, reload it.
// ---------------------------------------------------------------------------
void VansScriptContext::CheckAndReloadPyScripts()
{
    py::module importlib = py::module::import("importlib");

    for (auto& [name, info] : m_TrackedPyModules)
    {
        try
        {
            if (!std::filesystem::exists(info.filePath))
                continue;

            auto currentTime = std::filesystem::last_write_time(info.filePath);
            if (currentTime != info.lastWriteTime)
            {
                info.lastWriteTime = currentTime;

                // Reload the module in-place
                importlib.attr("reload")(info.module);
                // Re-fetch in case the reload created a new module object
                info.module = py::module::import(name.c_str());

                // If this is the main test module, update our handle
                if (name == "test")
                    testModule = info.module;

                VansConsole::Get().LogPython("[Hot-Reload] Reloaded " + name + ".py");
            }
        }
        catch (const py::error_already_set& e)
        {
            VansConsole::Get().LogPython("[Hot-Reload] Error reloading " + name + ": " + e.what());
        }
    }
}

// ---------------------------------------------------------------------------
// Explicit reload of all tracked .py modules (called from editor UI)
// ---------------------------------------------------------------------------
void VansScriptContext::ReloadAllPyScripts()
{
    py::module importlib = py::module::import("importlib");

    for (auto& [name, info] : m_TrackedPyModules)
    {
        try
        {
            importlib.attr("reload")(info.module);
            info.module = py::module::import(name.c_str());

            if (name == "test")
                testModule = info.module;

            if (std::filesystem::exists(info.filePath))
                info.lastWriteTime = std::filesystem::last_write_time(info.filePath);

            VansConsole::Get().LogPython("[Reload] Reloaded " + name + ".py");
        }
        catch (const py::error_already_set& e)
        {
            VansConsole::Get().LogPython("[Reload] Error reloading " + name + ": " + e.what());
        }
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

    // Install stdout/stderr redirect so print() goes to console window
    // (must be after sys.path setup so _engine_redirect.py is findable)
    InstallPythonOutputRedirect();

    try 
    {
        testModule = py::module::import("test");
        TrackPyModule("test", testModule);
    }
    catch (const py::error_already_set& e) 
    {
        VansConsole::Get().LogPython(std::string("Exception: ") + e.what());
        VANS_LOG_ERROR("Python exception:\n" << e.what());
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

    try
    {
        testModule.attr("update")();
    }
    catch (const py::error_already_set& e)
    {
        VansConsole::Get().LogPython(std::string("Exception: ") + e.what());
    }

    return;
}
