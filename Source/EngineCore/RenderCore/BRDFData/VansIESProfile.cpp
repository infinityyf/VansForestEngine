#include "VansIESProfile.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../../Util/VansLog.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    // FP32 → FP16 转换（无 NaN 特殊处理，用于 LUT 数据）
    static uint16_t FloatToHalf(float f)
    {
        union { float f; uint32_t u; } v{ f };
        uint32_t x    = v.u;
        uint32_t sign = (x >> 16) & 0x8000u;
        int32_t  exp  = ((x >> 23) & 0xFFu) - 127 + 15;
        uint32_t mant = x & 0x7FFFFFu;
        uint16_t h;
        if (exp <= 0)
        {
            if (exp < -10) { h = (uint16_t)sign; }
            else
            {
                mant |= 0x800000u;
                uint32_t shift = 14u - (uint32_t)exp;
                uint32_t m = mant >> shift;
                if ((mant >> (shift - 1u)) & 1u) m += 1u;
                h = (uint16_t)(sign | m);
            }
        }
        else if (exp >= 31) { h = (uint16_t)(sign | 0x7C00u); }
        else
        {
            uint32_t m = mant >> 13;
            if (mant & 0x1000u) m += 1u;
            if (m & 0x400u) { m = 0; ++exp; if (exp >= 31) { h = (uint16_t)(sign | 0x7C00u); m = 0; } }
            if (exp < 31) h = (uint16_t)(sign | ((uint32_t)exp << 10) | m);
        }
        return h;
    }

    // 读取一行，跳过空行
    static bool ReadLine(std::istringstream& ss, std::string& out)
    {
        while (std::getline(ss, out))
        {
            // 去掉 Windows 换行符 \r
            if (!out.empty() && out.back() == '\r')
                out.pop_back();
            if (!out.empty())
                return true;
        }
        return false;
    }

    // 从流中读取所有 float 数值（可跨行）
    static std::vector<float> ReadAllFloats(std::istringstream& ss, int count)
    {
        std::vector<float> result;
        result.reserve(count);
        std::string line;
        while ((int)result.size() < count)
        {
            if (!ReadLine(ss, line))
                break;
            std::istringstream ls(line);
            float val;
            while (ls >> val)
                result.push_back(val);
        }
        return result;
    }
}

namespace VansGraphics
{
    // =======================================================================
    // IES 文件解析（IESNA LM-63-2002，仅支持 TILT=NONE）
    // =======================================================================
    bool VansIESProfileManager::ParseIES(const std::string& text, IESProfileData& out)
    {
        std::istringstream ss(text);
        std::string line;

        // --- 1. 跳过头部直到 TILT=
        bool foundTilt = false;
        while (ReadLine(ss, line))
        {
            // 解析 MANUFAC / LUMCAT / LUMINAIRE 等元数据（可选）
            if (line.rfind("[MANUFAC]", 0) == 0)
                out.m_Manufacturer = line.substr(9);
            else if (line.rfind("[LUMCAT]", 0) == 0)
                out.m_CatalogNumber = line.substr(8);
            else if (line.rfind("[LUMINAIRE]", 0) == 0)
                out.m_Description = line.substr(11);
            else if (line.rfind("TILT=", 0) == 0)
            {
                // 只支持 TILT=NONE；有 TILT 数据的文件跳过（极少见）
                if (line.find("NONE") == std::string::npos)
                {
                    VANS_LOG_WARN("[VansIESProfileManager] TILT != NONE，跳过该 IES 文件");
                    return false;
                }
                foundTilt = true;
                break;
            }
        }
        if (!foundTilt)
        {
            VANS_LOG_WARN("[VansIESProfileManager] 未找到 TILT= 行，文件格式异常");
            return false;
        }

        // --- 2. 读取 13 个配置参数（可能跨多行）
        std::vector<float> params = ReadAllFloats(ss, 13);
        if ((int)params.size() < 13)
        {
            VANS_LOG_WARN("[VansIESProfileManager] 配置参数不足 13 个");
            return false;
        }
        // 跳过 params[0]=灯数, params[1]=流明 （这里只用到部分参数）
        out.m_TotalLumens        = params[1];
        out.m_CandelaMultiplier  = params[2];
        out.m_NumVerticalAngles  = static_cast<int>(params[3]);
        out.m_NumHorizontalAngles= static_cast<int>(params[4]);
        out.m_PhotometricType    = static_cast<int>(params[5]);
        out.m_UnitsType          = static_cast<int>(params[6]);
        // params[7..9] = 灯具尺寸（不使用）
        // params[10]   = 镇流器因子（不使用）
        // params[11]   = 保留
        // params[12]   = 输入功率

        int nV = out.m_NumVerticalAngles;
        int nH = out.m_NumHorizontalAngles;

        if (nV <= 0 || nH <= 0)
        {
            VANS_LOG_WARN("[VansIESProfileManager] 垂直/水平角度数量无效");
            return false;
        }

        // --- 3. 跳过可能存在的镇流器因子行（某些文件含 1 个额外数值行）
        // 实际上 params 已经读了 13 个，直接继续读角度数据

        // --- 4. 读取垂直角度列表
        std::vector<float> vAngles = ReadAllFloats(ss, nV);
        if ((int)vAngles.size() < nV)
        {
            VANS_LOG_WARN("[VansIESProfileManager] 垂直角度数据不足");
            return false;
        }
        out.m_VerticalAngles = std::move(vAngles);

        // --- 5. 读取水平角度列表
        std::vector<float> hAngles = ReadAllFloats(ss, nH);
        if ((int)hAngles.size() < nH)
        {
            VANS_LOG_WARN("[VansIESProfileManager] 水平角度数据不足");
            return false;
        }
        out.m_HorizontalAngles = std::move(hAngles);
        out.m_LastHorizontalAngle = out.m_HorizontalAngles.back();

        // --- 6. 读取坎德拉值（nH × nV 个，每行对应一个水平切面）
        int totalValues = nH * nV;
        std::vector<float> candela = ReadAllFloats(ss, totalValues);
        if ((int)candela.size() < totalValues)
        {
            VANS_LOG_WARN("[VansIESProfileManager] 坎德拉值数据不足，期望 " << totalValues
                          << " 个，实际 " << candela.size() << " 个");
            return false;
        }

        // 应用坎德拉乘数并计算最大值
        out.m_CandelaValues.resize(totalValues);
        float maxCd = 0.0f;
        for (int i = 0; i < totalValues; ++i)
        {
            float val = candela[i] * out.m_CandelaMultiplier;
            out.m_CandelaValues[i] = val;
            if (val > maxCd) maxCd = val;
        }
        out.m_MaxCandela = (maxCd > 1e-6f) ? maxCd : 1.0f;

        return true;
    }

