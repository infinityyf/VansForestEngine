#pragma once

#include "VansNavigationRuntimeComponents.h"
#include "VansNavigationTypes.h"
#include "../AssetCore/VansTriangleMeshData.h"
#include "../AssetCore/VansHeightfieldData.h"
#include <memory>

namespace Vans
{
enum class VansNavigationGeometryKind : unsigned char { Collision, Terrain, Pcg, CustomMesh };
inline constexpr std::size_t kMaximumNavigationObstacles=1024;

// 已冻结的世界空间阻挡事实；Box 只绕世界 Y 旋转，Cylinder 的 center 为底面中心。
struct VansNavigationObstacleGeometry
{
    std::string id;
    VansNavigationObstacleGeometryKind kind=VansNavigationObstacleGeometryKind::Cylinder;
    glm::vec3 center{0},halfExtents{.5f,1,.5f};
    float radius=.5f,height=2.f,yawRadians=0;
};
struct VansNavigationObstacleRecord
{
    VansNavigationObstacleMode mode=VansNavigationObstacleMode::Carve;
    std::string mapId="World";
    std::vector<std::string> profiles{"Humanoid"};
    bool enabled=true;
    VansNavigationObstacleGeometry shape;
};
bool ValidateNavigationObstacleGeometry(const VansNavigationObstacleGeometry& shape,std::string& error);
bool ValidateNavigationObstacleRecord(const VansNavigationObstacleRecord& obstacle,
    const VansNavigationSettings& settings,std::string& error);
void NavigationObstacleGeometryBounds(const VansNavigationObstacleGeometry& shape,glm::vec3& minimum,glm::vec3& maximum);
bool FreezeNavigationObstacleGeometry(const VansNavigationObstacleShape& shape,const glm::mat4& worldTransform,
    const std::string& id,VansNavigationObstacleGeometry& output,std::string& error);

// 组合层冻结的数据事实；收集器和 worker 不保存 Scene、actor 或 resolver。
struct VansNavigationSourceRecord
{
	std::string id;
	std::string ownerId;
	std::vector<std::string> ancestors;
	VansNavigationGeometryKind kind = VansNavigationGeometryKind::Collision;
	std::shared_ptr<const VansTriangleMeshData> geometry;
	std::shared_ptr<const VansHeightfieldData> heightfield;
	glm::mat4 worldTransform{ 1.0f };
	glm::vec3 boundsMinimum{ 0.0f }, boundsMaximum{ 0.0f };
	std::string collisionLayer;
	std::string area;
	bool enabled = true;
	bool isStatic = true;
	bool isTrigger = false;
	bool hasExplicitSource = false;
	bool sourceEnabled = true;
	VansRuntimeNavigationSourceComponent source;
	std::string geometryError;
};

struct VansNavigationSurfaceRecord
{
	std::string id;
	std::string ownerId;
	bool enabled = true;
	glm::mat4 worldTransform{ 1.0f };
	VansRuntimeNavigationSurfaceComponent surface;
};

struct VansNavigationModifierRecord
{
	std::string id;
	bool enabled = true;
	glm::mat4 worldTransform{ 1.0f };
	VansRuntimeNavigationModifierComponent modifier;
};

struct VansNavigationLinkRecord
{
    std::string id;
    bool enabled=true;
    glm::mat4 worldTransform{1};
    VansRuntimeNavigationLinkComponent link;
};
// Effective world-space connection supplied to both ordinary Tile and TileCache
// builders. It owns no Scene, action or native handle.
struct VansNavigationLinkGeometry
{
    std::string id;
    VansNavigationLinkKey key;
    glm::vec3 start{0},end{0};
    float radiusMeters=0;
    std::uint8_t area=0;
    bool bidirectional=true;
};
VansNavigationLinkKey DeriveNavigationLinkKey(const std::string& stableIdentity);
bool ValidateNavigationLinkRecord(const VansNavigationLinkRecord& record,
    const VansNavigationSettings& settings,std::string& error);
bool ValidateNavigationLinkGeometry(const VansNavigationLinkGeometry& link,
    const VansNavigationAreaSettings& areas,std::string& error);
bool FreezeNavigationLinkGeometry(const VansNavigationLinkRecord& record,
    const VansNavigationSettings& settings,VansNavigationLinkGeometry& output,std::string& error);

struct VansNavigationSourceSnapshot
{
	std::vector<std::string> collisionLayers;
	std::vector<VansNavigationSourceRecord> sources;
	std::vector<VansNavigationSurfaceRecord> surfaces;
	std::vector<VansNavigationModifierRecord> modifiers;
    std::vector<VansNavigationObstacleRecord> obstacles;
    std::vector<VansNavigationLinkRecord> links;
    struct Exclusion
    {
        std::string id;
        VansNavigationGeometryKind kind=VansNavigationGeometryKind::Pcg;
        std::uint64_t instanceCount=0;
        std::string reason;
    };
    // Producer exclusions are diagnostics, never fake geometry or bake fingerprints.
    std::vector<Exclusion> exclusions;
};

struct VansNavigationContribution
{
	std::string id;
	std::uint64_t contentHash = 0;
};

// 高度网格保持共享采样，仅在当前 Tile 的 border 范围内展开三角形。
struct VansNavigationHeightfieldInput
{
	std::string sourceId;
	std::shared_ptr<const VansHeightfieldData> data;
	glm::mat4 worldTransform{1.0f};
	glm::vec3 boundsMinimum{0.0f}, boundsMaximum{0.0f};
	unsigned char area = 0;
	std::vector<VansNavigationSurfaceRecord> surfaces;
};

// Runtime-only effective coverage. Cooked assets retain contribution hashes;
// these frozen bounds let an owner invalidate old and new affected cells.
struct VansNavigationBuildFootprint
{
    std::string id;
    std::uint64_t contentHash=0;
    glm::vec3 boundsMinimum{0},boundsMaximum{0};
};
struct VansNavigationProfileBuildInput
{
	std::string profileId;
	std::shared_ptr<const VansNavigationGeometry> geometry;
	std::vector<VansNavigationContribution> contributions;
	std::vector<VansNavigationModifierRecord> modifiers;
	std::vector<VansNavigationHeightfieldInput> heightfields;
    std::vector<VansNavigationBuildFootprint> footprints;
    std::vector<VansNavigationObstacleGeometry> carveObstacles;
    std::vector<VansNavigationLinkGeometry> links;
	// 已验证 cooked 源中的零面积面没有碰撞面积；离线报告实际跳过次数。
	std::uint64_t skippedZeroAreaTriangles = 0;
};

struct VansNavigationBuildInput
{
	std::string mapId;
	std::vector<VansNavigationProfileBuildInput> profiles;
};

bool ComputeNavigationBuildInputHash(const VansNavigationProfileBuildInput& input,
	std::uint64_t& hash, std::string& error);
// Geometry/modifier provenance excludes transient Carve state; the full fingerprint appends it.
bool ComputeNavigationBuildBaseHash(const VansNavigationProfileBuildInput& input,
    std::uint64_t& hash,std::string& error);
bool ComputeNavigationSurfaceContributionHash(const VansNavigationSurfaceRecord& record,
	std::uint64_t& hash, std::string& error);
bool ValidateNavigationModifierRecord(const VansNavigationModifierRecord& record,
	const VansNavigationSettings& settings, const std::string& mapId, std::string& error);
}
