#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "TerrainInteractionContractTests.h"
#include "../EngineCore/AssetCore/Storage/VansRenderPassAssetStorage.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/RenderCore/VulkanCore/VansMesh.h"
#include "../EngineCore/RenderCore/VulkanCore/VansTexture.h"
#include <glm/gtc/packing.hpp>
#include "../EngineCore/RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../EngineCore/RenderCore/VulkanCore/VansDescriptorSetLayouts.h"
#include "../EngineCore/RenderCore/VulkanCore/VansProjectRenderPassRuntime.h"
#include "../EngineCore/RenderCore/VulkanCore/VansProjectPassShaderContract.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/RenderCore/VansShaderManager.h"
#include "../EngineCore/RenderCore/SceneBuild/VansProjectRenderPassProjector.h"
#include "../EngineCore/RenderCore/VansRenderThreadTransaction.h"
#include "../EngineCore/RuntimeCore/VansThreadContract.h"
#include "../EngineCore/RuntimeCore/VansFramePhase.h"
#include "../EngineCore/Interfaces/INativeWindowProvider.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <spirv_cross/spirv_cross.hpp>

namespace
{
using namespace VansGraphics;
std::uint32_t validationErrors=0;
void Check(bool valid,const std::string& message)
{ if (!valid) throw std::runtime_error(message); }
VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* message,void*)
{
    if (severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    { ++validationErrors; std::cerr<<"[ProjectPassGPU validation] "<<message->pMessage<<'\n'; }
    return VK_FALSE;
}
struct Window final:INativeWindowProvider
{
    GLFWwindow* handle=nullptr;
    void* GetNativeWindowHandle()const override{return handle;}
    ~Window(){if(handle)glfwDestroyWindow(handle);glfwTerminate();}
};
struct Command
{
    VansVKDevice& device;VansVKCommandBuffer value;
    Command(VansVKDevice& backend,std::uint32_t family):device(backend)
    {
        CommandBufferCreateParams params{};
        params.pool_params=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        params.commandbuffer_level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;params.commandbuffer_count=1;
        Check(value.CreateVulkanCommandBuffer(device,family,params),"command buffer allocation");
        VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        Check(vkCreateFence(device.GetLogicDevice(),&info,nullptr,&value.m_CommandBufferFinishSubmitFence)==VK_SUCCESS,"fence allocation");
    }
    ~Command()
    {
        device.WaitForDevice();
        if(value.m_CommandBufferFinishSubmitFence)vkDestroyFence(device.GetLogicDevice(),value.m_CommandBufferFinishSubmitFence,nullptr);
        value.DestroyVulkanCommandBuffer(device.GetLogicDevice());
    }
};
struct Submission
{
    VansQueueTimelines timelines;VansCrossFrameResourceLedger ledger;
    std::array<VansFrameSubmitOrchestrator,2> prelude,consumer;
    ~Submission(){for(auto& graph:consumer)graph.Shutdown();for(auto& graph:prelude)graph.Shutdown();timelines.Shutdown();}
};
Vans::VansSceneResourceBuildPlan Resources()
{
    using namespace Vans;
    VansSceneResourceBuildPlan result;
    VansRenderPassAsset pass;pass.passId="configured-draw";pass.source=VansRenderPassSource::ProjectInstances;
    pass.instanceStream="objects";pass.instanceMesh=VansAssetGuid::FromStableName("ProjectPassGPU","mesh");
    pass.shaders={VansAssetGuid::FromStableName("ProjectPassGPU","shader")};pass.previousFrameHistory=true;
    VansRenderTargetAsset target;target.name="output";target.width=target.height=32;
    target.lifetime=VansRenderTargetLifetime::Persistent;target.load=VansRenderTargetLoad::CopyPrevious;
    target.usage=RenderTargetColorAttachment|RenderTargetSampled|RenderTargetTransferSource|RenderTargetTransferDestination;
    target.clear={0,0,0,0};pass.targets={target};pass.buffers={{"objects","instanceInput","projectDrawInstance",1,96,4}};
    pass.targets[0].exportChannel="validation.output";pass.targets[0].exportSlot=0;
    pass.parameters={{"tint",0,{1,1,1,1}}};
    result.projectRenderPasses.push_back({VansAssetGuid::FromStableName("ProjectPassGPU","pass"),pass});
    VansSceneShaderResourceRequest shader;shader.name="ProjectPassGPU";shader.assetGuid=pass.shaders[0].ToString();shader.pushConstantSize=0;
    result.shaders.push_back(shader);
    VansSceneMeshResourceRequest mesh;mesh.name="ProjectPassGPU.mesh";mesh.assetGuid=pass.instanceMesh.ToString();result.meshes.push_back(mesh);
    return result;
}
using StageSpirv = std::map<VkShaderStageFlagBits,std::vector<std::uint32_t>>;
StageSpirv CaptureStageSpirv(const VansGraphicsShader& shader)
{
    StageSpirv stages;
    for(const auto& [stage,module]:shader.m_ShaderModuleDataMap)
    {
        auto& words=stages[stage];words.resize(module.m_ShaderSPIRVCode.size()/sizeof(std::uint32_t));
        std::memcpy(words.data(),module.m_ShaderSPIRVCode.data(),module.m_ShaderSPIRVCode.size());
    }
    return stages;
}
StageSpirv SwizzledOutput(StageSpirv stages)
{
    auto& words=stages.at(VK_SHADER_STAGE_FRAGMENT_BIT);
    spirv_cross::Compiler compiler(words);
    const auto outputs=compiler.get_shader_resources().stage_outputs;
    Check(outputs.size()==1&&compiler.get_type(outputs[0].base_type_id).vecsize==4,"reload fixture output shape");
    // 只交换输出 R/B，保持真实程序的全部接口；候选通过正式热重载入口创建 Vulkan 模块。
    for(std::size_t offset=5;offset<words.size();)
    {
        const auto count=words[offset]>>16,opcode=words[offset]&0xffffu;
        Check(count&&offset+count<=words.size(),"reload fixture SPIR-V instruction");
        if(opcode==spv::OpStore&&count==3&&words[offset+1]==outputs[0].id)
        {
            const auto value=words[offset+2],result=words[3]++;
            words[offset+2]=result;
            const std::array<std::uint32_t,9> shuffle{{(9u<<16)|spv::OpVectorShuffle,outputs[0].base_type_id,result,value,value,2,1,0,3}};
            words.insert(words.begin()+offset,shuffle.begin(),shuffle.end());
            return stages;
        }
        offset+=count;
    }
    Check(false,"reload fixture output store missing");return {};
}
void Verify(VansVKDevice& device,VkQueue computeQueue,std::uint32_t slots,bool async,bool overlap=false,bool scrolling=false)
{
    auto resources=Resources();
    if(scrolling)
    {
        auto& definition=resources.projectRenderPasses[0].definition;
        definition.cameraWorldRect="worldRect";
        definition.parameters.push_back({"worldRect",16,{-16,-16,32,32}});
    }
    auto* shader=VansShaderManager::Get().FindGraphicsShader("ProjectPassGPU");
    Check(shader!=nullptr,"reload fixture shader missing");
    const auto originalStages=CaptureStageSpirv(*shader),reloadedStages=SwizzledOutput(originalStages);
    const auto originalHash=shader->GetShaderBinaryHash();
    std::uint64_t reloadedHash=0;
    VansProjectRenderPassState state;std::string error;
    VANS_INIT_MAIN_THREAD();Check(state.Configure(resources,error),error);
    VansMesh mesh;mesh.SetName("ProjectPassGPU.mesh");
    const std::array<glm::vec2,4> vertices{{{-.5f,-.5f},{.5f,-.5f},{.5f,.5f},{-.5f,.5f}}};
    const std::array<std::uint32_t,6> indices{{0,1,2,2,3,0}};
    mesh.InitFromRawData(device.GetLogicDevice(),vertices.data(),4,sizeof(glm::vec2),indices.data(),6,
        {{0,sizeof(glm::vec2),VK_VERTEX_INPUT_RATE_VERTEX}},{{0,0,VK_FORMAT_R32G32_SFLOAT,0}});
    VansScene scene;scene.AddMeshAsset(&mesh);
    auto& runtime=device.ProjectRenderPassRuntime();
    struct Cleanup{VansVKDevice& device;~Cleanup(){device.WaitForDevice();device.ProjectRenderPassRuntime().ShutdownAtIdle();}} cleanup{device};
    VANS_INIT_RENDER_THREAD();
    Submission submission;
    Check(submission.timelines.Initialize(device.GetLogicDevice(),device.GetGraphicsQueue(),computeQueue),"queue timeline allocation");
    std::array<std::unique_ptr<Command>,2> producers,readers;
    for(std::uint32_t i=0;i<slots;++i)
    {
        for(auto* graph:{&submission.prelude[i],&submission.consumer[i]})
        {graph->Bind(device.GetLogicDevice(),device.GetGraphicsQueue(),computeQueue);graph->SetQueueTimelines(&submission.timelines);graph->SetCrossFrameResourceLedger(&submission.ledger);}
        producers[i]=std::make_unique<Command>(device,device.GetGraphicsQueueFamilyIndex());
        readers[i]=std::make_unique<Command>(device,async?device.GetComputeQueueFamilyIndex():device.GetGraphicsQueueFamilyIndex());
    }
    VansVKBuffer readback;
    struct ReadbackCleanup{VansVKDevice& device;VansVKBuffer& buffer;~ReadbackCleanup(){device.WaitForDevice();buffer.DestroyVulkanBuffer(device.GetLogicDevice());}} readbackCleanup{device,readback};
    const std::uint32_t imageBytes=32*32*4;
    Check(readback.CreatVulkanBuffer(device.GetLogicDevice(),imageBytes*2*slots,VK_FORMAT_UNDEFINED,VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&readback.PersistentMap(),"readback allocation");
    std::string key;VANS_INIT_MAIN_THREAD();Check(state.GetOutputKey("configured-draw","output",key),"output identity");
    std::uint64_t generation=0;
    std::array<std::uint32_t,2> pendingFrame{{UINT32_MAX,UINT32_MAX}};
    std::array<std::vector<std::uint8_t>,2> expectedReadback;
    std::vector<std::uint8_t> worldPixels(imageBytes,0);
    std::array<std::int64_t,2> lastOrigin{-16,-16};
    const std::array<glm::vec3,15> cameraPath{{{0,0,0},{.49f,99,.49f},{7,0,5},{1.1f,0,2.1f},
        {-.1f,0,-.1f},{4.25f,0,-3.25f},{4.49f,0,-3.1f},{4.1f,0,-3.9f},{0,0,0},
        {32,0,32},{0,0,0},{0,0,0},{17,0,0},{-5,0,0},{0,0,0}}};
    const auto retire=[&](std::uint32_t slot) {
        if(pendingFrame[slot]==UINT32_MAX)return;
        Check(submission.consumer[slot].WaitForCompletion(),"consumer retirement");
        Check(submission.prelude[slot].WaitForCompletion(),"prelude retirement");
        Check(vkResetFences(device.GetLogicDevice(),1,&producers[slot]->value.m_CommandBufferFinishSubmitFence)==VK_SUCCESS&&
            vkResetFences(device.GetLogicDevice(),1,&readers[slot]->value.m_CommandBufferFinishSubmitFence)==VK_SUCCESS,"fence reset");
        const auto frame=pendingFrame[slot];
        readback.InvalidateMappedRange(0,imageBytes*2*slots);
        const auto* bytes=static_cast<const std::uint8_t*>(readback.GetMappedPtr())+imageBytes*2*slot;
        if(scrolling)
        {
            Check(std::equal(expectedReadback[slot].begin(),expectedReadback[slot].end(),bytes),
                "camera scroll/current/previous pixels differ at frame "+std::to_string(frame));
            pendingFrame[slot]=UINT32_MAX; return;
        }
        const auto center=(16*32+16)*4;
        const bool red=frame<3||frame==4;
        Check(bytes[center]==(red?255:0)&&bytes[center+1]==0&&bytes[center+2]==(red?0:255)&&bytes[center+3]==255,"current draw/history/hot-reload content differs");
        if(frame){const auto offset=imageBytes+center;const bool previousRed=frame<=3||frame==5;Check(bytes[offset]==(previousRed?255:0)&&bytes[offset+1]==0&&bytes[offset+2]==(previousRed?0:255)&&bytes[offset+3]==255,"previous-frame pixels were overwritten by this frame");}
        Check(bytes[0]==0&&bytes[1]==0&&bytes[2]==0&&bytes[3]==0,"independent target clear changed");
        pendingFrame[slot]=UINT32_MAX;
    };
    for(std::uint32_t frame=0;frame<(scrolling?cameraPath.size():6);++frame)
    {
        VANS_INIT_RENDER_THREAD();
        const auto slot=frame%slots;
        retire(slot);
        if(frame==4||frame==5)
        {
            for(std::uint32_t pending=0;pending<slots;++pending)retire(pending);
            Check(device.WaitForDevice(),"idle before Shader maintenance");
            Check(VansShaderManager::Get().ApplyCompiledShaderCandidate("ProjectPassGPU",frame==4?reloadedStages:originalStages,error),error);
            if(frame==4){reloadedHash=shader->GetShaderBinaryHash();Check(reloadedHash!=originalHash,"Shader hot reload retained its binary identity");}
            else Check(shader->GetShaderBinaryHash()!=reloadedHash&&CaptureStageSpirv(*shader)==originalStages,"Shader restore did not restore its modules");
        }
        auto& producer=*producers[slot];auto& reader=*readers[slot];
        auto& prelude=submission.prelude[slot];auto& consumerGraph=submission.consumer[slot];
        VANS_INIT_MAIN_THREAD();
        if (frame==2) Check(state.SetEnabled("configured-draw",false,error),error);
        if (frame==3) Check(state.SetEnabled("configured-draw",true,error),error);
        if(scrolling ? (frame==0||frame==11) : (frame==0||frame>=3))
        {VansProjectDrawInstance instance;instance.color=scrolling||frame==0 ? glm::vec4(1,0,0,1) : glm::vec4(0,0,1,1);Check(state.SubmitInstances("configured-draw",{instance},error),error);}
        const auto input=state.CaptureFrame(scrolling?cameraPath[frame]:glm::vec3{});
        VANS_INIT_RENDER_THREAD();VANS_SET_FRAME_PHASE(VansFramePhase::GPURecord);
        Check(producer.value.ResetCommandBuffer(false),"producer reset");
        if(scrolling&&frame==6)
        {
            auto abandoned=input; abandoned.passes[0].worldTexelOrigin={50,50};
            Check(runtime.Record(device,scene,abandoned,producer.value,frame%slots,slots,{32,32},1,error),error);
            runtime.ResolveSubmission(false);
            Check(producer.value.ResetCommandBuffer(false),"abandoned scroll reset");
        }
        Check(runtime.Record(device,scene,input,producer.value,frame%slots,slots,{32,32},1,error),error);
        auto snapshot=runtime.CaptureSnapshot();
        if (frame==2)
        {
            Check(!runtime.HasRecordedWork() && snapshot.targets[0].currentSlot!=snapshot.targets[0].previousSlot,
                "disabled frame must retain distinct produced history without recording work");
            Check(snapshot.passes.size()==1 && !snapshot.passes[0].enabled && !snapshot.passes[0].submitted &&
                snapshot.passes[0].instances==0 && snapshot.passes[0].uploadedBytes==0,"disabled diagnostics retain stale submitted work");
            continue;
        }
        if(frame==0)generation=snapshot.generation;
        if(scrolling)
        {
            const auto previousPixels=worldPixels;
            std::fill(worldPixels.begin(),worldPixels.end(),0);
            const auto origin=input.passes[0].worldTexelOrigin;
            // 独立按世界格点查询上一张图；逐字节验证整数移动、清边和移出后的丢弃。
            for(int y=0;y<32;++y)for(int x=0;x<32;++x)
            {
                const auto sx=x+origin[0]-lastOrigin[0],sy=y+origin[1]-lastOrigin[1];
                if(sx>=0&&sx<32&&sy>=0&&sy<32)
                    std::copy_n(previousPixels.data()+(sy*32+sx)*4,4,worldPixels.data()+(y*32+x)*4);
            }
            if(input.passes[0].instanceCount)for(int y=8;y<24;++y)for(int x=8;x<24;++x)
            { const auto pixel=(y*32+x)*4; worldPixels[pixel]=worldPixels[pixel+3]=255; }
            expectedReadback[slot]=worldPixels;
            if(frame)expectedReadback[slot].insert(expectedReadback[slot].end(),previousPixels.begin(),previousPixels.end());
            lastOrigin=origin;
        }
        Check(snapshot.generation==generation&&runtime.HasRecordedWork(),"generation changed during steady frames");
        prelude.Reset();
        VansFrameSubmitNode node;node.name="BeforeBuiltInRender";node.commandBuffers={producer.value.GetVKCommandBuffer()};
        node.resources=runtime.RecordedResources();node.fence=producer.value.m_CommandBufferFinishSubmitFence;
        prelude.AddNode(std::move(node));Check(prelude.Execute(),prelude.GetLastError());runtime.ResolveSubmission(true);
        const auto point=prelude.GetSubmittedTimelinePoint("BeforeBuiltInRender");Check(bool(point),"prelude completion point missing");
        const auto published=runtime.CaptureSnapshot();
        Check(published.passes.size()==1 && published.passes[0].submitted && published.passes[0].commandSlot==slot &&
            published.passes[0].targetSlot==published.targets[0].currentSlot &&
            (published.passes[0].shaders.empty()==(input.passes[0].instanceCount==0)) &&
            published.passes[0].uploadedBytes>=16 && published.passes[0].imageBarriers>0 && published.passes[0].payloadBytes==published.allocatedBytes &&
            published.passes[0].instances==input.passes[0].instanceCount,"submitted audit does not match actual frame resources/draws");
        Check(published.targets.size()==1 && published.targets[0].frameSlots==2,"history requires two physical targets in both frame modes");
        auto* output=runtime.ResolveTarget(key,generation,published.targets[0].currentSlot);Check(output!=nullptr,"submitted output unavailable");
        auto* previous=frame ? runtime.ResolveTarget(key,generation,published.targets[0].previousSlot) : nullptr;
        Check(!frame || (previous && previous->GetImage()!=output->GetImage()),"previous-frame history aliases the current output");
        Check(reader.value.ResetCommandBuffer(false)&&reader.value.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"reader begin");
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        reader.value.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{barrier});
        VkBufferImageCopy copy{};copy.bufferOffset=imageBytes*2*slot;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={32,32,1};
        vkCmdCopyImageToBuffer(reader.value.GetVKCommandBuffer(),output->GetImage(),VK_IMAGE_LAYOUT_GENERAL,readback.GetNativeBuffer(),1,&copy);
        if(previous)
        {
            copy.bufferOffset+=imageBytes;
            vkCmdCopyImageToBuffer(reader.value.GetVKCommandBuffer(),previous->GetImage(),VK_IMAGE_LAYOUT_GENERAL,readback.GetNativeBuffer(),1,&copy);
        }
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        reader.value.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Check(reader.value.EndCommandBufferRecord(),"reader end");
        consumerGraph.Reset();VansFrameSubmitNode consumer;
        consumer.name="BuiltInConsumer";consumer.queue=async?VansQueueRole::Compute:VansQueueRole::Graphics;
        consumer.commandBuffers={reader.value.GetVKCommandBuffer()};consumer.fence=reader.value.m_CommandBufferFinishSubmitFence;
        VansSubmitResourceAccess access;access.name=key+"/slot"+std::to_string(published.targets[0].currentSlot);access.image=true;access.persistent=true;
        access.stages=VK_PIPELINE_STAGE_TRANSFER_BIT;access.access=VK_ACCESS_TRANSFER_READ_BIT;access.layout=VK_IMAGE_LAYOUT_GENERAL;
        access.physical=VansPhysicalResourceIdentity::ForImage(output->GetImage());consumer.resources={access};
        if(previous){access.name=key+"/slot"+std::to_string(published.targets[0].previousSlot);access.physical=VansPhysicalResourceIdentity::ForImage(previous->GetImage());consumer.resources.push_back(access);}
        consumerGraph.AddNode(std::move(consumer));consumerGraph.AddExternalPrerequisite(*point);
        Check(consumerGraph.Execute(),consumerGraph.GetLastError());pendingFrame[slot]=frame;
        if(!overlap)retire(slot);
        if(overlap&&frame==1)Check(pendingFrame[0]!=UINT32_MAX&&pendingFrame[1]!=UINT32_MAX,
            "two frame submissions must remain unretired simultaneously");
        device.RefreshRuntimeRenderTextureCatalog();const auto catalog=device.QueryRuntimeRenderTextures();
        Check(catalog.size()==1&&catalog[0].key==key&&catalog[0].initialized,"project output did not enter common discovery catalog");
        Check(device.ResolveRuntimeRenderTexture(key,catalog[0].generation,false)==output &&
            (!frame || device.ResolveRuntimeRenderTexture(key,catalog[0].generation,true)==previous),"preview catalog selected the wrong history image");
    }
    for(std::uint32_t slot=0;slot<slots;++slot)retire(slot);
    auto& producer=*producers[0];
    VANS_INIT_MAIN_THREAD();Check(state.SetEnabled("configured-draw",false,error),error);auto disabled=state.CaptureFrame();
    VANS_INIT_RENDER_THREAD();VANS_SET_FRAME_PHASE(VansFramePhase::GPURecord);
    Check(runtime.Record(device,scene,disabled,producer.value,0,slots,{32,32},1,error)&&!runtime.HasRecordedWork(),"disabled pass records GPU work");
    const auto retained=runtime.CaptureSnapshot();
    auto invalid=std::make_shared<VansProjectRenderPassConfiguration>(*disabled.configuration);
    invalid->passes[0].definition.budgetBytes=1;disabled.configuration=invalid;
    Check(!runtime.Record(device,scene,disabled,producer.value,0,slots,{32,32},1,error)&&!error.empty(),"over-budget generation was accepted");
    runtime.ResolveSubmission(false);
    Check(runtime.CaptureSnapshot().generation==retained.generation&&!runtime.CaptureSnapshot().error.empty(),"failed candidate replaced generation or hid diagnostic");
    Check(runtime.CaptureSnapshot().passes[0].uploadedBytes==retained.passes[0].uploadedBytes &&
        runtime.CaptureSnapshot().passes[0].submitted==retained.passes[0].submitted,"failed candidate replaced committed audit");
    invalid=std::make_shared<VansProjectRenderPassConfiguration>(*state.Configuration());
    invalid->passes[0].meshName="missing-geometry";disabled.configuration=invalid;disabled.passes[0].enabled=true;
    disabled.passes[0].instanceCount=1;disabled.passes[0].buffers[0].resize(sizeof(VansProjectDrawInstance));
    Check(!runtime.Record(device,scene,disabled,producer.value,0,slots,{32,32},1,error) &&
        runtime.CaptureSnapshot().generation==retained.generation && runtime.ResolveTarget(key,generation,retained.targets[0].currentSlot),
        "geometry/Pipeline candidate failure must preserve the previous GPU generation and output");
    VANS_INIT_MAIN_THREAD();state.ResetForScene();const auto reloaded=state.CaptureFrame();
    VANS_INIT_RENDER_THREAD();VANS_SET_FRAME_PHASE(VansFramePhase::GPURecord);
    Check(producer.value.ResetCommandBuffer(false)&&runtime.Record(device,scene,reloaded,producer.value,0,slots,{32,32},2,error),error);
    auto& reloadPrelude=submission.prelude[0];reloadPrelude.Reset();
    VansFrameSubmitNode reloadNode;reloadNode.name="BeforeBuiltInRender";reloadNode.commandBuffers={producer.value.GetVKCommandBuffer()};
    reloadNode.resources=runtime.RecordedResources();reloadNode.fence=producer.value.m_CommandBufferFinishSubmitFence;
    reloadPrelude.AddNode(std::move(reloadNode));Check(reloadPrelude.Execute(),reloadPrelude.GetLastError());runtime.ResolveSubmission(true);
    const auto reset=runtime.CaptureSnapshot();
    Check(reset.generation>generation&&reset.draws==0&&reset.targets[0].previousSlot==UINT32_MAX&&!runtime.ResolveTarget(key,generation,0),
        "scene reload retains old generation, draws or history");
    auto* cleared=runtime.ResolveTarget(key,reset.generation,reset.targets[0].currentSlot);Check(cleared,"scene reload output missing");
    auto& reloadReader=readers[0]->value;
    Check(reloadReader.ResetCommandBuffer(false)&&reloadReader.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"reload readback begin");
    VkMemoryBarrier resetBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};resetBarrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;resetBarrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    reloadReader.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{resetBarrier});
    VkBufferImageCopy resetCopy{};resetCopy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};resetCopy.imageExtent={32,32,1};
    vkCmdCopyImageToBuffer(reloadReader.GetVKCommandBuffer(),cleared->GetImage(),VK_IMAGE_LAYOUT_GENERAL,readback.GetNativeBuffer(),1,&resetCopy);
    resetBarrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;resetBarrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    reloadReader.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{resetBarrier});Check(reloadReader.EndCommandBufferRecord(),"reload readback end");
    auto& reloadConsumer=submission.consumer[0];reloadConsumer.Reset();
    VansFrameSubmitNode reloadRead;reloadRead.name="ReloadReadback";reloadRead.queue=async?VansQueueRole::Compute:VansQueueRole::Graphics;
    reloadRead.commandBuffers={reloadReader.GetVKCommandBuffer()};reloadRead.fence=reloadReader.m_CommandBufferFinishSubmitFence;
    VansSubmitResourceAccess resetRead;resetRead.name=key+"/slot"+std::to_string(reset.targets[0].currentSlot);resetRead.image=true;resetRead.persistent=true;
    resetRead.stages=VK_PIPELINE_STAGE_TRANSFER_BIT;resetRead.access=VK_ACCESS_TRANSFER_READ_BIT;resetRead.layout=VK_IMAGE_LAYOUT_GENERAL;
    resetRead.physical=VansPhysicalResourceIdentity::ForImage(cleared->GetImage());reloadRead.resources={resetRead};reloadConsumer.AddNode(std::move(reloadRead));
    reloadConsumer.AddExternalPrerequisite(*reloadPrelude.GetSubmittedTimelinePoint("BeforeBuiltInRender"));
    Check(reloadConsumer.Execute()&&reloadConsumer.WaitForCompletion(),reloadConsumer.GetLastError());Check(reloadPrelude.WaitForCompletion(),"reload producer completion");
    readback.InvalidateMappedRange(0,imageBytes);
    const auto* resetPixels=static_cast<const std::uint8_t*>(readback.GetMappedPtr());
    Check(std::all_of(resetPixels,resetPixels+imageBytes,[](auto value){return value==0;}),"scene reload RT contains pixels from the retired generation");
    Check(device.WaitForDevice(),"idle before release");runtime.ShutdownAtIdle();
    Check(!runtime.ResolveTarget(key,generation,0),"retired output identity still resolves");
    std::cout<<"[ProjectPassGPU] slots="<<slots<<" async="<<async<<" overlap="<<overlap<<" scrolling="<<scrolling<<" draw/history/disable/catalog/rollback/scene-reload-clear/shader-hot-reload PASS\n";
}
void VerifySampledImageContract(const VansGraphicsShader& shader, const Vans::VansRenderPassAsset& definition)
{
    auto stages=CaptureStageSpirv(shader);
    auto& fragment=stages.at(VK_SHADER_STAGE_FRAGMENT_BIT);
    bool rebound=false, resized=false;
    std::size_t imageOffset=0;
    std::uint32_t integerType=0;
    for (std::size_t offset=5;offset<fragment.size();)
    {
        const auto count=fragment[offset]>>16, opcode=fragment[offset]&0xffffu;
        Check(count && offset+count<=fragment.size(),"sampled contract SPIR-V instruction");
        if (opcode==spv::OpDecorate && count==4 && fragment[offset+2]==spv::DecorationBinding && fragment[offset+3]==GLOBAL_BINDING_PROJECT_RENDER_TEXTURES)
        { fragment[offset+3]=GLOBAL_BINDING_BRDF_LUT; rebound=true; }
        if (opcode==spv::OpConstant && count==4 && fragment[offset+3]==16)
        { integerType=fragment[offset+1]; fragment[offset+3]=1; resized=true; }
        if (opcode==spv::OpTypeImage) imageOffset=offset;
        offset+=count;
    }
    Check(rebound && resized && imageOffset && integerType,"sampled contract fixture shape");
    VansProjectPassShaderSignature signature;
    std::string error;
    Check(ValidateProjectPassShaderSpirv(definition,stages,16,signature,error),"valid early sampler2D rejected: "+error);
    const auto retainedBindings=signature.globalBindings;
    // 错误接口只用于反射拒绝测试；实际 GPU 消费原编译器产出的 SPIR-V。
    for (const auto& [operand,value] : std::array<std::pair<std::size_t,std::uint32_t>,5>{{{2,integerType},{3,spv::Dim3D},{4,1},{5,1},{6,1}}})
    {
        auto invalid=stages; invalid.at(VK_SHADER_STAGE_FRAGMENT_BIT)[imageOffset+operand]=value;
        Check(!ValidateProjectPassShaderSpirv(definition,invalid,16,signature,error) && !error.empty() && signature.globalBindings==retainedBindings,
            "wrong sampler numeric class/dimension/depth/array/sample count accepted or replaced the published signature");
    }
    std::cout<<"[ProjectPassGPU] early sampled image shape/atomic ABI rejection PASS\n";
}
void VerifyInputBufferContract(const VansGraphicsShader& shader)
{
    auto definition=Resources().projectRenderPasses[0].definition;
    definition.buffers[0].role="storageInput";
    definition.buffers[0].layout.clear();
    auto stages=CaptureStageSpirv(shader);
    VansProjectPassShaderSignature signature;
    std::string error;
    Check(ValidateProjectPassShaderSpirv(definition,stages,16,signature,error),"matching raw storage input rejected: "+error);
    definition.buffers[0].stride=16;
    Check(!ValidateProjectPassShaderSpirv(definition,stages,16,signature,error)&&!error.empty(),"raw storage runtime-array ABI stride mismatch accepted");
    definition.buffers[0].stride=96;
    Check(!ValidateProjectPassShaderSpirv(definition,stages,8,signature,error)&&!error.empty(),"reflected UBO larger than its declared range accepted");
    std::cout<<"[ProjectPassGPU] input buffer range/runtime-array ABI rejection PASS\n";
}
void VerifyOutputSampling(VansVKDevice& device, bool alpha = false)
{
    VANS_INIT_MAIN_THREAD();
    auto resources=Resources();std::string error;
    if (alpha)
    {
        resources.projectRenderPasses[0].definition.shaders[0]=Vans::VansAssetGuid::FromStableName("ProjectPassGPU","alpha-shader");
        resources.shaders[0].assetGuid=resources.projectRenderPasses[0].definition.shaders[0].ToString();
        resources.shaders[0].name="ProjectPassGPUAlpha";
    }
    VansProjectRenderPassState state;Check(state.Configure(resources,error),error);
    VansMesh mesh;mesh.SetName(resources.meshes[0].name);
    const std::array<glm::vec2,3> vertices{{{-.75f,-.75f},{.75f,-.75f},{0,.75f}}};
    const std::array<std::uint32_t,3> indices{{0,1,2}};
    mesh.InitFromRawData(device.GetLogicDevice(),vertices.data(),3,sizeof(glm::vec2),indices.data(),3,
        {{0,sizeof(glm::vec2),VK_VERTEX_INPUT_RATE_VERTEX}},{{0,0,VK_FORMAT_R32G32_SFLOAT,0}});
    VansScene scene;scene.AddMeshAsset(&mesh);
    VANS_INIT_RENDER_THREAD();auto logic=device.GetLogicDevice();auto& runtime=device.ProjectRenderPassRuntime();
    struct ResourcesOwner
    {
        VansVKDevice& device;VansVKImage display;VansVKBuffer readback;
        VkRenderPass pass=VK_NULL_HANDLE;VkFramebuffer framebuffer=VK_NULL_HANDLE;
        VkDescriptorSetLayout layout=VK_NULL_HANDLE;std::vector<VkDescriptorSet> sets;
        VansGraphicsShader shader;
        ~ResourcesOwner()
        {
            device.WaitForDevice();device.ProjectRenderPassRuntime().ShutdownAtIdle();device.DrainDeferredDeletesAfterDeviceIdle();
            shader.RetireRenderPassPipelineVariants(pass);
            if(framebuffer)vkDestroyFramebuffer(device.GetLogicDevice(),framebuffer,nullptr);
            if(pass)vkDestroyRenderPass(device.GetLogicDevice(),pass,nullptr);
            VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(sets);
            VansVKDescriptorManager::GetInstance()->ReleaseDescriptorSetLayout(layout);
            display.DestroyVulkanImage(device.GetLogicDevice());readback.DestroyVulkanBuffer(device.GetLogicDevice());
        }
    } owned{device};
    Check(owned.display.CreateVulkanImage(logic,{32,32,1},VK_FORMAT_R8G8B8A8_UNORM,1,1,VK_IMAGE_TYPE_2D,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_SAMPLE_COUNT_1_BIT),"sampling display allocation");
    Check(owned.readback.CreatVulkanBuffer(logic,32*32*4,VK_FORMAT_UNDEFINED,VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&owned.readback.PersistentMap(),"sampling readback allocation");
    VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R8G8B8A8_UNORM;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference reference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&reference;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};info.attachmentCount=1;info.pAttachments=&attachment;info.subpassCount=1;info.pSubpasses=&subpass;
    VkSubpassDependency dependency{};dependency.srcSubpass=0;dependency.dstSubpass=VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependency.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependency.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dependency.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    info.dependencyCount=1;info.pDependencies=&dependency;
    Check(vkCreateRenderPass(logic,&info,nullptr,&owned.pass)==VK_SUCCESS,"sampling render pass");
    const auto view=owned.display.GetImageView();VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass=owned.pass;fb.attachmentCount=1;fb.pAttachments=&view;fb.width=fb.height=32;fb.layers=1;
    Check(vkCreateFramebuffer(logic,&fb,nullptr,&owned.framebuffer)==VK_SUCCESS,"sampling framebuffer");
    VansDescriptorSetLayoutFactory::CreateAndAllocate_Global(owned.layout,owned.sets,MAX_BINDLESS_TEXTURES,2);
    VansShaderEntry entry;entry.name="OutputSamplingGPU";entry.relativePath="EngineAssets/Validation/ProjectRenderPass";
    entry.depthTest=entry.depthWrite=VK_FALSE;entry.cullMode=VK_CULL_MODE_NONE;entry.colorAttachmentCount=1;
    entry.explicitStageFiles={{VK_SHADER_STAGE_VERTEX_BIT,"OutputSample.vert"},{VK_SHADER_STAGE_FRAGMENT_BIT,"OutputSample.frag"}};
    Check(VansShaderManager::Get().ConfigureGraphicsShader(owned.shader,entry,entry.relativePath),"sampling Shader state");
    owned.shader.SetArtifactRoot(std::filesystem::current_path()/"Library/Artifacts/Shaders");
    Check(owned.shader.InitShader(logic,entry.relativePath,entry.explicitStageFiles),"sampling Shader loading");
    VerifySampledImageContract(owned.shader,resources.projectRenderPasses[0].definition);
    GlobalStateData pipelineState;pipelineState.currentRenderPass=owned.pass;
    pipelineState.viewport={0,0,32,32,0,1};pipelineState.scissor={{0,0},{32,32}};
    auto* pipeline=owned.shader.GetGraphicsPipeline(logic,pipelineState,{owned.layout});Check(pipeline&&pipeline->GetNativePipeline(),"sampling Pipeline");
    Command producer(device,device.GetGraphicsQueueFamilyIndex()),consumer(device,device.GetGraphicsQueueFamilyIndex());
    Submission submission;VkQueue compute=VK_NULL_HANDLE;vkGetDeviceQueue(logic,device.GetComputeQueueFamilyIndex(),0,&compute);
    Check(submission.timelines.Initialize(logic,device.GetGraphicsQueue(),compute),"sampling timelines");
    for(auto* graph:{&submission.prelude[0],&submission.consumer[0]})
    {graph->Bind(logic,device.GetGraphicsQueue(),compute);graph->SetQueueTimelines(&submission.timelines);graph->SetCrossFrameResourceLedger(&submission.ledger);}
    for(std::uint32_t frame=0;frame<3;++frame)
    {
        VANS_INIT_MAIN_THREAD();
        if(frame==2){resources.projectRenderPasses[0].definition.targets[0].width=64;Check(state.Configure(resources,error),error);}
        VansProjectDrawInstance instance;instance.color=frame==0?glm::vec4(1,0,0,1):glm::vec4(0,0,1,1);
        if (alpha) instance.color.a=0.5f;
        Check(state.SubmitInstances("configured-draw",{instance},error),error);const auto input=state.CaptureFrame();
        VANS_INIT_RENDER_THREAD();VANS_SET_FRAME_PHASE(VansFramePhase::GPURecord);
        Check(producer.value.ResetCommandBuffer(false)&&runtime.Record(device,scene,input,producer.value,frame%2,2,{32,32},1,error),error);
        auto& prelude=submission.prelude[0];prelude.Reset();VansFrameSubmitNode start;start.name="BeforeBuiltInRender";
        start.commandBuffers={producer.value.GetVKCommandBuffer()};start.resources=runtime.RecordedResources();start.fence=producer.value.m_CommandBufferFinishSubmitFence;
        prelude.AddNode(std::move(start));Check(prelude.Execute(),prelude.GetLastError());runtime.ResolveSubmission(true);
        const auto snapshot=runtime.CaptureSnapshot();auto* output=runtime.ResolveTarget(snapshot.targets[0].resourceKey,snapshot.generation,snapshot.targets[0].currentSlot);
        Check(output&&runtime.BindOutputDescriptors(logic,owned.sets[frame%2],{output->GetSampler(),output->GetImageView(),VK_IMAGE_LAYOUT_GENERAL}),"sampling binding");
        Check(consumer.value.ResetCommandBuffer(false)&&consumer.value.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"sampling begin");
        const auto cb=consumer.value.GetVKCommandBuffer();VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=owned.display.GetImage();barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        barrier.oldLayout=frame?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcAccessMask=frame?VK_ACCESS_TRANSFER_READ_BIT:0;barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cb,frame?VK_PIPELINE_STAGE_TRANSFER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkClearValue clear{};VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass=owned.pass;begin.framebuffer=owned.framebuffer;begin.renderArea={{0,0},{32,32}};begin.clearValueCount=1;begin.pClearValues=&clear;
        vkCmdBeginRenderPass(cb,&begin,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline->GetNativePipeline());
        vkCmdSetViewport(cb,0,1,&pipelineState.viewport);vkCmdSetScissor(cb,0,1,&pipelineState.scissor);
        vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline->GetNativePipelineLayout(),0,1,&owned.sets[frame%2],0,nullptr);
        vkCmdDraw(cb,3,1,0,0);vkCmdEndRenderPass(cb);
        barrier.oldLayout=barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={32,32,1};
        vkCmdCopyImageToBuffer(cb,owned.display.GetImage(),VK_IMAGE_LAYOUT_GENERAL,owned.readback.GetNativeBuffer(),1,&copy);
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=owned.readback.GetNativeBuffer();host.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);Check(consumer.value.EndCommandBufferRecord(),"sampling end");
        auto& graph=submission.consumer[0];graph.Reset();VansFrameSubmitNode finish;finish.name="ShaderConsumer";
        finish.commandBuffers={cb};finish.fence=consumer.value.m_CommandBufferFinishSubmitFence;graph.AddNode(std::move(finish));
        graph.AddQueueReadResources(VansQueueRole::Graphics,runtime.ExportedReadResources());
        graph.AddExternalPrerequisite(*prelude.GetSubmittedTimelinePoint("BeforeBuiltInRender"));
        Check(graph.Execute()&&graph.WaitForCompletion()&&prelude.WaitForCompletion(),graph.GetLastError());
        Check(vkResetFences(logic,1,&producer.value.m_CommandBufferFinishSubmitFence)==VK_SUCCESS&&vkResetFences(logic,1,&consumer.value.m_CommandBufferFinishSubmitFence)==VK_SUCCESS,"sampling fence reset");
        owned.readback.InvalidateMappedRange(0,32*32*4);const auto* bytes=static_cast<const std::uint8_t*>(owned.readback.GetMappedPtr())+(16*32+16)*4;
        if (alpha)
        {
            const std::array<int,4> expected=frame==0?std::array<int,4>{128,0,0,128}:
                frame==1?std::array<int,4>{64,0,128,192}:std::array<int,4>{0,0,128,128};
            for (std::size_t channel=0;channel<4;++channel)
                Check(std::abs(int(bytes[channel])-expected[channel])<=1,"automatic attachment count alpha blend/history output differs");
        }
        else Check(bytes[0]==(frame==0?255:0)&&bytes[1]==0&&bytes[2]==(frame==0?0:255)&&bytes[3]==255,"Shader sampled stale generation or wrong frame-slot output");
    }
    std::cout<<"[ProjectPassGPU] exported RT graphics sampling/frame-slot/resize alpha="<<alpha<<" PASS\n";
}
void VerifyTireMRT(VansVKDevice& device)
{
    auto resources=Resources();auto& definition=resources.projectRenderPasses[0].definition;
    resources.shaders[0].name="TireMRTGPU";
    definition.shaders[0]=Vans::VansAssetGuid::FromStableName("ProjectPassGPU","tire-mrt-shader");
    resources.shaders[0].assetGuid=definition.shaders[0].ToString();
    const auto target=definition.targets[0];
    for(int i=1;i<3;++i)
    {
        auto extra=target;extra.name=i==1?"normal":"deformation";
        extra.exportSlot=i;extra.exportChannel=extra.name;extra.format=Vans::VansRenderTargetFormat::RGBA16F;
        if(i==1)extra.clear={.5f,1,.5f,0};definition.targets.push_back(extra);
    }
    Command upload(device,device.GetGraphicsQueueFamilyIndex());
    std::array<VansTexture,3> textures;
    const std::array<std::array<std::uint8_t,4>,3> pixels{{{64,128,192,255},{128,255,128,255},{255,255,255,255}}};
    VansScene scene;
    for(int i=0;i<3;++i)
    {
        Vans::VansSceneTextureResourceRequest request;
        request.name="TireMRT.texture"+std::to_string(i);request.assetGuid=Vans::VansAssetGuid::FromStableName("TireMRT",request.name).ToString();
        resources.textures.push_back(request);
        definition.textures.push_back({request.name,Vans::VansAssetGuid::FromStableName("TireMRT",request.name),static_cast<std::uint32_t>(i+2)});
        textures[i].SetName(request.name);textures[i].m_TextureType=TEXTURE_2D;
        textures[i].LoadFromMemory(upload.value,pixels[i].data(),4,1,1,VK_FORMAT_R8G8B8A8_UNORM);
        Check(textures[i].GetImage().GetImage()!=VK_NULL_HANDLE,"MRT texture upload");scene.AddTextureAsset(&textures[i],request.assetGuid);
    }
    VansMesh mesh;mesh.SetName(resources.meshes[0].name);
    const std::array<glm::vec2,4> vertices{{{-.5f,-.5f},{.5f,-.5f},{.5f,.5f},{-.5f,.5f}}};
    const std::array<std::uint32_t,6> indices{{0,1,2,2,3,0}};
    mesh.InitFromRawData(device.GetLogicDevice(),vertices.data(),4,sizeof(glm::vec2),indices.data(),6,
        {{0,sizeof(glm::vec2),VK_VERTEX_INPUT_RATE_VERTEX}},{{0,0,VK_FORMAT_R32G32_SFLOAT,0}});scene.AddMeshAsset(&mesh);
    auto& runtime=device.ProjectRenderPassRuntime();
    struct Cleanup{VansVKDevice& device;~Cleanup(){device.WaitForDevice();device.ProjectRenderPassRuntime().ShutdownAtIdle();}} cleanup{device};
    VansProjectRenderPassState state;std::string error;VANS_INIT_MAIN_THREAD();Check(state.Configure(resources,error),error);
    Command producer(device,device.GetGraphicsQueueFamilyIndex()),reader(device,device.GetGraphicsQueueFamilyIndex());
    Submission submission;VkQueue compute=VK_NULL_HANDLE;vkGetDeviceQueue(device.GetLogicDevice(),device.GetComputeQueueFamilyIndex(),0,&compute);
    Check(submission.timelines.Initialize(device.GetLogicDevice(),device.GetGraphicsQueue(),compute),"MRT timelines");
    for(auto* graph:{&submission.prelude[0],&submission.consumer[0]})
    {graph->Bind(device.GetLogicDevice(),device.GetGraphicsQueue(),compute);graph->SetQueueTimelines(&submission.timelines);graph->SetCrossFrameResourceLedger(&submission.ledger);}
    VansVKBuffer readback;
    struct BufferCleanup{VansVKDevice& device;VansVKBuffer& buffer;~BufferCleanup(){device.WaitForDevice();buffer.DestroyVulkanBuffer(device.GetLogicDevice());}} bufferCleanup{device,readback};
    constexpr std::uint32_t colorBytes=32*32*4,floatBytes=32*32*8,totalBytes=colorBytes+floatBytes*2;
    Check(readback.CreatVulkanBuffer(device.GetLogicDevice(),totalBytes,VK_FORMAT_UNDEFINED,VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&readback.PersistentMap(),"MRT readback");
    for(std::uint32_t frame=0;frame<3;++frame)
    {
        VANS_INIT_MAIN_THREAD();
        if(frame<2){VansProjectDrawInstance stamp;stamp.color={frame==0?.03f:.01f,1,1,1};Check(state.SubmitInstances("configured-draw",{stamp},error),error);}
        auto input=state.CaptureFrame();VANS_INIT_RENDER_THREAD();VANS_SET_FRAME_PHASE(VansFramePhase::GPURecord);
        Check(producer.value.ResetCommandBuffer(false)&&runtime.Record(device,scene,input,producer.value,frame%2,2,{32,32},1,error),error);
        auto& prelude=submission.prelude[0];prelude.Reset();VansFrameSubmitNode start;start.name="MRTProducer";
        start.commandBuffers={producer.value.GetVKCommandBuffer()};start.resources=runtime.RecordedResources();start.fence=producer.value.m_CommandBufferFinishSubmitFence;
        prelude.AddNode(std::move(start));Check(prelude.Execute(),prelude.GetLastError());runtime.ResolveSubmission(true);
        auto snapshot=runtime.CaptureSnapshot();Check(snapshot.targets.size()==3,"MRT target publication");
        Check(reader.value.ResetCommandBuffer(false)&&reader.value.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"MRT read begin");
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        reader.value.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{barrier});
        for(int i=0;i<3;++i)
        {
            const auto& target=snapshot.targets[i];auto* image=runtime.ResolveTarget(target.resourceKey,snapshot.generation,target.currentSlot);Check(image,"MRT resolve");
            VkBufferImageCopy copy{};copy.bufferOffset=i==0?0:colorBytes+(i-1)*floatBytes;
            copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={32,32,1};
            vkCmdCopyImageToBuffer(reader.value.GetVKCommandBuffer(),image->GetImage(),VK_IMAGE_LAYOUT_GENERAL,readback.GetNativeBuffer(),1,&copy);
        }
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        reader.value.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Check(reader.value.EndCommandBufferRecord(),"MRT read end");
        auto& consumer=submission.consumer[0];consumer.Reset();VansFrameSubmitNode finish;finish.name="MRTReadback";
        finish.commandBuffers={reader.value.GetVKCommandBuffer()};finish.fence=reader.value.m_CommandBufferFinishSubmitFence;consumer.AddNode(std::move(finish));
        consumer.AddQueueReadResources(VansQueueRole::Graphics,runtime.ExportedReadResources());consumer.AddExternalPrerequisite(*prelude.GetSubmittedTimelinePoint("MRTProducer"));
        Check(consumer.Execute()&&consumer.WaitForCompletion()&&prelude.WaitForCompletion(),consumer.GetLastError());
        Check(vkResetFences(device.GetLogicDevice(),1,&producer.value.m_CommandBufferFinishSubmitFence)==VK_SUCCESS&&
            vkResetFences(device.GetLogicDevice(),1,&reader.value.m_CommandBufferFinishSubmitFence)==VK_SUCCESS,"MRT fences");
        readback.InvalidateMappedRange(0,totalBytes);const auto* bytes=static_cast<const std::uint8_t*>(readback.GetMappedPtr());constexpr auto center=(16*32+16);
        const int alpha=frame==0?128:192;Check(std::abs(int(bytes[center*4+3])-alpha)<=1,"MRT alpha history");
        const float coverage=frame==0?.5f:.75f;
        for(int channel=0;channel<3;++channel)
            Check(std::abs(int(bytes[center*4+channel])-int(pixels[0][channel]*coverage+.5f))<=1,"MRT sampled albedo");
        const auto half=[&](int output,int channel){return glm::unpackHalf1x16(reinterpret_cast<const std::uint16_t*>(bytes+colorBytes+(output-1)*floatBytes)[center*4+channel]);};
        Check(std::abs(half(2,0)-.03f)<.0001f&&std::abs(half(2,1)-.006f)<.0001f&&std::abs(half(2,2)-.75f)<.001f,"weaker stamp erased maximum deformation/history");
        Check(half(1,1)>.99f&&half(1,0)>.49f&&half(1,0)<.51f,"MRT sampled normal");
        device.RefreshRuntimeRenderTextureCatalog();Check(device.QueryRuntimeRenderTextures().size()==3,"three MRT previews must be discoverable");
    }
    std::cout<<"[ProjectPassGPU] sampled textures/MRT alpha/MAX deformation/history/three previews PASS\n";
}
void VerifyUnifiedSurfaceProducer(VansVKDevice& device,const std::string& projectRoot)
{
    Vans::VansRenderPassAsset asset;std::string error;
    Check(Vans::VansRenderPassAssetStorage::Load(std::filesystem::path(projectRoot)/"Assets/Rendering/SurfaceInteraction.vrenderpass",asset,error),error);
    Check(asset.targets.size()==3&&asset.textures.size()==6,"shared producer must own three semantic targets and both source texture sets");
    auto resources=Resources();auto& definition=resources.projectRenderPasses[0].definition;definition=asset;
    // Bound the readback fixture, while retaining the delivered asset ABI and production shaders.
    for(auto& target:definition.targets) target.width=target.height=32;
    for(auto& parameter:definition.parameters)
    {if(parameter.name=="worldRect")parameter.initialValue={-1,-1,2,2};if(parameter.name=="outputTexel")parameter.initialValue={1.f/32,1.f/32,0,0};}
    resources.shaders[0].name="SurfaceInteractionProductionGPU";resources.shaders[0].assetGuid=definition.shaders[0].ToString();
    resources.meshes[0].assetGuid=definition.instanceMesh.ToString();
    VansShaderEntry entry;entry.relativePath=(std::filesystem::path(projectRoot)/"Assets/Shaders/SurfaceInteraction").generic_string();
    entry.assetGuid=definition.shaders[0].ToString();entry.explicitStageFiles={{VK_SHADER_STAGE_VERTEX_BIT,"SurfaceStamp.vert"},{VK_SHADER_STAGE_FRAGMENT_BIT,"SurfaceStamp.frag"}};
    entry.depthTest=entry.depthWrite=VK_FALSE;entry.cullMode=VK_CULL_MODE_NONE;entry.colorAttachmentCount=3;entry.colorAttachmentBlends={"alpha","alpha","max"};
    auto& shaders=VansShaderManager::Get();shaders.RegisterGraphicsShader(resources.shaders[0].name,entry);
    Check(shaders.LoadAll("",std::filesystem::current_path()/"Library/Artifacts/Shaders",device.GetLogicDevice()),"production SurfaceStamp shader load");
    Command upload(device,device.GetGraphicsQueueFamilyIndex());std::array<VansTexture,6> textures;
    const std::array<std::array<std::uint8_t,4>,6> pixels{{{128,32,16,255},{179,153,230,255},{128,128,128,255},{200,200,200,255},{128,128,255,255},{143,143,143,255}}};
    VansScene scene;
    for(int i=0;i<6;++i)
    {
        Vans::VansSceneTextureResourceRequest request;request.name=definition.textures[i].name;request.assetGuid=definition.textures[i].asset.ToString();
        resources.textures.push_back(request);textures[i].SetName(request.name);textures[i].m_TextureType=TEXTURE_2D;
        textures[i].LoadFromMemory(upload.value,pixels[i].data(),4,1,1,VK_FORMAT_R8G8B8A8_UNORM);
        Check(textures[i].GetImage().GetImage()!=VK_NULL_HANDLE,"surface texture upload");scene.AddTextureAsset(&textures[i],request.assetGuid);
    }
    VansMesh mesh;mesh.SetName(resources.meshes[0].name);
    const std::array<glm::vec3,4> vertices{{{-.5f,0,0},{.5f,0,0},{.5f,0,1},{-.5f,0,1}}};
    const std::array<std::uint32_t,6> indices{{0,1,2,2,3,0}};
    mesh.InitFromRawData(device.GetLogicDevice(),vertices.data(),4,sizeof(glm::vec3),indices.data(),6,
        {{0,sizeof(glm::vec3),VK_VERTEX_INPUT_RATE_VERTEX}},{{0,0,VK_FORMAT_R32G32B32_SFLOAT,0}});scene.AddMeshAsset(&mesh);
    auto& runtime=device.ProjectRenderPassRuntime();
    struct Cleanup{VansVKDevice& device;~Cleanup(){device.WaitForDevice();device.ProjectRenderPassRuntime().ShutdownAtIdle();}} cleanup{device};
    VansProjectRenderPassState state;VANS_INIT_MAIN_THREAD();Check(state.Configure(resources,error),error);
    Command producer(device,device.GetGraphicsQueueFamilyIndex()),reader(device,device.GetGraphicsQueueFamilyIndex());
    Submission submission;VkQueue compute=VK_NULL_HANDLE;vkGetDeviceQueue(device.GetLogicDevice(),device.GetComputeQueueFamilyIndex(),0,&compute);
    Check(submission.timelines.Initialize(device.GetLogicDevice(),device.GetGraphicsQueue(),compute),"MRT timelines");
    for(auto* graph:{&submission.prelude[0],&submission.consumer[0]})
    {graph->Bind(device.GetLogicDevice(),device.GetGraphicsQueue(),compute);graph->SetQueueTimelines(&submission.timelines);graph->SetCrossFrameResourceLedger(&submission.ledger);}
    VansVKBuffer readback;
    struct BufferCleanup{VansVKDevice& device;VansVKBuffer& buffer;~BufferCleanup(){device.WaitForDevice();buffer.DestroyVulkanBuffer(device.GetLogicDevice());}} bufferCleanup{device,readback};
    constexpr std::uint32_t colorBytes=32*32*4,floatBytes=32*32*8,totalBytes=colorBytes+floatBytes*2;
    Check(readback.CreatVulkanBuffer(device.GetLogicDevice(),totalBytes,VK_FORMAT_UNDEFINED,VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&readback.PersistentMap(),"MRT readback");
    for(std::uint32_t frame=0;frame<3;++frame)
    {
        VANS_INIT_MAIN_THREAD();
        if(frame==0)
        {
            VansProjectDrawInstance foot,tire;
            for(auto* stamp:{&foot,&tire}){stamp->model[0]={.5f,0,0,0};stamp->model[1]={0,1,0,0};stamp->model[2]={0,0,.5f,0};}
            foot.model[3]={-.5f,0,-.25f,1};foot.color={1,1,1,1};foot.data={0,0,-1,.012f};
            tire.model[3]={.5f,0,-.25f,1};tire.color={.3f,.2f,.1f,.26f};tire.data={0,.5f/.26f,1,.026f};
            Check(state.SubmitInstances(definition.passId,{foot,tire},error),error);
        }
        auto input=state.CaptureFrame();VANS_INIT_RENDER_THREAD();VANS_SET_FRAME_PHASE(VansFramePhase::GPURecord);
        Check(producer.value.ResetCommandBuffer(false)&&runtime.Record(device,scene,input,producer.value,frame%2,2,{32,32},1,error),error);
        auto& prelude=submission.prelude[0];prelude.Reset();VansFrameSubmitNode start;start.name="MRTProducer";
        start.commandBuffers={producer.value.GetVKCommandBuffer()};start.resources=runtime.RecordedResources();start.fence=producer.value.m_CommandBufferFinishSubmitFence;
        prelude.AddNode(std::move(start));Check(prelude.Execute(),prelude.GetLastError());runtime.ResolveSubmission(true);
        auto snapshot=runtime.CaptureSnapshot();Check(snapshot.targets.size()==3,"MRT target publication");
        Check(reader.value.ResetCommandBuffer(false)&&reader.value.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"MRT read begin");
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        reader.value.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{barrier});
        for(int i=0;i<3;++i)
        {
            const auto& target=snapshot.targets[i];auto* image=runtime.ResolveTarget(target.resourceKey,snapshot.generation,target.currentSlot);Check(image,"MRT resolve");
            VkBufferImageCopy copy{};copy.bufferOffset=i==0?0:colorBytes+(i-1)*floatBytes;
            copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={32,32,1};
            vkCmdCopyImageToBuffer(reader.value.GetVKCommandBuffer(),image->GetImage(),VK_IMAGE_LAYOUT_GENERAL,readback.GetNativeBuffer(),1,&copy);
        }
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        reader.value.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{barrier});Check(reader.value.EndCommandBufferRecord(),"MRT read end");
        auto& consumer=submission.consumer[0];consumer.Reset();VansFrameSubmitNode finish;finish.name="MRTReadback";
        finish.commandBuffers={reader.value.GetVKCommandBuffer()};finish.fence=reader.value.m_CommandBufferFinishSubmitFence;consumer.AddNode(std::move(finish));
        consumer.AddQueueReadResources(VansQueueRole::Graphics,runtime.ExportedReadResources());consumer.AddExternalPrerequisite(*prelude.GetSubmittedTimelinePoint("MRTProducer"));
        Check(consumer.Execute()&&consumer.WaitForCompletion()&&prelude.WaitForCompletion(),consumer.GetLastError());
        Check(vkResetFences(device.GetLogicDevice(),1,&producer.value.m_CommandBufferFinishSubmitFence)==VK_SUCCESS&&
            vkResetFences(device.GetLogicDevice(),1,&reader.value.m_CommandBufferFinishSubmitFence)==VK_SUCCESS,"MRT fences");
        readback.InvalidateMappedRange(0,totalBytes);const auto* bytes=static_cast<const std::uint8_t*>(readback.GetMappedPtr());
        const auto half=[&](int output,int x,int channel){return glm::unpackHalf1x16(reinterpret_cast<const std::uint16_t*>(bytes+colorBytes+(output-1)*floatBytes)[(16*32+x)*4+channel]);};
        for(int x:{8,24})Check(bytes[(16*32+x)*4+3]>200,"both foot and tire must persist in the same albedo target");
        Check(bytes[(16*32+8)*4]>bytes[(16*32+24)*4],"shared albedo retains each stamp's distinct color");
        Check(std::abs(half(2,8,0)-.012f)<.0001f&&std::abs(half(2,24,0)-.026f)<.0003f,"shared height retains both producer depths in meters");
        Check(std::abs(half(1,8,0)-.5f)>.05f&&half(1,24,1)>.99f,"shared normal receives actual foot and tire writes");
        for(const auto& target:snapshot.targets)Check(target.worldRect&&*target.worldRect==glm::vec4(-1,-1,2,2),"all semantic publications retain their actual produced world window");
        device.RefreshRuntimeRenderTextureCatalog();Check(device.QueryRuntimeRenderTextures().size()==3,"three MRT previews must be discoverable");
    }
    std::cout<<"[SurfaceInteractionGPU] production shader, feet + tires in shared albedo/normal/height, persistent history, authoritative window, three previews PASS\n";
}
void VerifyConfigurationApply(VansVKDevice& device)
{
    struct Executor final:IVansRenderThreadTransactionExecutor
    {
        VansVKDevice& device;
        explicit Executor(VansVKDevice& value):device(value) {}
        bool ExecuteRenderThreadTransaction(std::unique_ptr<IVansRenderThreadTransaction> transaction)override
        {
            VANS_INIT_RENDER_THREAD();
            const bool accepted=device.WaitForDevice()&&transaction->Execute(device);
            VANS_INIT_MAIN_THREAD();return accepted;
        }
    } executor(device);
    VANS_INIT_MAIN_THREAD();
    auto resources=Resources();std::string error;
    VansMesh mesh;mesh.SetName(resources.meshes[0].name);
    const std::array<glm::vec2,3> vertices{{{-.5f,-.5f},{.5f,-.5f},{0,.5f}}};
    const std::array<std::uint32_t,3> indices{{0,1,2}};
    mesh.InitFromRawData(device.GetLogicDevice(),vertices.data(),3,sizeof(glm::vec2),indices.data(),3,
        {{0,sizeof(glm::vec2),VK_VERTEX_INPUT_RATE_VERTEX}},{{0,0,VK_FORMAT_R32G32_SFLOAT,0}});
    VansScene scene;scene.AddMeshAsset(&mesh);
    struct Cleanup{VansVKDevice& device;~Cleanup(){VANS_INIT_RENDER_THREAD();device.WaitForDevice();device.ProjectRenderPassRuntime().ShutdownAtIdle();device.DrainDeferredDeletesAfterDeviceIdle();}}cleanup{device};
    const auto context=Vans::VansSceneResourceLoadContext::ForEditor(std::filesystem::current_path(),std::filesystem::current_path(),{});
    const auto apply=[&](const auto& plan){return VansProjectRenderPassProjector::Apply(scene,executor,plan,context,32,32,error);};
    Check(apply(resources),error);
    const auto published=scene.ProjectRenderPasses().Configuration();
    const auto generation=device.ProjectRenderPassRuntime().CaptureSnapshot().generation;
    Check(published&&generation&&!device.ProjectRenderPassRuntime().HasRecordedWork(),"Apply must prepare resources without executing a Pass");
    Check(scene.ProjectRenderPasses().SetParameter("configured-draw","tint",{9,8,7,6},error)&&
        scene.ProjectRenderPasses().SubmitInstances("configured-draw",{VansProjectDrawInstance{}},error),error);
    auto invalid=resources;invalid.projectRenderPasses[0].definition.budgetBytes=4096;
    Check(!apply(invalid)&&!error.empty()&&scene.ProjectRenderPasses().Configuration()==published&&
        device.ProjectRenderPassRuntime().CaptureSnapshot().generation==generation,
        "GPU budget rejection must retain both Main configuration and GPU generation");
    invalid=resources;const auto foreign=Vans::VansAssetGuid::FromStableName("ProjectPassGPU","foreign-asset");
    invalid.projectRenderPasses[0].definition.shaders={foreign};invalid.shaders[0].assetGuid=foreign.ToString();
    Check(!apply(invalid)&&!error.empty()&&scene.ProjectRenderPasses().Configuration()==published&&
        device.ProjectRenderPassRuntime().CaptureSnapshot().generation==generation,
        "same runtime Shader name from a different GUID must not replace an existing asset");
    const auto retained=scene.ProjectRenderPasses().CaptureFrame();
    float tint=0;std::memcpy(&tint,retained.passes[0].parameters.data(),sizeof(tint));
    Check(retained.passes[0].instanceCount==1&&tint==9,"rejected Apply must retain pending inputs and parameters");
    invalid=resources;const auto missing=Vans::VansAssetGuid::FromStableName("ProjectPassGPU","unavailable-shader");
    invalid.projectRenderPasses[0].definition.shaders={missing};invalid.shaders[0].assetGuid=missing.ToString();invalid.shaders[0].name="UnpublishedProjectPassGPU";
    Check(!apply(invalid)&&!VansShaderManager::Get().FindShaderEntry("UnpublishedProjectPassGPU")&&
        scene.ProjectRenderPasses().Configuration()==published&&device.ProjectRenderPassRuntime().CaptureSnapshot().generation==generation,
        "failed Shader preparation must leave no registration or published generation");
    auto resized=resources;resized.projectRenderPasses[0].definition.targets[0].width=64;
    Check(apply(resized),error);
    const auto next=device.ProjectRenderPassRuntime().CaptureSnapshot();
    Check(next.generation>generation&&next.targets[0].width==64&&scene.ProjectRenderPasses().Configuration()->revision>published->revision,
        "accepted Apply must publish matching Main configuration and resource generation");
    auto empty=resources;empty.projectRenderPasses.clear();Check(apply(empty),error);
    Check(scene.ProjectRenderPasses().Configuration()->passes.empty()&&device.ProjectRenderPassRuntime().CaptureSnapshot().targets.empty(),
        "empty Apply must release the project generation");
    std::cout<<"[ProjectPassGPU] configuration Apply/prepare/atomic-rejection/resize/empty PASS\n";
}
}
bool RunProjectRenderPassGpuContractTests(const std::string& interactionProjectRoot)
{
    try
    {
        VANS_INIT_MAIN_THREAD();validationErrors=0;
        Check(m_GraphicsDevice==nullptr,"separate process required");
        const auto* layers=std::getenv("VK_INSTANCE_LAYERS");Check(layers&&std::string(layers).find("VK_LAYER_KHRONOS_validation")!=std::string::npos,"validation layer must be enabled");
        Window window;Check(glfwInit()==GLFW_TRUE,"GLFW initialization");
        glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        window.handle=glfwCreateWindow(64,64,"ProjectPass native contract",nullptr,nullptr);Check(window.handle!=nullptr,"hidden window creation");
        auto device=std::make_unique<VansVKDevice>(VkExtent2D{64,64},&window);Check(device->IsInitialized(),"native device initialization");
        m_GraphicsDevice=device.get();struct GlobalDeviceCleanup{~GlobalDeviceCleanup(){m_GraphicsDevice=nullptr;}} globalDeviceCleanup;
        auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(device->GetInstance(),"vkCreateDebugUtilsMessengerEXT"));
        auto destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(device->GetInstance(),"vkDestroyDebugUtilsMessengerEXT"));
        Check(create&&destroy,"debug messenger functions");VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        info.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;info.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;info.pfnUserCallback=Validation;
        VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;Check(create(device->GetInstance(),&info,nullptr,&messenger)==VK_SUCCESS,"debug messenger");
        struct MessengerCleanup{VkInstance instance;VkDebugUtilsMessengerEXT handle;PFN_vkDestroyDebugUtilsMessengerEXT destroy;~MessengerCleanup(){destroy(instance,handle,nullptr);}} messengerCleanup{device->GetInstance(),messenger,destroy};
        auto& shaders=VansShaderManager::Get();VansShaderEntry entry;entry.relativePath="EngineAssets/Validation/ProjectRenderPass";
        entry.assetGuid=Vans::VansAssetGuid::FromStableName("ProjectPassGPU","shader").ToString();
        entry.explicitStageFiles={{VK_SHADER_STAGE_VERTEX_BIT,"Draw.vert"},{VK_SHADER_STAGE_FRAGMENT_BIT,"Draw.frag"}};
        entry.depthTest=entry.depthWrite=VK_FALSE;entry.cullMode=VK_CULL_MODE_NONE;entry.colorAttachmentCount=1;
        shaders.RegisterGraphicsShader("ProjectPassGPU",entry);
        entry.assetGuid=Vans::VansAssetGuid::FromStableName("ProjectPassGPU","alpha-shader").ToString();
        entry.colorAttachmentCount=-1;entry.enableAlphaBlend=true;
        shaders.RegisterGraphicsShader("ProjectPassGPUAlpha",entry);
        entry.assetGuid=Vans::VansAssetGuid::FromStableName("ProjectPassGPU","tire-mrt-shader").ToString();
        entry.colorAttachmentCount=3;entry.enableAlphaBlend=false;entry.colorAttachmentBlends={"alpha","alpha","max"};
        entry.explicitStageFiles[VK_SHADER_STAGE_FRAGMENT_BIT]="TireMRT.frag";
        shaders.RegisterGraphicsShader("TireMRTGPU",entry);
        Check(shaders.LoadAll(std::filesystem::current_path().generic_string()+"/",std::filesystem::current_path()/"Library/Artifacts/Shaders",device->GetLogicDevice()),"fixture shader load");
        struct ShaderCleanup{~ShaderCleanup(){VansShaderManager::Get().Clear();}} shaderCleanup;
        VerifyInputBufferContract(*shaders.FindGraphicsShader("ProjectPassGPU"));
        VkQueue compute=VK_NULL_HANDLE;vkGetDeviceQueue(device->GetLogicDevice(),device->GetComputeQueueFamilyIndex(),0,&compute);
        for(auto slots:{1u,2u})for(bool async:{false,true})Verify(*device,compute,slots,async);
        for(bool async:{false,true})Verify(*device,compute,2,async,true);
        for(auto slots:{1u,2u})for(bool async:{false,true})Verify(*device,compute,slots,async,false,true);
        for(bool async:{false,true})Verify(*device,compute,2,async,true,true);
        VerifyConfigurationApply(*device);
        VerifyOutputSampling(*device);
        VerifyOutputSampling(*device,true);
        VerifyTireMRT(*device);
        VerifyTerrainInteractionGpu(*device);
        if(!interactionProjectRoot.empty())VerifyUnifiedSurfaceProducer(*device,interactionProjectRoot);
        Check(validationErrors==0,"Vulkan validation errors="+std::to_string(validationErrors));
        std::cout<<"[ProjectPassGPU] CHECKS PASS validationErrors=0\n";return true;
    }
    catch(const std::exception& error){std::cerr<<"[ProjectPassGPU] FAIL "<<error.what()<<'\n';return false;}
}