    // =======================================================================
    // 对称性处理：将任意水平角折叠到 IES 文件记录的范围内
    // =======================================================================
    float VansIESProfileManager::ApplyHorizontalSymmetry(const IESProfileData& data, float horizDeg) const
    {
        // 规范化到 [0, 360)
        horizDeg = std::fmod(horizDeg, 360.0f);
        if (horizDeg < 0.0f) horizDeg += 360.0f;

        float last = data.m_LastHorizontalAngle;
        if (last <= 0.0f)
        {
            // 完全轴对称：只有一个切面（0°），水平角无意义
            return 0.0f;
        }
        if (last <= 90.0f)
        {
            // 四分之一对称（0°-90°）
            horizDeg = std::fmod(horizDeg, 90.0f);
        }
        else if (last <= 180.0f)
        {
            // 半对称（0°-180°）
            if (horizDeg > 180.0f) horizDeg = 360.0f - horizDeg;
        }
        // 360°：不做折叠

        // 夹到 [0, last]
        horizDeg = std::max(0.0f, std::min(horizDeg, last));
        return horizDeg;
    }

    // =======================================================================
    // 双线性插值采样单个 profile（角度单位：度）
    // =======================================================================
    float VansIESProfileManager::SampleCandela(const IESProfileData& data,
                                                float vertDeg, float horizDeg) const
    {
        const auto& vA = data.m_VerticalAngles;
        const auto& hA = data.m_HorizontalAngles;
        int nV = data.m_NumVerticalAngles;
        int nH = data.m_NumHorizontalAngles;

        // 应用对称性
        horizDeg = ApplyHorizontalSymmetry(data, horizDeg);

        // 找到垂直角度包围索引
        int v0 = 0;
        for (int i = 0; i < nV - 1; ++i)
            if (vA[i + 1] >= vertDeg) { v0 = i; break; }
        v0 = std::min(v0, nV - 2);
        int v1 = v0 + 1;
        float tV = (vA[v1] > vA[v0]) ? (vertDeg - vA[v0]) / (vA[v1] - vA[v0]) : 0.0f;
        tV = std::max(0.0f, std::min(1.0f, tV));

        // 找到水平角度包围索引（轴对称时 nH=1）
        int h0 = 0, h1 = 0;
        float tH = 0.0f;
        if (nH > 1)
        {
            for (int i = 0; i < nH - 1; ++i)
                if (hA[i + 1] >= horizDeg) { h0 = i; break; }
            h0 = std::min(h0, nH - 2);
            h1 = h0 + 1;
            tH = (hA[h1] > hA[h0]) ? (horizDeg - hA[h0]) / (hA[h1] - hA[h0]) : 0.0f;
            tH = std::max(0.0f, std::min(1.0f, tH));
        }

        float c00 = data.m_CandelaValues[h0 * nV + v0];
        float c10 = data.m_CandelaValues[h0 * nV + v1];
        float c01 = (nH > 1) ? data.m_CandelaValues[h1 * nV + v0] : c00;
        float c11 = (nH > 1) ? data.m_CandelaValues[h1 * nV + v1] : c10;

        float c0 = c00 + tV * (c10 - c00);
        float c1 = c01 + tV * (c11 - c01);
        return c0 + tH * (c1 - c0);
    }

