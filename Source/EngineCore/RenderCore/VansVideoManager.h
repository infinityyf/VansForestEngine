#pragma once
#include "VulkanCore/VansVideoTexture.h"
#include <unordered_map>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace VansGraphics
{
    // ===========================================================================
    // VansVideoManager — 管理场景内所有视频纹理的生命周期
    //
    // 设计原则：
    //   - 每条视频纹理以 name 为键唯一标识，name 来自 JSON 中 video_textures 数组。
    //   - 视频纹理由 unique_ptr 持有，生命周期与当前场景绑定。
    //   - VansScene 内嵌一个 VansVideoManager 成员，无需 new/delete。
    //   - TickAll() 每帧调用，推进所有视频的播放并上传就绪帧到 GPU。
    //   - Clear() 在 UnLoadScene() 中调用，停止所有线程并释放资源。
    // ===========================================================================
    class VansVideoManager
    {
    public:
        VansVideoManager()  = default;
        ~VansVideoManager() = default;

        // 禁止拷贝，允许移动
        VansVideoManager(const VansVideoManager&)            = delete;
        VansVideoManager& operator=(const VansVideoManager&) = delete;
        VansVideoManager(VansVideoManager&&)                 = default;
        VansVideoManager& operator=(VansVideoManager&&)      = default;

        // ── 批量加载 ─────────────────────────────────────────────────────────
        // 解析 scene JSON 中的 "video_textures" 数组。
        // 每条记录格式：
        //   {
        //     "name"     : "MyVideo",        // 引擎内唯一标识（材质引用时使用）
        //     "path"     : "Videos/clip.mp4",// 相对于 projectRoot 的路径
        //     "loop"     : true,             // 可选，默认 true
        //     "autoplay" : true,             // 可选，默认 true
        //     "srgb"     : true              // 可选，默认 true
        //   }
        void LoadFromJson(const json& videoArray,
                          const std::string& projectRoot,
                          VansVKDevice* device);

        // ── 单条查找 ──────────────────────────────────────────────────────────
        // 按名称查找视频纹理，找不到返回 nullptr。
        VansVideoTexture* Get(const std::string& name) const;

        // ── 每帧驱动 ─────────────────────────────────────────────────────────
        // 推进所有处于播放状态的视频，挑选本帧应显示的新帧。
        // deltaTime : 本帧耗时（秒）
        void TickAll(double deltaTime);

        // 在渲染命令录制阶段，将 TickAll() 选中的视频帧批量记录到当前 command buffer。
        void RecordPendingUploads(VansVKCommandBuffer& cmd);

        // ── 场景切换时暂停 ────────────────────────────────────────────────
        // 暂停所有视频播放，但保留 GPU 纹理资源（VkImage/VkImageView 仍然有效）。
        // 视频是项目级资源，生命周期与 mesh/texture 相同，不随场景卸载而销毁。
        // 在 UnLoadScene() 中调用，替代原来错误的 Clear()。
        void PauseAll();

        // 恢复所有视频播放（场景加载完成后调用）
        void PlayAll();

        // ── 项目卸载时清理 ───────────────────────────────────────────────
        // 停止所有后台解码线程，释放全部 FFmpeg 和 GPU 资源。
        // Called when unloading a project or shutting down the engine.
        void Clear();

        bool IsEmpty() const { return m_Videos.empty(); }
        size_t Count() const { return m_Videos.size(); }

    private:
        // name → VansVideoTexture（unique_ptr 持有所有权）
        std::unordered_map<std::string, std::unique_ptr<VansVideoTexture>> m_Videos;
    };

} // namespace VansGraphics
