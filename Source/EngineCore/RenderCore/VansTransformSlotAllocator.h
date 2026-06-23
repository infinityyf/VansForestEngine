#pragma once
#include <queue>
#include <unordered_set>
#include <cstdint>
#include <cassert>

namespace VansGraphics
{
    // ════════════════════════════════════════════════════════════════════════════
    //  TransformSlotAllocator
    //
    //  管理 m_InstanceTransformDataBuffer SSBO 内槽位的分配与回收。
    //
    //  使用方式:
    //    - AllocateSlot()  创建实体时分配槽位, 返回 INVALID_SLOT 表示容量耗尽
    //    - FreeSlot(idx)   销毁实体时回收槽位 (放入 free-list 供后续复用)
    //    - GetActiveCount() 监控使用率, 触发扩容逻辑
    //
    //  线程安全: 非线程安全, 调用方必须在主线程(或持有场景锁的线程)操作
    // ════════════════════════════════════════════════════════════════════════════
    class TransformSlotAllocator
    {
    public:
        static constexpr uint32_t INVALID_SLOT = UINT32_MAX;

        explicit TransformSlotAllocator(uint32_t maxCapacity = 4096)
            : m_MaxCapacity(maxCapacity)
        {
        }

        // ── 分配一个空闲槽位 ──────────────────────────────────────────────────
        // 返回槽位索引; 容量耗尽时返回 INVALID_SLOT
        uint32_t AllocateSlot()
        {
            if (!m_FreeSlots.empty())
            {
                uint32_t slot = m_FreeSlots.front();
                m_FreeSlots.pop();
                m_ActiveSlots.insert(slot);
                return slot;
            }
            if (m_NextIndex < m_MaxCapacity)
            {
                uint32_t slot = m_NextIndex++;
                m_ActiveSlots.insert(slot);
                return slot;
            }
            // 容量耗尽
            return INVALID_SLOT;
        }

        // ── 回收一个槽位 ──────────────────────────────────────────────────────
        void FreeSlot(uint32_t slot)
        {
            if (slot >= m_MaxCapacity) return;
            if (m_ActiveSlots.erase(slot) > 0)
            {
                m_FreeSlots.push(slot);
            }
        }

        // ── 查询 ──────────────────────────────────────────────────────────────
        bool   IsActive(uint32_t slot) const     { return m_ActiveSlots.count(slot) > 0; }
        size_t GetActiveCount() const             { return m_ActiveSlots.size(); }
        uint32_t GetMaxCapacity() const           { return m_MaxCapacity; }
        float  GetUsageRatio() const              { return (float)GetActiveCount() / (float)m_MaxCapacity; }

        // ── 扩容 ──────────────────────────────────────────────────────────────
        // 注意: 扩容需要重建 GPU buffer 并 re-write descriptor set,
        //       不在此类中处理, 由 VansScene::GrowTransformBuffer() 负责
        void SetMaxCapacity(uint32_t newMax)
        {
            assert(newMax >= m_NextIndex);
            m_MaxCapacity = newMax;
        }

        // ── 序列化/反序列化 (场景保存/恢复) ───────────────────────────────────
        const std::unordered_set<uint32_t>& GetActiveSlots() const { return m_ActiveSlots; }

        // 重置所有状态 (在 FullSceneReload 时调用)
        void Reset()
        {
            m_ActiveSlots.clear();
            while (!m_FreeSlots.empty()) m_FreeSlots.pop();
            m_NextIndex = 0;
        }

    private:
        uint32_t                     m_MaxCapacity = 4096;
        uint32_t                     m_NextIndex   = 0;
        std::queue<uint32_t>         m_FreeSlots;
        std::unordered_set<uint32_t> m_ActiveSlots;
    };

} // namespace VansGraphics
