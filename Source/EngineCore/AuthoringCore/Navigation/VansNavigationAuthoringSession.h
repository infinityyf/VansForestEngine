#pragma once

#include "../../NavigationCore/VansNavigationBuilder.h"
#include "../../NavigationCore/VansNavigationQueryScheduler.h"
#include <cstdint>
#include <memory>
#include <string>

namespace Vans
{
enum class VansNavigationPreviewState : std::uint8_t
{
    Empty, Building, Ready, Failed, Cancelling, Cancelled, Stale
};

struct VansNavigationPreviewSnapshot
{
    VansNavigationPreviewState state = VansNavigationPreviewState::Empty;
    bool busy = false;
    std::uint64_t requestId = 0;
    std::string mapId;
    std::string diagnostic;
    std::size_t sourceCount = 0;
    std::size_t surfaceCount = 0;
    std::vector<VansNavigationSourceSnapshot::Exclusion> exclusions;
    double elapsedMilliseconds = 0;
    std::shared_ptr<const VansNavigationMeshAsset> asset;
};

// 会话只接收冻结数据，worker 不访问 Scene、ProjectManager、文件或编辑器。
class VansNavigationAuthoringSession final
{
public:
    VansNavigationAuthoringSession();
    ~VansNavigationAuthoringSession();
    VansNavigationAuthoringSession(const VansNavigationAuthoringSession&) = delete;
    VansNavigationAuthoringSession& operator=(const VansNavigationAuthoringSession&) = delete;

    bool BuildPreview(VansNavigationSourceSnapshot sources, VansNavigationSettings settings,
        std::string mapId, std::string sourceScene, std::uint64_t sourceObservation,
        std::string& error);
    VansNavigationPreviewSnapshot Capture();
    // 只由作者帧推进一次；Capture/视口绘制不增加查询额度。
    void Update();
    bool SubmitQuery(const VansNavigationQueryRequest& request,std::string& error);
    std::optional<VansNavigationQuerySnapshot> CaptureQuery(std::string& error) const;
    void CancelQuery();
    bool CaptureDebugGeometry(const VansNavigationDebugView& view,const std::string& profileId,
        const std::string& filterId,VansNavigationDebugGeometry& output,std::string& error);
    bool MatchesSources(const VansNavigationSettings& settings, std::uint64_t sourceObservation) const;
    void Invalidate(std::string reason);
    void Cancel();

private:
    struct Task;
    struct Queries;
    void Poll();
    bool PrepareMap(const std::string& profileId,std::string& error);
    void ClearQueries();
    VansNavigationSettings QuerySettings() const;
    std::shared_ptr<Task> m_Task;
    std::unique_ptr<Queries> m_Queries;
    VansNavigationPreviewSnapshot m_Snapshot;
    VansNavigationSettings m_Settings;
    std::uint64_t m_Observation = 0;
};
}
