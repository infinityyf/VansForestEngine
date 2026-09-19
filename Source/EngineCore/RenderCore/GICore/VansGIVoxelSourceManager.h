#pragma once

#include "VansGIVoxelSource.h"

#include <functional>
#include <cstdint>
#include <utility>
#include <vector>

namespace VansGraphics
{
    // PCG、地形和其他来源只依赖这个窄接口；具体 DDGI/体素世界由绑定端实现。
    class VansGIVoxelSourceManager final
    {
    public:
        using Submitter = std::function<bool(GIVoxelSourceChanges)>;
        using HeightPatchSubmitter = std::function<bool(uint32_t,uint32_t,uint32_t,uint32_t,const std::vector<uint8_t>&)>;
        using ColorPatchSubmitter = std::function<bool(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,const std::vector<uint8_t>&)>;

        void Bind(Submitter submitter) { m_Submitter=std::move(submitter); }
        void BindTerrainPatchers(HeightPatchSubmitter height, ColorPatchSubmitter color)
        { m_HeightSubmitter=std::move(height);m_ColorSubmitter=std::move(color); }
        void Clear() { m_Submitter={};m_HeightSubmitter={};m_ColorSubmitter={}; }
        bool IsBound() const { return bool(m_Submitter); }

        bool Submit(GIVoxelSourceChanges changes) const
        {
            if (changes.Empty()) return true;
            return m_Submitter ? m_Submitter(std::move(changes)) : false;
        }

        bool SubmitHeightPatch(uint32_t x,uint32_t z,uint32_t w,uint32_t h,const std::vector<uint8_t>& bytes) const
        { return !m_HeightSubmitter || m_HeightSubmitter(x,z,w,h,bytes); }
        bool SubmitColorPatch(uint32_t map,uint32_t x,uint32_t y,uint32_t w,uint32_t h,const std::vector<uint8_t>& bytes) const
        { return !m_ColorSubmitter || m_ColorSubmitter(map,x,y,w,h,bytes); }

    private:
        Submitter m_Submitter;
        HeightPatchSubmitter m_HeightSubmitter;
        ColorPatchSubmitter m_ColorSubmitter;
    };
}
