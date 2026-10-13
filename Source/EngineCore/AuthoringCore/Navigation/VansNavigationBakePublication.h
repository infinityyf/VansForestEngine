#pragma once
#include "../../AssetCore/VansAssetObjectRepository.h"
#include "../../NavigationCore/VansNavigationMeshAsset.h"
#include <filesystem>
#include <memory>
#include <string>

namespace Vans
{
// 显式 Bake 的文件/索引候选。作者引用事务验证完成后才能 Commit；销毁未提交候选自动回滚。
class VansNavigationBakePublication final
{
public:
    static std::unique_ptr<VansNavigationBakePublication> Prepare(VansAssetDatabase& database,
        VansAssetObjectRepository& repository,std::shared_ptr<const VansNavigationMeshAsset> asset,
        const std::filesystem::path& outputPath,std::string& error);
    ~VansNavigationBakePublication();
    VansNavigationBakePublication(const VansNavigationBakePublication&)=delete;
    VansNavigationBakePublication& operator=(const VansNavigationBakePublication&)=delete;
    const VansAssetRecord& Record() const;
    bool Commit(std::string& error);
private:
    struct State;
    explicit VansNavigationBakePublication(std::unique_ptr<State> state);
    std::unique_ptr<State> m_State;
};
}
