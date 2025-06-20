#include "VansScriptContext.h"
#include <cstdlib>
#include "../../../ForestExporter/VansPyExporter.h"
namespace py = pybind11;

void VansScriptContext::VansScriptSetup()
{
    _putenv("PYTHONHOME=C:/Users/infinityyf/Projects/githubs/Python-3.13.3/PCbuild/amd64");
    _putenv("PYTHONHOME=C:/Users/infinityyf/Projects/githubs/Python-3.13.3");

    static py::scoped_interpreter guard{};// 每个进程只能创建一次
    

    // Add your script directory to sys.path
    py::module sys = py::module::import("sys");
    sys.attr("path").attr("insert")(0, "C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestExporter");

    try 
    {
        testModule = py::module::import("test");
    }
    catch (const py::error_already_set& e) 
    {
        std::cerr << "Python exception:\n" << e.what() << std::endl;
    }
    
}

void VansScriptContext::VansScriptUpdate()
{
  
    testModule.attr("update")();

    return;
}
