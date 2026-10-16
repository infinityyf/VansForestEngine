#pragma once
#include <functional>
#include <memory>
#include <utility>

namespace VansGraphics
{
    // A submitted reader retains native handles, not the mutable asset wrapper.
    // The resource owner retires once; the last completed reader releases storage.
    // No queue, polling, or device wait is introduced for resources without readers.
    // The Vulkan device and allocator must outlive the owner and every submitted reader.
    class VansVKResourceLifetime
    {
    public:
        using Lease=std::shared_ptr<const VansVKResourceLifetime>;
        static Lease Retain(std::shared_ptr<VansVKResourceLifetime>& owner)
        {
            if(!owner)owner=std::make_shared<VansVKResourceLifetime>();
            return owner;
        }
        template<class Dispose>
        static void Retire(std::shared_ptr<VansVKResourceLifetime>& owner,Dispose&& dispose)
        {
            if(!owner || owner.use_count()==1){owner.reset();dispose();return;}
            owner->m_Dispose=std::forward<Dispose>(dispose);
            owner.reset();
        }
        ~VansVKResourceLifetime(){if(m_Dispose)m_Dispose();}
    private:
        std::function<void()> m_Dispose;
    };
}
