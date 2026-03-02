#include "VansRuntimeRenderTextureManager.h"

using namespace VansGraphics;

bool VansRuntimeRenderTextureManager::Add(const std::string& name, VansTexture* texture, bool replaceExisting)
{
	if (name.empty() || texture == nullptr)
	{
		return false;
	}

	auto it = m_RuntimeRenderTextures.find(name);
	if (it != m_RuntimeRenderTextures.end())
	{
		if (!replaceExisting)
		{
			return false;
		}

		if (it->second != texture)
		{
			delete it->second;
		}
		it->second = texture;
		return true;
	}

	m_RuntimeRenderTextures.emplace(name, texture);
	return true;
}

VansTexture* VansRuntimeRenderTextureManager::Get(const std::string& name) const
{
	auto it = m_RuntimeRenderTextures.find(name);
	if (it == m_RuntimeRenderTextures.end())
	{
		return nullptr;
	}

	return it->second;
}

bool VansRuntimeRenderTextureManager::Has(const std::string& name) const
{
	return m_RuntimeRenderTextures.find(name) != m_RuntimeRenderTextures.end();
}

bool VansRuntimeRenderTextureManager::Remove(const std::string& name)
{
	auto it = m_RuntimeRenderTextures.find(name);
	if (it == m_RuntimeRenderTextures.end())
	{
		return false;
	}

	delete it->second;
	m_RuntimeRenderTextures.erase(it);
	return true;
}

void VansRuntimeRenderTextureManager::Clear()
{
	for (auto& kv : m_RuntimeRenderTextures)
	{
		delete kv.second;
	}
	m_RuntimeRenderTextures.clear();
}
