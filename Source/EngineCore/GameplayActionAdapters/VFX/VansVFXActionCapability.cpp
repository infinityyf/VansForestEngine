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
            VansActionCommandField("source",V::Object,false,VansSerializedValue::Object({})),
            VansActionCommandField("position",V::Object,false,VansSerializedValue::Object({})),
            VansActionCommandField("maxConcurrentPerSource",V::Int,false,VansSerializedValue::Int(16))}),
        VansActionCommandCapability("VFX.SurfaceField",R::Create,{
            VansActionCommandField("effect",V::String,true),
            VansActionCommandField("position",V::Object,true),
            VansActionCommandField("surfaceLayers",V::Array,true),
            VansActionCommandNumberField("radius",V::Float,true,VansSerializedValue::Float(3),0.01,30),
            VansActionCommandNumberField("spacing",V::Float,false,VansSerializedValue::Float(1.5),0.1,30),
            VansActionCommandNumberField("probeHeight",V::Float,false,VansSerializedValue::Float(2),0.01,30),
            VansActionCommandNumberField("maxDrop",V::Float,false,VansSerializedValue::Float(4),0.01,60),
            VansActionCommandNumberField("minNormalY",V::Float,false,VansSerializedValue::Float(0.25),0,1),
            VansActionCommandNumberField("duration",V::Float,true,VansSerializedValue::Float(5.5),0.01,60),
            VansActionCommandNumberField("spreadDuration",V::Float,true,VansSerializedValue::Float(3),0.01,60),
            VansActionCommandNumberField("spawnInterval",V::Float,false,VansSerializedValue::Float(0.12),0.02,1),
            VansActionCommandNumberField("maxSurfaces",V::Int,false,VansSerializedValue::Int(16),1,16),
            VansActionCommandField("maxConcurrentPerSource",V::Int,false,VansSerializedValue::Int(8))}),
        VansActionCommandCapability("VFX.Spawn",R::Create,{
            VansActionCommandField("effect",V::String,true),
            VansActionCommandField("source",V::Object,false,VansSerializedValue::Object({})),
            VansActionCommandField("position",V::Object,false,VansSerializedValue::Object({})),
            VansActionCommandField("maxConcurrentPerSource",V::Int,false,VansSerializedValue::Int(16))}),
        VansActionCommandCapability("VFX.ExtinguishingVolume",R::Create,{
            VansActionCommandField("effect",V::String,true),
            VansActionCommandField("position",V::Object,true),
            VansActionCommandField("sampleOffsets",V::Array,true),
            VansActionCommandField("extinguishEffect",V::String,true),
            VansActionCommandField("zeroDistanceDirection",V::Object,true),
            VansActionCommandNumberField("extinctionThreshold",V::Float,true,VansSerializedValue::Float(.03),.000001,100),
            VansActionCommandNumberField("minimumCells",V::Int,true,VansSerializedValue::Int(3),1,16),
            VansActionCommandNumberField("fullExtinguishFraction",V::Float,true,VansSerializedValue::Float(1./3),0,1),
            VansActionCommandNumberField("samplePeriod",V::Float,true,VansSerializedValue::Float(.1),.02,1),
            VansActionCommandNumberField("maxConcurrentPerSource",V::Int,false,VansSerializedValue::Int(8),1,8)}),
        // 停止发射只改变播放状态，资源在真正消散后由账本完成协议释放。
        VansActionCommandCapability("VFX.Stop",R::Update,{
            VansActionCommandResourceField(),
            VansActionCommandField("mode",V::String,false,VansSerializedValue::String("Drain"))})
    });
    return capability;
}
}
