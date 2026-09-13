#include "VansVFXActionCapability.h"
#include "../VansActionServiceAdapter.h"
namespace Vans
{
const VansActionServiceCapability& VansVFXActionCapability()
{
    using V = VansActionCommandValueKind;
    using R = VansActionCommandResourcePolicy;
    static const auto capability = VansActionServiceCapabilityDescriptor("Service.VFX", {
        VansActionCommandCapability("VFX.Pulse",R::None,{
            VansActionCommandField("effect",V::String,true),
            VansActionCommandField("source",V::Object,true)}),
        VansActionCommandCapability("VFX.Spawn",R::Create,{
            VansActionCommandField("effect",V::String,true),
            VansActionCommandField("source",V::Object,true),
            VansActionCommandField("maxConcurrentPerSource",V::Int,false,VansSerializedValue::Int(16))}),
        // 停止发射只改变播放状态，资源在真正消散后由账本完成协议释放。
        VansActionCommandCapability("VFX.Stop",R::Update,{
            VansActionCommandResourceField(),
            VansActionCommandField("mode",V::String,false,VansSerializedValue::String("Drain"))})
    });
    return capability;
}
}
