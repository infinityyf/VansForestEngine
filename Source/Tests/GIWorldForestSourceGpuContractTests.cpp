#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/GICore/VansGIWorld.h"
#include "../EngineCore/RenderCore/GICore/VansGIProbeDispatchParameters.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/RenderCore/VansMaterial.h"
#include "../EngineCore/RenderCore/VansShaderManager.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKMemoryAllocator.h"
#include "../EngineCore/RenderCore/VulkanCore/VansMesh.h"
#include "../EngineCore/RenderCore/VulkanCore/VansTexture.h"
#include "../EngineCore/RenderCore/VegetationCore/VansVegetationCollection.h"
#include "../EngineCore/PcgCore/Storage/VansPlantTypeAssetStorage.h"
#include "../EngineCore/Interfaces/INativeWindowProvider.h"
#include "../EngineCore/Util/VansJobSystem.h"
#include "../EngineCore/Util/VansProfiler.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#include <map>
#include <set>
#include <cstring>
#include <crtdbg.h>
#include <algorithm>
#include <cmath>

namespace
{
    using namespace VansGraphics;
    using Json=nlohmann::json;
    using Clock=std::chrono::steady_clock;
    void Check(bool value,const std::string& error){if(!value)throw std::runtime_error(error);}
    double Milliseconds(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
    Json Read(const std::filesystem::path& path){std::ifstream file(path);Check(bool(file),"Missing fixture: "+path.string());Json data;file>>data;return data;}
    uint32_t validationErrors=0;
    VKAPI_ATTR VkBool32 VKAPI_CALL Validate(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* message,void*)
    {
        if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {++validationErrors;std::cerr<<"[GIForestSourceGPU validation] "<<message->pMessage<<'\n';}
        return VK_FALSE;
    }
    struct Window final:INativeWindowProvider
    {
        GLFWwindow* handle=nullptr;
        ~Window(){if(handle)glfwDestroyWindow(handle);glfwTerminate();}
        void* GetNativeWindowHandle()const override{return handle;}
    };
    struct Messenger
    {
        VkInstance instance{};VkDebugUtilsMessengerEXT handle{};PFN_vkDestroyDebugUtilsMessengerEXT destroy=nullptr;
        ~Messenger(){if(handle)destroy(instance,handle,nullptr);}
    };
    struct Workers
    {
        Workers(){Vans::VansJobSystem::Get().Initialize(2);}
        ~Workers(){Vans::VansJobSystem::Get().Shutdown();}
    };
    // 复用生产 Profiler 拆解捕获提交成本；不向捕获实现添加测试回调。
    struct CaptureProfile
    {
        inline static uint64_t lastFrame=Vans::VansProfiler::INVALID_FRAME_INDEX;
        bool active=true;
        CaptureProfile(){Vans::VansProfiler::Get().BeginFrame();}
        ~CaptureProfile(){if(active)Vans::VansProfiler::Get().EndFrame();}
        void Finish(Json& report)
        {
            auto& profiler=Vans::VansProfiler::Get();profiler.EndFrame();active=false;
            const auto& frame=profiler.GetTimeline();Json stages=Json::object();
            if(frame.frameDurationUs<=0 || frame.frameIndex==lastFrame)return;
            lastFrame=frame.frameIndex;
            Check(!frame.overflow && frame.droppedCpuEvents==0,"Capture profiling lost CPU events");
            for(uint32_t i=0;i<frame.eventCount;++i)
            {
                const auto& event=frame.events[i];const std::string name=event.name;
                if(name.rfind("GIWorld.Capture.",0)==0)
                    stages[name]=stages.value(name,0.0)+(event.endUs-event.startUs)/1000.0;
            }
            if(!stages.empty())report["captureSetupMilliseconds"].push_back({{"frameIndex",frame.frameIndex},{"stages",std::move(stages)}});
        }
    };
    class EnqueueObserver
    {
    public:
        EnqueueObserver():wait(vkWaitForFences),idle(vkDeviceWaitIdle),submit(vkQueueSubmit),createShader(vkCreateShaderModule),copy(vkCmdCopyBuffer)
        {Check(!active,"Nested enqueue observer");active=this;vkWaitForFences=Wait;vkDeviceWaitIdle=Idle;vkQueueSubmit=Submit;vkCreateShaderModule=CreateShader;vkCmdCopyBuffer=Copy;}
        ~EnqueueObserver(){vkWaitForFences=wait;vkDeviceWaitIdle=idle;vkQueueSubmit=submit;vkCreateShaderModule=createShader;vkCmdCopyBuffer=copy;active=nullptr;}
        uint32_t waits=0,idles=0,submissions=0,shaderModules=0,corruptedCopies=0;
        bool rejectSubmit=false;
        const std::set<VkBuffer>* corruptIndexBuffers=nullptr;
    private:
        PFN_vkWaitForFences wait;PFN_vkDeviceWaitIdle idle;PFN_vkQueueSubmit submit;
        PFN_vkCreateShaderModule createShader;
        PFN_vkCmdCopyBuffer copy;
        inline static EnqueueObserver* active=nullptr;
        static VKAPI_ATTR VkResult VKAPI_CALL Wait(VkDevice d,uint32_t n,const VkFence* f,VkBool32 all,uint64_t timeout)
        {++active->waits;return active->wait(d,n,f,all,timeout);}
        static VKAPI_ATTR VkResult VKAPI_CALL Idle(VkDevice d){++active->idles;return active->idle(d);}
        static VKAPI_ATTR VkResult VKAPI_CALL Submit(VkQueue q,uint32_t n,const VkSubmitInfo* i,VkFence f)
        {++active->submissions;return active->rejectSubmit?VK_ERROR_OUT_OF_HOST_MEMORY:active->submit(q,n,i,f);}
        static VKAPI_ATTR VkResult VKAPI_CALL CreateShader(VkDevice d,const VkShaderModuleCreateInfo* info,const VkAllocationCallbacks* allocator,VkShaderModule* shader)
        {++active->shaderModules;return active->createShader(d,info,allocator,shader);}
        static VKAPI_ATTR void VKAPI_CALL Copy(VkCommandBuffer command,VkBuffer source,VkBuffer destination,uint32_t count,const VkBufferCopy* regions)
        {
            active->copy(command,source,destination,count,regions);
            if(!count || !active->corruptIndexBuffers || !active->corruptIndexBuffers->count(source))return;
            // 仅破坏本次捕获的读回副本；真实 mesh 和资产文件保持不变。
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
            vkCmdFillBuffer(command,destination,regions[0].dstOffset,4,UINT32_MAX);++active->corruptedCopies;
        }
    };
    void Submit(VansVKDevice& device,VansVKCommandBuffer& command)
    {
        Check(command.EndCommandBufferRecord()&&VansVKCommandBuffer::SubmitCommands(device.GetGraphicsQueue(),device.GetLogicDevice(),
            {command.GetVKCommandBuffer()},{},{},command.m_CommandBufferFinishSubmitFence)&&command.ResetCommandBuffer(false),"Fixture submission failed");
    }
    struct Readback
    {
        VkDevice device;VansVKBuffer buffer;
        ~Readback(){buffer.DestroyVulkanBuffer(device);}
    };
    // 只测 GI 查询场；512 条固定射线不冒充最终屏幕图像。
    std::vector<glm::vec4> Query(VansVKDevice& device,VansGIWorld& world)
    {
        constexpr uint32_t rays=512;constexpr size_t bytes=rays*3*sizeof(glm::vec4);
        Readback readback{device.GetLogicDevice()};
        Check(readback.buffer.CreatVulkanBuffer(device.GetLogicDevice(),bytes,VK_FORMAT_UNDEFINED,VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&readback.buffer.PersistentMap(),"Query readback allocation failed");
        VansComputeShader shader;shader.SetName("GIWorldForestQuery");shader.SetArtifactRoot(std::filesystem::current_path()/"Library/Artifacts/Shaders");
        Check(shader.InitShader(device.GetLogicDevice(),"Source/Tests/Shaders",{{VK_SHADER_STAGE_COMPUTE_BIT,"GIWorldLodBoundary.comp"}}),"Forest query shader failed");
        VansPipelineProgramDesc desc{};desc.kind=VansPipelineProgramKind::Compute;desc.pushConstantSize=32;
        shader.SetPipelineProgramDesc(desc);shader.SetPushConstant(32);
        auto& command=device.GetImmediateGraphicsCommandBuffer();
        Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Query recording failed");
        for(uint32_t i=0;i<rays;++i)
        {
            const float x=i<256?0.f:36.f;const uint32_t pixel=i%256;
            const glm::vec4 parameters[2]={{x-24.f,(float(pixel/16)+.5f)*2.5f,(float(pixel%16)+.5f)*1.5f-12.f,48.f},{1,0,0,0}};
            VkMemoryBarrier ready{VK_STRUCTURE_TYPE_MEMORY_BARRIER};ready.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
            ready.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{ready});
            command.EnsureComputeShader(shader,{world.Layout()});command.DispatchCompute(shader,3,1,1,{world.Descriptor()},parameters,sizeof(parameters));
            ready.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;ready.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,{ready});
            command.CopyBuffer(world.ScatterBuffer().GetNativeBuffer(),readback.buffer.GetNativeBuffer(),0,i*3*sizeof(glm::vec4),3*sizeof(glm::vec4));
        }
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,{host});Submit(device,command);
        readback.buffer.InvalidateMappedRange(0,bytes);std::vector<glm::vec4> values(rays*3);
        std::memcpy(values.data(),readback.buffer.GetMappedPtr(),bytes);return values;
    }
    void SaveQueries(const std::filesystem::path& directory,const std::string& label,const std::vector<glm::vec4>& values)
    {
        std::ofstream file(directory/(label+".bin"),std::ios::binary);file.write(reinterpret_cast<const char*>(values.data()),values.size()*sizeof(glm::vec4));
        Check(bool(file),"Query evidence write failed");
    }
    Json Compare(const std::vector<glm::vec4>& actual,const std::vector<glm::vec4>& reference)
    {
        Check(actual.size()==reference.size(),"Query size mismatch");float error=0;uint32_t surfaces=0,transmitted=0;
        for(size_t i=0;i<actual.size();++i)
        {
            for(uint32_t c=0;c<4;++c){Check(std::isfinite(actual[i][c])&&std::isfinite(reference[i][c]),"Non-finite forest result");error=std::max(error,std::abs(actual[i][c]-reference[i][c]));}
            if(i%3<2)Check(actual[i].z==1&&actual[i].w==0,"Forest query contains unresolved geometry");
            if(i%3==0){surfaces+=actual[i].x<48.f;transmitted+=actual[i].y<.999f;}
        }
        Check(error<=1e-5f,"Incremental field differs from fresh initialization");
        return {{"maxAbsoluteError",error},{"surfaceRays",surfaces},{"attenuatedRays",transmitted},{"rays",actual.size()/3}};
    }
    void Run(VansVKDevice& device,const Json& fixture,const std::filesystem::path& output,Json& report)
    {
        auto& profiler=Vans::VansProfiler::Get();profiler.SetCaptureEnabled(true);profiler.RegisterCurrentThread("ForestSource");
        Workers workers;auto& command=device.GetImmediateGraphicsCommandBuffer();
        std::map<std::string,std::unique_ptr<VansTexture>> textures;
        std::map<std::string,std::unique_ptr<VansMesh>> meshes;
        std::map<std::string,std::unique_ptr<VansPBRMaterial>> materials;
        auto plant=std::make_shared<Vans::VansPlantTypeAsset>();std::string error;
        Check(Vans::VansPlantTypeAssetStorage::Load(fixture.at("plant").get<std::string>(),*plant,error),error);
        const auto variants=fixture.at("variants").get<std::vector<std::string>>();Check(variants.size()==2,"Fixture requires two variants");
        for(const auto& input:fixture.at("textures"))
        {
            auto texture=std::make_unique<VansTexture>();VansTexture::TextureLoadDesc desc;
            desc.path=input.at("source");desc.cookedPath=input.at("cache");desc.isSRGB=input.at("srgb");desc.useCompress=input.at("compressed");
            desc.needMip=input.at("mips");desc.importChannel=input.at("channels");texture->LoadTexture(command,desc);
            Check(texture->GetWidth()>0&&texture->GetImage().GetImage(),"Forest texture upload failed");textures.emplace(input.at("guid"),std::move(texture));
        }
        uint64_t vertices=0,indices=0;
        for(const auto& input:fixture.at("models"))
        {
            auto mesh=std::make_unique<VansMesh>(false,false);const auto path=input.at("source").get<std::string>(),cache=input.at("cache").get<std::string>();
            if(input.at("multi").get<bool>())mesh->LoadMultiMesh(device.GetLogicDevice(),device.GetGraphicsQueue(),&command,path,input.at("tangents"),false,false,input.at("scale"),{},cache,true,false);
            else mesh->LoadMesh(device.GetLogicDevice(),device.GetGraphicsQueue(),&command,path,input.at("tangents"),cache,true,input.at("scale"));
            mesh->SetName(input.at("guid"));
            auto count=[&](VansMesh* part)
            {
                Check(part&&part->GetIndexCount()>0,"Forest mesh is empty");vertices+=part->GetMeshVertexCount();indices+=part->GetIndexCount();
                report["meshInputs"].push_back({{"guid",input.at("guid")},{"vertices",part->GetMeshVertexCount()},
                    {"indices",part->GetIndexCount()},{"stride",part->GetMeshVertexStride()}});
            };
            if(mesh->m_IsMultiMesh)for(auto* part:mesh->m_SubMeshes)count(part);else count(mesh.get());
            meshes.emplace(input.at("guid"),std::move(mesh));
        }
        for(const auto& input:fixture.at("materials"))
        {
            auto material=std::make_unique<VansPBRMaterial>();material->m_MaterialType=VansMaterialType::VAN_PBR;material->SetName(input.at("guid"));
            const auto color=input.at("albedo").get<std::array<float,3>>();material->m_BasePBRParam.m_albedo={color[0],color[1],color[2]};
            material->m_BasePBRParam.m_metallic=input.at("metallic");material->m_BasePBRParam.m_roughness=input.at("roughness");
            material->m_AlphaTestEnabled=input.at("alphaTest");material->m_AlphaCutoff=input.at("alphaCutoff");
            material->m_BaseColorTexture=textures.at(input.at("texture")).get();materials.emplace(input.at("guid"),std::move(material));
        }
        report["loaded"]={{"models",meshes.size()},{"materials",materials.size()},{"textures",textures.size()},{"verticesIncludingDrawLods",vertices},{"trianglesIncludingDrawLods",indices/3}};
        VansScene scene;for(auto& item:meshes)scene.AddMeshAsset(item.second.get());for(auto& item:materials)scene.AddMaterialAsset(item.second.get());
        scene.SetVegetationCollection(std::make_unique<VansVegetationCollection>());
        GIWorldSettings settings;settings.enabled=true;settings.voxelSize=.25f;settings.levelCount=5;settings.maxBricks=1024;
        settings.bricksPerFrame=64;settings.coverageDistance=128;settings.maxTraceSteps=256;
        VansGIWorld world;world.Initialize(device,scene,settings,false,4096,scene.GetMaterialManager()->GetGIVoxelMaterialInputs());
        const auto descriptor=world.Descriptor();const auto generation=world.QueryPublication().generation;const auto bytes=world.BufferCapacityBytes();
        report["worldBufferCapacityBytes"]=bytes;
        report["enqueues"]=Json::array();auto& enqueues=report["enqueues"];
        scene.GetGIVoxelSourceManager().Bind([&](GIVoxelSourceChanges changes)
        {
            CaptureProfile profile;
            const auto start=Clock::now();EnqueueObserver observer;
            const size_t updated=changes.updated.size(),removed=changes.removed.size();
            const bool accepted=world.QueueSourceChanges(std::move(changes));
            enqueues.push_back({{"milliseconds",Milliseconds(start)},{"updated",updated},{"removed",removed},
                {"submissions",observer.submissions},{"fenceWaits",observer.waits},{"deviceIdleWaits",observer.idles},
                {"shaderModules",observer.shaderModules},{"accepted",accepted}});
            Check(accepted&&observer.waits==0&&observer.idles==0,"Forest capture blocked or rejected incrementality");
            Check(observer.shaderModules==0,"Source enqueue rebuilt its prepared shader");
            profile.Finish(report);return accepted;
        });
        struct SourceBinding
        {
            VansScene& scene;
            ~SourceBinding(){scene.GetGIVoxelSourceManager().Clear();}
        } binding{scene};
        auto checkpoint=[&](){return VansVKMemoryAllocator::Get().QueryMemorySnapshot().allocationBytes;};
        auto pump=[&](VansGIWorld& field,Json& metrics)
        {
            CaptureProfile profile;
            if(metrics.is_null())metrics=Json::object();
            auto start=Clock::now();field.PrepareUpdates();metrics["maximumPrepareMs"]=std::max(metrics.value("maximumPrepareMs",0.0),Milliseconds(start));
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"World recording failed");
            VansGIWorldUploadWork work;
            {
                EnqueueObserver observer;start=Clock::now();work=field.RecordUpdates(command);
                metrics["maximumRecordMs"]=std::max(metrics.value("maximumRecordMs",0.0),Milliseconds(start));
                metrics["captureShaderModules"]=metrics.value("captureShaderModules",uint64_t(0))+observer.shaderModules;
                Check(observer.shaderModules==0,"Queued source capture rebuilt its prepared shader");
            }
            metrics["uploadedBricks"]=metrics.value("uploadedBricks",uint64_t(0))+work.uploadedBricks;
            metrics["uploadBytes"]=metrics.value("uploadBytes",uint64_t(0))+work.UploadBytes();Submit(device,command);
            metrics["observedAllocationPeakBytes"]=std::max(metrics.value("observedAllocationPeakBytes",uint64_t(0)),checkpoint());
            profile.Finish(report);
        };
        auto drain=[&](VansGIWorld& field,Json& metrics)
        {
            auto start=Clock::now();uint32_t frames=0;const auto sequence=field.QueryPublication().sequence;
            while(field.HasPendingUpdates())
            {
                pump(field,metrics);++frames;
                Check(!field.SourcesDirty(),"Forest source update requested an unexpected full rebuild");
                if(!metrics.contains("firstPublicationMs")&&field.QueryPublication().sequence!=sequence)
                {
                    Check(field.CpuWorkStats().templateBakes>=metrics.value("requiredBakesAtPublication",uint64_t(0)),
                        "A source batch published before all required templates were prepared");
                    metrics["firstPublicationMs"]=Milliseconds(start);
                }
                Check(Milliseconds(start)<60000,"Forest source update did not converge");
                std::this_thread::sleep_until(start+std::chrono::milliseconds(16*frames));
            }
            metrics["drainMilliseconds"]=Milliseconds(start);metrics["pumpFrames"]=frames;
        };
        auto apply=[&](std::vector<std::pair<uint32_t,float>> trees,std::shared_ptr<const Vans::VansPlantTypeAsset> definition)
        {
            Vans::VansPcgBatchUpdate update;update.region="forest-contract";update.layer="trees";update.replaceAll=true;update.cellSize=16;
            for(size_t i=0;i<trees.size();++i)
            {
                auto batch=std::make_shared<Vans::VansPcgBatchSource>();batch->plant=definition;
                batch->key={update.region,update.layer,variants.at(trees[i].first),int64_t(std::floor(trees[i].second/16)),0};
                Vans::VansPcgPoint point;point.id=i+1;point.position={trees[i].second,0,0};point.scale={1,1,1};point.rotation={0,0,0,1};batch->points.push_back(point);
                update.batches.emplace(batch->key,batch);
            }
            const auto start=Clock::now();Check(scene.GetVegetationCollection()->Apply(scene,device,update,error),error);return Milliseconds(start);
        };
        auto verify=[&](const std::string& name)
        {
            Check(world.Descriptor()==descriptor&&world.QueryPublication().generation==generation&&world.BufferCapacityBytes()==bytes,"Forest update rebuilt resources");
            const auto actual=Query(device,world);SaveQueries(output,name,actual);
            VansGIWorld reference;reference.Initialize(device,scene,settings,false,4096,scene.GetMaterialManager()->GetGIVoxelMaterialInputs());Json metrics;drain(reference,metrics);
            const auto expected=Query(device,reference);SaveQueries(output,name+"-reference",expected);
            report["stages"][name]["equivalence"]=Compare(actual,expected);
            const auto cpu=world.CpuWorkStats();
            report["cpuWork"]={{"templateBakes",cpu.templateBakes},{"captureDecodes",cpu.captureDecodes},
                {"submissionThreadTemplateBakes",cpu.submissionThreadTemplateBakes},{"submissionThreadCaptureDecodes",cpu.submissionThreadCaptureDecodes},
                {"bakeMilliseconds",cpu.templateBakeMilliseconds},{"decodeMilliseconds",cpu.captureDecodeMilliseconds},
                {"captureSubmissions",cpu.captureSubmissions},{"peakCaptureInputBytes",cpu.peakCaptureInputBytes}};
            Check(report["stages"][name]["equivalence"]["surfaceRays"].get<uint32_t>()>0||
                report["stages"][name]["equivalence"]["attenuatedRays"].get<uint32_t>()>0,"Forest query did not sample tree geometry");
            return actual;
        };
        Json initial;drain(world,initial);const auto empty=Query(device,world);SaveQueries(output,"empty",empty);
        for(uint32_t phase=0;phase<2;++phase)
        {
            const std::string name=phase?"replace":"add";auto& metrics=report["stages"][name];
            const auto previous=Query(device,world);
            metrics["baselineAllocationBytes"]=checkpoint();metrics["applyMilliseconds"]=apply({{phase,0},{0,36}},plant);
            metrics["afterApplyAllocationBytes"]=checkpoint();Check(Query(device,world)==previous,"Unpublished edit changed old field");
            drain(world,metrics);verify(name);
        }
        auto& edits=report["stages"]["continuous"];edits=Json::object();
        for(uint32_t i=1;i<=12;++i){edits["applyMilliseconds"]=edits.value("applyMilliseconds",0.0)+apply({{1,float(i)},{0,36}},plant);pump(world,edits);std::this_thread::sleep_for(std::chrono::milliseconds(16));}
        drain(world,edits);const auto latest=verify("continuous");
        auto clone=std::make_shared<Vans::VansPlantTypeAsset>(*plant);
        auto& multi=report["stages"]["multi-template"];
        multi["requiredBakesAtPublication"]=4;multi["applyMilliseconds"]=apply({{1,24},{0,48}},clone);
        Check(Query(device,world)==latest,"Queued multi-template update changed the published field");
        drain(world,multi);const auto multiLatest=verify("multi-template");
        Check(world.CpuWorkStats().captureSubmissions==4&&world.CpuWorkStats().captureDecodes==4,
            "The large two-template source update was not split into bounded submissions");
        auto cancelledClone=std::make_shared<Vans::VansPlantTypeAsset>(*plant);const auto beforeCancel=world.CpuWorkStats();
        auto& cancel=report["stages"]["cancel"];
        const auto clonedApply=apply({{1,24},{0,48}},cancelledClone);
        cancel["applyMilliseconds"]=clonedApply+apply({{1,24},{0,48}},clone);drain(world,cancel);
        Check(world.CpuWorkStats().captureDecodes==beforeCancel.captureDecodes&&world.CpuWorkStats().templateBakes==beforeCancel.templateBakes,"Cancelled forest batch was decoded or baked");
        Compare(Query(device,world),multiLatest);verify("cancel");
        auto& removed=report["stages"]["remove"];removed["applyMilliseconds"]=apply({},plant);drain(world,removed);Compare(Query(device,world),empty);
        const auto cpu=world.CpuWorkStats();Check(cpu.templateBakes==4&&cpu.captureDecodes==4&&cpu.submissionThreadTemplateBakes==0&&cpu.submissionThreadCaptureDecodes==0,"Forest template work was duplicated or ran inline");
        Check(report["captureSetupMilliseconds"].size()==cpu.captureSubmissions,"Capture setup profiling missed or repeated a submission");
        Check(cpu.peakCaptureInputBytes<=80u*1024u*1024u,"Active capture inputs exceeded the fixed budget");
        report["cpuWork"]={{"templateBakes",cpu.templateBakes},{"captureDecodes",cpu.captureDecodes},{"bakeMilliseconds",cpu.templateBakeMilliseconds},
            {"decodeMilliseconds",cpu.captureDecodeMilliseconds},{"captureSubmissions",cpu.captureSubmissions},{"peakCaptureInputBytes",cpu.peakCaptureInputBytes}};
        report["worldBufferCapacityBytes"]=bytes;report["fixedResources"]=true;
        // 两个真实模板超过单批预算，拒绝第二批，验证已经烘焙的第一候选也一并撤回。
        auto& rollbackSetup=report["stages"]["coverage-rollback-setup"];
        apply({{1,24},{0,48},{0,-12}},clone);drain(world,rollbackSetup);
        const auto originalMaterials=scene.GetMaterialManager()->GetGIVoxelMaterialInputs();
        std::vector<VansTexture*> bindings;
        for(const auto& item:*originalMaterials)bindings.push_back(item.second.baseColorTexture);
        Check(bindings.size()==3 && std::set<VansTexture*>(bindings.begin(),bindings.end()).size()==3,
            "Coverage rollback fixture requires three distinct existing texture bindings");
        const auto changedBindings=[&](uint32_t shift)
        {
            auto input=std::make_shared<VansGIVoxelMaterialInputs>(*originalMaterials);size_t index=0;
            for(auto& item:*input){item.second.baseColorTexture=bindings[(index+shift)%bindings.size()];++index;}
            return input;
        };
        for(uint32_t edit=0;edit<2;++edit)
        {
            const std::string name=edit?"coverage-rollback-source-edits":"coverage-rollback-unchanged";
            auto& metrics=report["stages"][name];const auto previous=Query(device,world);
            const auto before=world.CpuWorkStats();const auto templateCount=world.SourceTemplateCount();
            const auto sequence=world.QueryPublication().sequence;
            world.ApplyMaterials(changedBindings(edit+1));pump(world,metrics);
            Check(world.CpuWorkStats().captureSubmissions==before.captureSubmissions+1,
                "Coverage rollback fixture did not submit its first batch");
            if(edit)apply({{1,25},{0,49},{1,8}},clone);
            uint32_t rejected=0;const auto start=Clock::now();
            while(!rejected)
            {
                world.PrepareUpdates();
                Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Coverage rollback recording failed");
                {
                    EnqueueObserver observer;observer.rejectSubmit=true;world.RecordUpdates(command);rejected+=observer.submissions;
                    Check(observer.waits==0 && observer.idles==0,"Coverage rollback waited for GPU completion");
                }
                Submit(device,command);
                Check(!world.SourcesDirty(),"Coverage failure requested a full source rebuild");
                Check(Milliseconds(start)<60000,"Second coverage capture was not exercised");
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
            Check(rejected==1 && world.CpuWorkStats().templateBakes==before.templateBakes+1,
                "Coverage rollback did not exercise a previously baked candidate");
            Compare(Query(device,world),previous);drain(world,metrics);
            Check(world.SourceTemplateCount()==templateCount && world.CpuWorkStats().captureSubmissions==before.captureSubmissions+1,
                "Failed coverage accumulated templates or retried without another edit");
            if(!edit)Check(world.QueryPublication().sequence==sequence && world.CpuWorkStats().pagePlanJobs==before.pagePlanJobs,
                "Coverage rollback rebuilt unchanged published pages");
            verify(name);metrics["rejectedSubmissions"]=rejected;metrics["sourceRebuildRequested"]=false;
            metrics["preservedConcurrentSourceEdits"]=bool(edit);
        }
        {
            auto& metrics=report["stages"]["coverage-rollback-decode"];
            const auto before=world.CpuWorkStats();const auto templateCount=world.SourceTemplateCount();
            std::set<VkBuffer> indexBuffers;
            for(const auto& item:meshes)
                if(item.second->m_IsMultiMesh)for(auto* part:item.second->m_SubMeshes)indexBuffers.insert(part->GetIndexBuffer().GetNativeBuffer());
                else indexBuffers.insert(item.second->GetIndexBuffer().GetNativeBuffer());
            world.ApplyMaterials(changedBindings(1));
            Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Decode failure recording failed");
            {
                EnqueueObserver observer;observer.corruptIndexBuffers=&indexBuffers;world.RecordUpdates(command);
                Check(observer.corruptedCopies>0 && observer.waits==0 && observer.idles==0,"Decode failure injection did not capture geometry");
                metrics["corruptedReadbackCopies"]=observer.corruptedCopies;
            }
            Submit(device,command);drain(world,metrics);verify("coverage-rollback-decode");
            Check(world.SourceTemplateCount()==templateCount && world.CpuWorkStats().templateBakes==before.templateBakes &&
                world.CpuWorkStats().captureSubmissions==before.captureSubmissions+1,"Decode failure did not discard its bounded coverage candidate");
            metrics["sourceRebuildRequested"]=false;
        }
        // 后续实际材质编辑仍可成功发布；新建场使用同一 owner 输入作为对照。
        const auto retryInput=changedBindings(2);
        for(auto& item:materials)
        {
            auto& material=*item.second;
            auto* manager=scene.GetMaterialManager();
            material.m_BaseColorTexture=retryInput->at(&material).baseColorTexture;manager->FlushMaterialPayload(material);
        }
        world.ApplyMaterials(scene.GetMaterialManager()->GetGIVoxelMaterialInputs());
        drain(world,report["stages"]["coverage-rollback-retry"]);verify("coverage-rollback-retry");
        scene.GetGIVoxelSourceManager().Clear();
        {
            VansGIWorld failed;failed.Initialize(device,scene,settings,false,4096,scene.GetMaterialManager()->GetGIVoxelMaterialInputs());Json initialFailure;
            drain(failed,initialFailure);const auto previous=Query(device,failed);
            const auto failedDescriptor=failed.Descriptor();const auto failedGeneration=failed.QueryPublication().generation;
            scene.GetGIVoxelSourceManager().Bind([&](GIVoxelSourceChanges changes){return failed.QueueSourceChanges(std::move(changes));});
            SourceBinding failedBinding{scene};
            auto& metrics=report["stages"]["queued-submit-failure"];
            metrics["applyMilliseconds"]=apply({{1,24},{0,48}},std::make_shared<Vans::VansPlantTypeAsset>(*plant));
            Check(failed.CpuWorkStats().captureSubmissions==1,"Failure fixture did not submit its first batch");
            const auto start=Clock::now();uint32_t rejected=0;
            while(!failed.SourcesDirty())
            {
                failed.PrepareUpdates();
                Check(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Failure fixture recording failed");
                const auto allocationBefore=checkpoint();
                {
                    EnqueueObserver observer;observer.rejectSubmit=true;
                    failed.RecordUpdates(command);rejected+=observer.submissions;
                    Check(observer.waits==0&&observer.idles==0,"Queued submission failure blocked the render thread");
                    if(observer.submissions)Check(checkpoint()==allocationBefore,"Failed queued capture retained GPU allocations");
                }
                Submit(device,command);
                Check(Milliseconds(start)<60000,"Queued capture failure was never exercised");
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
            Check(rejected==1 && failed.CpuWorkStats().captureSubmissions==1 && failed.Descriptor()==failedDescriptor &&
                failed.QueryPublication().generation==failedGeneration,"Queued failure rebuilt or partially submitted the world");
            metrics["equivalence"]=Compare(Query(device,failed),previous);metrics["rejectedSubmissions"]=rejected;
            metrics["sourceRebuildRequested"]=true;metrics["previousFieldRetained"]=true;
        }
        Check(device.DrainDeferredDeletesAfterDeviceIdle(),"Forest draw resource retirement failed");
    }
}

bool TestGIWorldForestSourceGpuContract(const char* fixturePath,const char* outputPath)
{
    Json report;const std::filesystem::path output(outputPath);bool createdOutput=false;
    try
    {
        Check(!std::filesystem::exists(output),"Use a fresh forest evidence directory");std::filesystem::create_directories(output);createdOutput=true;
        const auto fixture=Read(fixturePath);validationErrors=0;
        _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
        Check(m_GraphicsDevice==nullptr,"Forest fixture requires its own native process");
        Window window;Check(glfwInit()==GLFW_TRUE,"GLFW failed");glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        window.handle=glfwCreateWindow(64,64,"GI forest source contract",nullptr,nullptr);Check(window.handle!=nullptr,"Hidden window failed");
        auto device=std::make_unique<VansVKDevice>(VkExtent2D{64,64},&window);Check(device->IsInitialized(),"Vulkan device failed");
        m_GraphicsDevice=device.get();
        Messenger messenger;report["validationEnabled"]=false;
#ifdef _DEBUG
        const char* layers=std::getenv("VK_INSTANCE_LAYERS");Check(layers&&std::string(layers).find("VK_LAYER_KHRONOS_validation")!=std::string::npos,"Debug requires validation layer");
        messenger.instance=device->GetInstance();const auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(messenger.instance,"vkCreateDebugUtilsMessengerEXT"));
        messenger.destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(messenger.instance,"vkDestroyDebugUtilsMessengerEXT"));Check(create&&messenger.destroy,"Validation messenger unavailable");
        VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};info.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;info.pfnUserCallback=Validate;
        Check(create(messenger.instance,&info,nullptr,&messenger.handle)==VK_SUCCESS,"Validation messenger failed");report["validationEnabled"]=true;
