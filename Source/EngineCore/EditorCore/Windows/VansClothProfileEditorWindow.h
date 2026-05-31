#pragma once

#include "VansBaseWindowComponent.h"
#include "../../PhysicsCore/VansClothProfile.h"

#include <imgui.h>
#include <GLM/glm.hpp>
#include <string>
#include <vector>

namespace VansGraphics
{
    // =========================================================================
    // VansClothProfileEditorWindow
    // Cloth Profile 独立编辑器窗口。
    // 通过 OpenProfile(profilePath) 或 NewProfile() 打开，编辑器自行根据
    // Profile 内的 m_ModelPath 通过 Assimp CPU-only 加载网格，
    // 完全不依赖场景中的 RenderNode。
    // =========================================================================
    class VansClothProfileEditorWindow : public VansBaseWindowComponent
    {
    public:
        void ShowWindow(VansVKDevice& device) override;

        // 通过 profilePath 打开已有配置文件（HierachyWindow 或 ProjectWindow 调用）
        void OpenProfile(const std::string& profilePath);

        // 新建一个空的 Profile（弹出路径输入后保存）
        void NewProfile();

        bool IsOpen() const { return m_IsOpen; }

    private:
        // ── 核心状态（围绕 Profile 本身，无任何 RenderNode 引用）─────────────
        bool                          m_IsOpen            = false;
        std::string                   m_CurrentProfilePath;
        VansEngine::VansClothProfile  m_Profile;
        bool                          m_IsDirty           = false; // 有未保存改动

        // ── 编辑器专用 CPU 网格数据（由 m_Profile.m_ModelPath 独立加载）──────
        struct EditorParticle
        {
            glm::vec3 m_LocalPosition; // 网格局部空间坐标（焊接后唯一粒子）
            bool      m_IsPinned = false;
        };
        std::vector<EditorParticle> m_WeldedParticles;  // 焊接后唯一粒子列表
        std::vector<uint32_t>       m_WeldedTriangles;  // 线框三角形索引（焊接后粒子空间）
        bool                        m_MeshLoaded  = false;
        std::string                 m_MeshLoadError;    // 加载失败时的错误信息

        // ── 3D 轨道视图相机 ────────────────────────────────────────────────
        float     m_CameraAzimuth   = 45.0f;
        float     m_CameraElevation = 30.0f;
        float     m_CameraDistance  = 3.0f;
        glm::vec3 m_CameraTarget    = glm::vec3(0.0f);
        bool      m_IsOrbitingViewport = false;
        bool      m_IsPanningViewport  = false;

        // ── NewProfile 状态 ────────────────────────────────────────────────
        bool m_ShowNewProfileDialog = false;
        char m_NewProfilePathBuf[512] = {};

        // ── 内部函数 ───────────────────────────────────────────────────────

        // 根据 m_Profile.m_ModelPath 通过 Assimp CPU-only 读取顶点数据
        // 不需要 VkDevice，不上传任何 GPU 资源
        void LoadModelFromProfile();

        // 对原始顶点做位置焊接（同 VansClothNode）并根据 Profile 标记固定点
        void BuildWeldedParticles(const std::vector<glm::vec3>& rawPositions,
                                  const std::vector<uint32_t>& rawIndices);

        // 将 m_WeldedParticles 中的 m_IsPinned 状态同步回 m_Profile.m_PinnedLocalPositions
        void SyncProfilePinnedVertices();

        void DrawProfileInfoPanel();    // 顶部 Profile 元信息与操作按钮
        void DrawMeshViewport();        // ImGui DrawList 3D 投影渲染（左侧）
        void DrawParametersPanel();     // 右侧物理参数 Inspector
        void DrawPinnedParticleList();  // 右侧已固定粒子列表

        void HandleOrbitalCamera(bool isViewportHovered);
        void HandleVertexPicking(ImVec2 viewportMin, ImVec2 viewportSize,
                     const glm::mat4& mvp,
                     const glm::mat4& view);

        void SaveProfile();
        void RevertProfile(); // 重新从文件加载，丢弃当前改动

        // Assimp CPU-only 辅助（静态，不依赖任何引擎对象）
        struct RawEditorMesh
        {
            std::vector<glm::vec3> m_Positions; // 局部空间顶点位置
            std::vector<uint32_t>  m_Indices;   // 三角形索引列表
            bool                   m_Ok    = false;
            std::string            m_Error;
        };
        static RawEditorMesh LoadRawMeshFromFile(const std::string& modelPath);
    };
}