    // =======================================================================
    // 烘焙单个 profile 到 FP32 像素数组（R 通道归一化到 [0,1]）
    // =======================================================================
    void VansIESProfileManager::BakeToPixels(int profileIndex, int width, int height,
                                              std::vector<float>& outPixels) const
    {
        const IESProfileData& data = m_Profiles[profileIndex];
        outPixels.resize(width * height, 0.0f);

        const float PI = 3.14159265358979f;

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                // UV → 球面角度（(θ, cosφ) 参数化，与 SampleIESProfile GLSL 一致）
                float u = (x + 0.5f) / static_cast<float>(width);   // [0, 1]
                float v = (y + 0.5f) / static_cast<float>(height);  // [0, 1]

                // v = (cos(phi) * 0.5) + 0.5  →  phi = acos(2*v - 1)
                float phi   = std::acos(std::max(-1.0f, std::min(1.0f, 2.0f * v - 1.0f))); // [0, π], NADIR=0
                // u = theta * INV_TWO_PI + 0.5  →  theta = (u - 0.5) * 2π
                float theta = (u - 0.5f) * (2.0f * PI);  // [-π, π]

                // 球面角 → Type C IES 角度
                float vertDeg  = phi * (180.0f / PI);           // [0°, 180°]
                float horizDeg = theta * (180.0f / PI);         // [-180°, 180°]
                if (horizDeg < 0.0f) horizDeg += 360.0f;       // [0°, 360°)

                float candela = SampleCandela(data, vertDeg, horizDeg);
                outPixels[y * width + x] = candela / data.m_MaxCandela;
            }
        }
    }

    // =======================================================================
    // 公开接口：从文件加载
    // =======================================================================
    bool VansIESProfileManager::LoadIESFile(const std::string& filePath, int& outProfileIndex)
    {
        if ((int)m_Profiles.size() >= kMaxProfiles)
        {
            VANS_LOG_WARN("[VansIESProfileManager] 已达到最大 profile 数量 " << kMaxProfiles);
            return false;
        }

        std::ifstream file(filePath);
        if (!file.is_open())
        {
            VANS_LOG_WARN("[VansIESProfileManager] 无法打开文件: " << filePath);
            return false;
        }

        std::string text((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
        return LoadIESFromMemory(text.c_str(), text.size(), outProfileIndex);
    }

    // =======================================================================
    // 公开接口：从内存加载
    // =======================================================================
    bool VansIESProfileManager::LoadIESFromMemory(const char* data, size_t dataSize,
                                                   int& outProfileIndex)
    {
        if ((int)m_Profiles.size() >= kMaxProfiles)
        {
            VANS_LOG_WARN("[VansIESProfileManager] 已达到最大 profile 数量 " << kMaxProfiles);
            return false;
        }

        std::string text(data, dataSize);
        IESProfileData profile;
        if (!ParseIES(text, profile))
            return false;

        outProfileIndex = static_cast<int>(m_Profiles.size());
        m_Profiles.push_back(std::move(profile));
        VANS_LOG("[VansIESProfileManager] 加载 IES profile [" << outProfileIndex << "] 成功");
        return true;
    }

    // =======================================================================
    // 创建 GPU 纹理数组并上传所有已加载的 profile
    // =======================================================================
    void VansIESProfileManager::CreateGPUResources(VkDevice& logicDevice)
    {
        if (m_Profiles.empty())
        {
            // 没有任何 IES 文件被加载，创建一个 1×1×1 的占位纹理
            // 以确保 Descriptor Set 可以绑定有效资源
            VkExtent3D extent = { 1, 1, 1 };
            m_IESTextureArray.CreateVulkanImage(
                logicDevice,
                extent,
                VK_FORMAT_R16_SFLOAT,
                1,    // mip_num
                1,    // layer_num
                VK_IMAGE_TYPE_2D,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_SAMPLE_COUNT_1_BIT,
                false, // isCube
                false, // need_raw_Data（不使用线性布局）
                true,  // combined_sampler
                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            );
            m_GPUResourcesCreated = true;
            VANS_LOG("[VansIESProfileManager] 无 IES profile，创建占位纹理");
            return;
        }

        int layerCount = static_cast<int>(m_Profiles.size());

        // 创建 sampler2DArray：256×128 × layerCount，格式 R16F，单 mip，clamp 采样
        VkExtent3D extent = { (uint32_t)kBakeWidth, (uint32_t)kBakeHeight, 1 };
        m_IESTextureArray.CreateVulkanImage(
            logicDevice,
            extent,
            VK_FORMAT_R16_SFLOAT,
            1,                    // mip_num
            (uint32_t)layerCount, // layer_num
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_SAMPLE_COUNT_1_BIT,
            false, // isCube
            false, // need_raw_Data
            true,  // combined_sampler
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
        );

        m_GPUResourcesCreated = true;
        VANS_LOG("[VansIESProfileManager] 创建 IES GPU 纹理数组：256×128×" << layerCount);
    }

    // =======================================================================
    // 将所有已加载的 profile 上传到 GPU（需在 CreateGPUResources 之后调用）
    // 流程：
    //   1. 初始 layout 转换 UNDEFINED → SHADER_READ_ONLY_OPTIMAL
    //   2. 逐层 bake→FP16，调用 SetDeviceImageData（内部含 SHADER_READ_ONLY
    //      ↔ TRANSFER_DST 的往返 barrier 及 submit/wait）
    // =======================================================================
    void VansIESProfileManager::UploadAllProfiles(VansVKDevice* device, VansVKCommandBuffer& cmd)
    {
        if (!m_GPUResourcesCreated || !device)
            return;

        VkQueue    queue       = device->GetGraphicsQueue();
        VkDevice   logicDevice = device->GetLogicDevice();

        // ── 步骤 1：将整张纹理数组从 UNDEFINED 转换到 SHADER_READ_ONLY_OPTIMAL ──
        // 与 VansTexture::InitTextureArray 相同模式，确保 SetDeviceImageData 中
        // originalLayout = SHADER_READ_ONLY_OPTIMAL，避免 undefined-layout 往返
        cmd.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        m_IESTextureArray.SetImageMemoryBarrier(
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            {
                m_IESTextureArray.GetImage(),
                VK_ACCESS_NONE,
                VK_ACCESS_SHADER_READ_BIT,
                m_IESTextureArray.GetImageLayout(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                m_IESTextureArray.GetImageAspect()
            });
        cmd.EndCommandBufferRecord();
        VansVKCommandBuffer::SubmitCommands(queue, logicDevice,
            { cmd.GetVKCommandBuffer() }, {}, {}, cmd.m_CommandBufferFinishSubmitFence);
        cmd.ResetCommandBuffer(false);

        if (m_Profiles.empty())
        {
            VANS_LOG("[VansIESProfileManager] 无 IES profile，跳过 GPU 上传");
            return;
        }

        // ── 步骤 2：逐层烘焙并上传 ──
        std::vector<float>    fp32;
        std::vector<uint16_t> fp16;
        const VkOffset3D      zeroOffset = { 0, 0, 0 };
        const VkExtent3D      layerExt   = { (uint32_t)kBakeWidth, (uint32_t)kBakeHeight, 1 };
        const int             pixelCount = kBakeWidth * kBakeHeight;
        const int             byteSize   = pixelCount * static_cast<int>(sizeof(uint16_t));

        for (int i = 0; i < static_cast<int>(m_Profiles.size()); ++i)
        {
            BakeToPixels(i, kBakeWidth, kBakeHeight, fp32);

            fp16.resize(fp32.size());
            for (size_t j = 0; j < fp32.size(); ++j)
                fp16[j] = FloatToHalf(fp32[j]);

            // SetDeviceImageData 内部自行 Begin/Submit/Reset，无需外部 BeginCommandBufferRecord
            device->SetDeviceImageData(
                m_IESTextureArray, cmd,
                fp16.data(), 0, byteSize,
                zeroOffset, layerExt,
                0,  // mip_level
                i   // layer_level
            );

            VANS_LOG("[VansIESProfileManager] 上传 IES profile [" << i << "] 完成");
        }

        VANS_LOG("[VansIESProfileManager] 所有 IES profiles GPU 上传完成，共 " << m_Profiles.size() << " 个");
    }

    // =======================================================================
    // 销毁 GPU 资源
    // =======================================================================
    void VansIESProfileManager::DestroyGPUResources(VkDevice& logicDevice)
    {
        if (m_GPUResourcesCreated)
        {
            m_IESTextureArray.DestroyVulkanImage(logicDevice);
            m_GPUResourcesCreated = false;
        }
    }

    VansIESProfileManager::~VansIESProfileManager()
    {
        // GPU 资源应由 DestroyGPUResources 显式释放，析构时不再调用
    }
}
