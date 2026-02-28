#include "VansScriptContext.h"
#include "../Configration/VansConfigration.h"
#include <cstdlib>
#include <string>
#include "../../../ForestExporter/VansPyExporter.h"
namespace py = pybind11;

void VansScriptContext::VansScriptSetup()
{
    auto vansConfigration = VansConfigration::GetInstance();
    std::string projectRoot = vansConfigration->GetProjectRootPath();
    
    // Set PYTHONHOME to the External Python directory in the project
    std::string pythonHome = projectRoot + "External/Python-3.13.3";
    std::string pythonHomeEnv = "PYTHONHOME=" + pythonHome;
    _putenv(pythonHomeEnv.c_str());

    static py::scoped_interpreter guard{};// 每个进程只能创建一个
    

    // Add your script directory to sys.path
    // ForestExporter is at the parent directory level of the project root
    py::module sys = py::module::import("sys");
    std::string exporterPath = projectRoot + "../ForestExporter";
    sys.attr("path").attr("insert")(0, exporterPath);

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