#endif
        auto& shaders=VansShaderManager::Get();shaders.RegisterComputeShader("GIPointLight","EngineAssets/Shaders/GIPointLight",sizeof(VansGIProbeDispatchParameters));
        Check(shaders.LoadAll(std::filesystem::current_path().generic_string()+"/",std::filesystem::current_path()/"Library/Artifacts/Shaders",device->GetLogicDevice()),"GI shader bootstrap failed");
        Run(*device,fixture,output,report);Check(validationErrors==0,"Forest source validation errors");
        if(messenger.handle){messenger.destroy(messenger.instance,messenger.handle,nullptr);messenger.handle=VK_NULL_HANDLE;}
        device.reset();m_GraphicsDevice=nullptr;report["passed"]=true;report["validationErrors"]=validationErrors;
        report["scope"]="Real forest mesh/texture inputs through PCG collection and GI source manager; 16 ms paced GI-only publication, not full-frame or editor-brush latency";
        std::ofstream(output/"result.json")<<report.dump(2);
        std::cout<<"[GIForestSourceGPU] PASS: real tree sources, replacement, continuous edits, cancellation, fresh-field GPU equivalence; validationErrors="<<validationErrors<<std::endl;
        return true;
    }
    catch(const std::exception& error)
    {
        report["passed"]=false;report["error"]=error.what();report["validationErrors"]=validationErrors;
        m_GraphicsDevice=nullptr;
        if(createdOutput)std::ofstream(output/"result.json")<<report.dump(2);
        std::cerr<<"[GIForestSourceGPU] FAIL: "<<error.what()<<std::endl;return false;
    }
}
