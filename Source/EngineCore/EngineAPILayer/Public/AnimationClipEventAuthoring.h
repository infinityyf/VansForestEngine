#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Vans::EditorAPI
{
struct AnimationClipEventDocumentState;
struct AnimationClipEventDTO
{
    std::uint64_t id = 0;
    float time = 0.0f;
    std::string name;
    // JSON 标量或三个数值的数组；Clip 不保存游戏实体、脚本函数或物理参数。
    std::string payloadJson = "null";
};
struct AnimationClipEventDocument
{
    std::string assetGuid;
    std::string path;
    std::string clipName;
    float duration = 0;
    std::vector<AnimationClipEventDTO> events;
    std::shared_ptr<AnimationClipEventDocumentState> state;
};
// 作者工具读写边界：编辑 DTO 只修改内存；显式 Save 验证指纹并原子写入同一个 vclip。
bool OpenAnimationClipEvents(const std::string& assetGuid, AnimationClipEventDocument& document, std::string& error);
bool SaveAnimationClipEvents(AnimationClipEventDocument& document, std::string& error);
}
