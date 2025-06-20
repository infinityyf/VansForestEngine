#pragma once
//创建一个python的上下文环境用于调用python的逻辑
//1. 每帧会调用VansScriptUpdate函数，这里会调用对应python上下文的函数
//2. 剩下的交给python, python里会去找梭有的python对象，调用update函数，这个update函数则是引擎暴露出去的接口

#include <vector>
#include <string>
#include <pybind11/embed.h>
namespace py = pybind11;

class VansScriptContext
{
private:
	py::module testModule;

public:

	void VansScriptSetup();

	void VansScriptUpdate();
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