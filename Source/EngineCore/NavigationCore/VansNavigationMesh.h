#pragma once

#include "VansNavigationSource.h"
#include "VansNavigationTypes.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class dtNavMesh;
class dtNavMeshQuery;
class dtQueryFilter;

namespace Vans
{
struct VansNavigationQueryScratch;

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
		const VansNavigationSettings& settings,
		std::string& error);
	bool Save(const std::filesystem::path& path,
		const VansNavigationSource& source,
		std::string& error) const;
	bool Load(const std::filesystem::path& path,
		const VansNavigationSettings& runtimeSettings,
		std::string& error);

	VansNavigationPath FindPath(const glm::vec3& start,
		const glm::vec3& end,
		const glm::vec3& nearestExtents = glm::vec3(1.0f, 2.0f, 1.0f)) const;

	bool IsReady() const
	{
		return m_NavMesh != nullptr && m_Query != nullptr &&
			m_QueryFilter != nullptr && m_QueryScratch != nullptr;
	}
	const VansNavigationBakeSettings& GetBakeSettings() const { return m_Settings.bake; }
	const VansNavigationSource& GetSource() const { return m_Source; }

private:
	void Reset();
	bool InitializeFromData(std::vector<unsigned char> data,
		const VansNavigationBakeSettings& bakedSettings,
		const VansNavigationSettings& settings,
		std::string& error);

	dtNavMesh* m_NavMesh = nullptr;
	dtNavMeshQuery* m_Query = nullptr;
	std::unique_ptr<dtQueryFilter> m_QueryFilter;
	std::unique_ptr<VansNavigationQueryScratch> m_QueryScratch;
	std::vector<unsigned char> m_SerializedData;
	VansNavigationSettings m_Settings;
	VansNavigationSource m_Source;
};
}
