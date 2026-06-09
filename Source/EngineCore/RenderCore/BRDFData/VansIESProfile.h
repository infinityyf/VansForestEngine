#pragma once
#include <string>
#include <vector>
#include "../VulkanCore/VansVKImage.h"
#include "vulkan/vulkan.h"

namespace VansGraphics
{
    // 前向声明，避免循环 include
    class VansVKDevice;
    class VansVKCommandBuffer;

    // IES LM-63-2002 单文件的 CPU 端中间表示
    struct IESProfileData
    {
        std::string m_Manufacturer;
        std::string m_CatalogNumber;
        std::string m_Description;

        int   m_NumVerticalAngles   = 0;
        int   m_NumHorizontalAngles = 0;
        int   m_PhotometricType     = 1;  // 1=C（最常见）, 2=B, 3=A
        int   m_UnitsType           = 2;  // 1=feet, 2=meters

        float m_CandelaMultiplier   = 1.0f;
        float m_MaxCandela          = 1.0f;
        float m_TotalLumens         = 0.0f;

        // 垂直角度列表（升序，单位：度）
        std::vector<float> m_VerticalAngles;
        // 水平角度列表（升序，单位：度）
        std::vector<float> m_HorizontalAngles;
        // 坎德拉值矩阵：numH × numV 展开，行主序（[hIdx * numV + vIdx]）
        std::vector<float> m_CandelaValues;

        // 最后一个水平角度：0°=完全轴对称，90°=四分之一对称，180°=半对称，360°=无对称
        float m_LastHorizontalAngle = 0.0f;
    };

    // ============================================================
    // VansIESProfileManager
    //   职责：解析 IES 文件 → 烘焙 256×128 R16F 纹理 → 管理 GPU sampler2DArray
    //   Descriptor Set 绑定由 VansDeferredRenderNode 负责（set=1, binding=16）
    // ============================================================
    class VansIESProfileManager
    {
    public:
        // GPU 纹理参数
        static constexpr int kBakeWidth   = 256;
        static constexpr int kBakeHeight  = 128;
        static constexpr int kMaxProfiles = 32;  // Texture2DArray 最大层数

        // 从文件加载 IES profile，返回 atlas 层索引（写入光源的 m_IESProfileIndex）
        // 返回 false 表示解析失败
        bool LoadIESFile(const std::string& filePath, int& outProfileIndex);

        // 从内存加载（支持从 ieslibrary.com 下载的字节流）
        bool LoadIESFromMemory(const char* data, size_t dataSize, int& outProfileIndex);

        // 获取已加载的 profile 数量
        uint32_t GetProfileCount() const { return static_cast<uint32_t>(m_Profiles.size()); }

        // 获取指定 profile 数据（只读）
        const IESProfileData& GetProfileData(int index) const { return m_Profiles[index]; }

        // 将指定 profile 烘焙到 FP32 像素数组（R 通道，归一化到 [0,1]）
        void BakeToPixels(int profileIndex, int width, int height,
                          std::vector<float>& outPixels) const;

        // 获取 GPU sampler2DArray（Deferred pass binding=16 使用）
        VansVKImage& GetIESProfileTexture() { return m_IESTextureArray; }

        // 步骤 1：创建 GPU 纹理对象（仅分配 VkImage，不上传数据）
        void CreateGPUResources(VkDevice& logicDevice);

        // 步骤 2：上传所有已加载 profile 的像素数据到 GPU（需要 command buffer 做 staging 拷贝）
        // 在 CreateGPUResources 之后调用，传入 VansVKDevice 和一次性 command buffer
        void UploadAllProfiles(VansVKDevice* device, VansVKCommandBuffer& cmd);

        // 销毁 GPU 资源
        void DestroyGPUResources(VkDevice& logicDevice);

        // 是否已创建 GPU 资源
        bool IsGPUResourcesCreated() const { return m_GPUResourcesCreated; }

        ~VansIESProfileManager();

    private:
        // 解析 IES 文件内容到 IESProfileData
        bool ParseIES(const std::string& text, IESProfileData& outData);

        // 双线性插值采样单个 profile 的坎德拉值（角度单位：度）
        float SampleCandela(const IESProfileData& data,
                            float vertDeg, float horizDeg) const;

        // 根据对称类型折叠水平角度
        float ApplyHorizontalSymmetry(const IESProfileData& data, float horizDeg) const;

        std::vector<IESProfileData> m_Profiles;
        VansVKImage                 m_IESTextureArray;  // sampler2DArray，格式 VK_FORMAT_R16_SFLOAT
        bool                        m_GPUResourcesCreated = false;
    };
}
