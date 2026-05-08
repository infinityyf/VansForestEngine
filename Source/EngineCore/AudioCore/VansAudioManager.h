#pragma once
#include "VansAudioNode.h"
#include <unordered_map>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace VansEngine
{
    // ===========================================================================
    // VansAudioManager — 管理场景内所有音频节点的生命周期
    //
    // 对标 VansVideoManager（VansGraphics 命名空间），但音频无 GPU 依赖，
    // 因此放在 VansEngine 命名空间下。
    //
    // 使用方式：
    //   VansScene 内嵌一个 VansAudioManager 成员，无需 new/delete。
    //
    // 两阶段加载（对应 AudioSystem.md Section 10.2）：
    //   1. LoadFromJson()    — 项目级调用（LoadResources 中）：按 resource.json["audio"]
    //                          逐条创建 VansAudioNode，并调用 Open() 加载解码。
    //   2. ApplySceneConfig() — 场景级调用（LoadSceneObjects 中）：按 scene["audio_sources"]
    //                          调整单条节点的空间化、音量、自动播放等运行时参数，
    //                          并触发 AutoPlay。
    //
    // 卸载策略：
    //   UnLoadProject -> Clear()：停止后台线程 + 释放所有资源
    //   UnLoadScene   -> StopAll()：仅停止播放，保留已解码资源（静态模式）
    // ===========================================================================
    class VansAudioManager
    {
    public:
        VansAudioManager()  = default;
        ~VansAudioManager();

        VansAudioManager(const VansAudioManager&)            = delete;
        VansAudioManager& operator=(const VansAudioManager&) = delete;
        VansAudioManager(VansAudioManager&&)                 = default;
        VansAudioManager& operator=(VansAudioManager&&)      = default;

        // ── 项目级加载（LoadResources 中调用） ──────────────────────────────
        // audioArray   : resource.json["audio"] 数组，每条包含 name/path/play_mode/loop/auto_play 等
        // assetPrefix  : VansProjectManager::GetProjectRootPath()，用于拼接完整路径
        void LoadFromJson(const json& audioArray, const std::string& assetPrefix);

        // ── 场景级配置（LoadSceneObjects / LoadSceneContent 中调用） ────────
        // audioSourcesArray : scene.json["audio_sources"] 数组
        // 每条记录只包含 { "name": "...", 可选覆盖字段 }，name 是在 resource.json 中已注册的
        void ApplySceneConfig(const json& audioSourcesArray);

        // ── 按名称查找，未找到返回 nullptr ───────────────────────────────────
        VansAudioNode* Get(const std::string& name) const;

        // ── 每帧驱动（VansScene::Tick 中调用） ──────────────────────────────
        // deltaTime : 本帧耗时（秒），当前未使用，为将来拓展保留
        // camPosX/Y/Z : 主摄像机世界坐标（传给 OpenAL Listener）
        // camFwdX/Y/Z : 摄像机 Forward 方向（归一化）
        // camUpX/Y/Z  : 摄像机 Up 方向（归一化）
        void TickAll(double deltaTime,
                     float camPosX, float camPosY, float camPosZ,
                     float camFwdX, float camFwdY, float camFwdZ,
                     float camUpX,  float camUpY,  float camUpZ);

        // ── 场景切换时停止所有播放（不释放资源） ─────────────────────────────
        void StopAll();

        // ── 项目卸载时释放全部资源 ────────────────────────────────────────────
        void Clear();

        bool   IsEmpty() const { return m_Nodes.empty(); }
        size_t Count()   const { return m_Nodes.size();  }

    private:
        // name → VansAudioNode（unique_ptr 持有所有权）
        std::unordered_map<std::string, std::unique_ptr<VansAudioNode>> m_Nodes;
    };

} // namespace VansEngine
