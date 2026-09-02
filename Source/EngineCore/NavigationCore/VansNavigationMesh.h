#pragma once

#include "VansNavigationTypes.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class dtNavMesh;
class dtNavMeshQuery;

namespace Vans
{
class VansNavigationMesh
{
public:
	VansNavigationMesh();
	~VansNavigationMesh();

	VansNavigationMesh(const VansNavigationMesh&) = delete;
	VansNavigationMesh& operator=(const VansNavigationMesh&) = delete;
	VansNavigationMesh(VansNavigationMesh&& other) noexcept;
	VansNavigationMesh& operator=(VansNavigationMesh&& other) noexcept;

	bool Build(const VansNavigationGeometry& geometry,
		const VansNavigationBuildSettings& settings,
		std::string& error);
	bool Save(const std::filesystem::path& path, std::string& error) const;
	bool Load(const std::filesystem::path& path, std::string& error);

	VansNavigationPath FindPath(const glm::vec3& start,
		const glm::vec3& end,
		const glm::vec3& nearestExtents = glm::vec3(1.0f, 2.0f, 1.0f)) const;

	bool IsReady() const { return m_NavMesh != nullptr && m_Query != nullptr; }
	const VansNavigationBuildSettings& GetBuildSettings() const { return m_Settings; }

private:
	void Reset();
	bool InitializeFromData(std::vector<unsigned char> data,
		const VansNavigationBuildSettings& settings,
		std::string& error);

	dtNavMesh* m_NavMesh = nullptr;
	dtNavMeshQuery* m_Query = nullptr;
	std::vector<unsigned char> m_SerializedData;
	VansNavigationBuildSettings m_Settings;
};
}
