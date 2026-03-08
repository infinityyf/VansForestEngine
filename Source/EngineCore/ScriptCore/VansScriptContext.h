#pragma once
//创建一个python的上下文环境用于调用python的逻辑
//1. 每帧会调用VansScriptUpdate函数，这里会调用对应python上下文的函数
//2. 剩下的交给python, python里会去找梭有的python对象，调用update函数，这个update函数则是引擎暴露出去的接口

#include <vector>
#include <string>
#include <filesystem>
#include <unordered_map>
#include <pybind11/embed.h>
namespace py = pybind11;

class VansScriptContext
{
private:
	py::module testModule;

	// ---- Hot-reload infrastructure ----
	std::string m_ScriptDir;  // path to EngineExported/

	// Track last-write-time for each imported .py file
	struct PyModuleInfo {
		py::module   module;
		std::filesystem::file_time_type lastWriteTime;
		std::filesystem::path           filePath;
	};
	std::unordered_map<std::string, PyModuleInfo> m_TrackedPyModules;

	// Counter for unique temp .pyd file names during copy-based reload
	int m_PydReloadCounter = 0;

	// Accumulator so we don't stat the filesystem every single frame
	float m_FileCheckAccumulator = 0.0f;
	static constexpr float FILE_CHECK_INTERVAL = 0.5f;  // seconds

	// Internal helpers
	void TrackPyModule(const std::string& name, py::module mod);
	void CheckAndReloadPyScripts();

public:

	void VansScriptSetup();

	void VansScriptUpdate();

	// Explicit reload called from editor UI
	void ReloadAllPyScripts();

	// Reload the .pyd C++ extension module (copy-based hot-reload on Windows)
	void ReloadPydModule(const std::string& moduleName = "vanscomponent");

	// Access from editor windows
	static VansScriptContext* GetInstance() { return s_Instance; }

private:
	static VansScriptContext* s_Instance;
};

class VansScriptComponent
{
public:
	std::string m_ComponentName;
};

class VansScriptObject
{
public:

	std::vector<VansScriptComponent> m_Components;
};


class VansScriptTransform : public VansScriptComponent
{
public:

	float positionX;
	float positionY;
	float positionZ;
};