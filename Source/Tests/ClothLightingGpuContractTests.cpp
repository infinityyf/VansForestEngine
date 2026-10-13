#define VK_NO_PROTOTYPES
#include "../Graphics/Vulkan/VansVKFunctions.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../EngineCore/RenderCore/VulkanCore/VansTexture.h"
#include "../EngineCore/RenderCore/BRDFData/VansClothLightingLUT.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/RenderCore/VansRenderSystem.h"
#include "../EngineCore/EngineAPILayer/Private/EngineAPIImpl.h"
#include "../EngineCore/EditorCore/VansEditorRuntimePreviewProjector.h"
#include "../EngineCore/EditorCore/VansEditorMaterialSchemaService.h"
#include "../EngineCore/AuthoringCore/VansAssetDocumentRegistry.h"
#include "../EngineCore/AuthoringCore/VansAssetDocumentEditService.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../EngineCore/AssetCore/Storage/VansFileStorage.h"
#include "../EngineCore/ProjectSystem/VansProjectManager.h"
#include "../EngineCore/Interfaces/INativeWindowProvider.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <stdexcept>
#include <thread>

namespace
{
using namespace VansGraphics;
using Json=nlohmann::json;
void Require(bool value,const char* message)
{
    if(!value) throw std::runtime_error(message);
}
struct HiddenWindow : INativeWindowProvider
{
    GLFWwindow* handle=nullptr;
    void* GetNativeWindowHandle() const override { return handle; }
    ~HiddenWindow() { if(handle) glfwDestroyWindow(handle);glfwTerminate(); }
};
struct GpuScope
{
    HiddenWindow window;
    std::unique_ptr<VansVKDevice> device;
    VansVKBuffer input,output;
    std::unique_ptr<VansTexture> energy,lighting;
    VkDescriptorSetLayout setLayout{};
    std::vector<VkDescriptorSet> sets;
    VkShaderModule module{};
    VkPipelineLayout pipelineLayout{};
    VkPipeline pipeline{};
    ~GpuScope()
    {
        if(!device) return;
        if(!device->IsInitialized()) { device.reset();m_GraphicsDevice=nullptr;return; }
        auto logical=device->GetLogicDevice();vkDeviceWaitIdle(logical);
        if(pipeline) vkDestroyPipeline(logical,pipeline,nullptr);
        if(pipelineLayout) vkDestroyPipelineLayout(logical,pipelineLayout,nullptr);
        if(module) vkDestroyShaderModule(logical,module,nullptr);
        auto* descriptors=VansVKDescriptorManager::GetInstance();
        descriptors->DestroyDescriptorSet(sets);descriptors->ReleaseDescriptorSetLayout(setLayout);
        energy.reset();lighting.reset();
        input.DestroyVulkanBuffer(logical);output.DestroyVulkanBuffer(logical);
        device.reset();m_GraphicsDevice=nullptr;
    }
    void Initialize()
    {
        Require(glfwInit()!=0,"GLFW initialization");
        glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        window.handle=glfwCreateWindow(64,64,"Cloth lighting GPU contract",nullptr,nullptr);
        Require(window.handle!=nullptr,"hidden Cloth GPU window");
        device=std::make_unique<VansVKDevice>(VkExtent2D{64,64},&window);
        Require(device->IsInitialized(),"Vulkan initialization");m_GraphicsDevice=device.get();
    }
    void Buffer(VansVKBuffer& buffer,size_t bytes,const void* data)
    {
        Require(buffer.CreatVulkanBuffer(device->GetLogicDevice(),bytes,VK_FORMAT_R32_UINT,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&
            buffer.PersistentMap(),"Cloth GPU buffer allocation");
        if(data) std::memcpy(buffer.GetMappedPtr(),data,bytes);else std::memset(buffer.GetMappedPtr(),0,bytes);
    }
};
struct Case
{
    glm::vec4 view,surface,extra,center,right,up;
};
static_assert(sizeof(Case)==96,"Cloth GPU case layout");

// Runs the production transaction queue without creating a frame or scene Pass.
// All material images and descriptor retirement use the real Vulkan device.
class MaterialTransactionBackend final : public VansGraphicsDevice
{
public:
    explicit MaterialTransactionBackend(VansVKDevice& device) : device(device)
    { m_RenderWidth=m_RenderHeight=64;m_GraphicsAPI=VULKAN; }
    bool BeforeRendering() override { return true; }
    void Rendering() override { throw std::runtime_error("Material transaction test produced a frame"); }
    bool CurrentFrameRequiresSurfaceResize() const override { return false; }
    bool HasDeviceLost() const override { return device.HasDeviceLost(); }
    void Present() override {}
    void AfterRendering() override {}
    bool WaitForIdle() override { return device.WaitForIdle(); }
    void RetireAfterCurrentFrame(std::function<void()> destroy) override { destroy(); }
    bool OnWindowResize(uint32_t,uint32_t) override { return false; }
    void* GetNativeGraphicsDevice() override { return device.GetNativeGraphicsDevice(); }
    void* GetNativeCommandBuffer() override { return device.GetNativeCommandBuffer(); }
private:
    VansVKDevice& device;
};
class NoMaterialFrameSource final : public IVansRenderFrameSource
{
    std::optional<VansRenderFrameSourceOutput> PrepareMainThreadRenderFrame(
        const VansRenderFramePreparationContext&) override { return std::nullopt; }
};
class ClothDescriptorBuildTransaction final : public IVansRenderThreadTransaction
{
public:
    explicit ClothDescriptorBuildTransaction(VansClothMaterial& material) : material(material) {}
    bool Execute(VansGraphicsDevice&) override
    {
        material.BuildClothTextureDescriptors();
        return material.m_ClothOwnedLayout!=VK_NULL_HANDLE&&material.m_ClothOwnedDescSets.size()==1;
    }
private:
    VansClothMaterial& material;
};
PFN_vkDeviceWaitIdle materialOriginalWaitIdle=nullptr;
PFN_vkUpdateDescriptorSets materialOriginalUpdateDescriptors=nullptr;
uint32_t materialIdleCalls=0;
std::thread::id materialIdleThread;
VkDescriptorSet materialTrackedSet=VK_NULL_HANDLE;
std::array<VkImageView,7> materialDescriptorViews{};
uint32_t materialDescriptorWrites=0;
VKAPI_ATTR VkResult VKAPI_CALL CountMaterialWaitIdle(VkDevice device)
{
    ++materialIdleCalls;materialIdleThread=std::this_thread::get_id();
    return materialOriginalWaitIdle(device);
}
VKAPI_ATTR void VKAPI_CALL TrackMaterialDescriptorUpdate(VkDevice device,uint32_t count,
    const VkWriteDescriptorSet* writes,uint32_t copyCount,const VkCopyDescriptorSet* copies)
{
    for(uint32_t i=0;i<count;++i)
        if(writes[i].dstSet==materialTrackedSet&&writes[i].dstBinding<materialDescriptorViews.size()&&
            writes[i].descriptorType==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER&&writes[i].descriptorCount==1)
        {
            materialDescriptorViews[writes[i].dstBinding]=writes[i].pImageInfo[0].imageView;
            ++materialDescriptorWrites;
        }
    materialOriginalUpdateDescriptors(device,count,writes,copyCount,copies);
}
double Percentile(std::vector<double> values,double p)
{
    std::sort(values.begin(),values.end());double x=(values.size()-1)*p;
    auto i=static_cast<size_t>(x);return values[i]+(values[std::min(i+1,values.size()-1)]-values[i])*(x-i);
}
}

bool TestClothLightingGpuContract(const char* referenceDirectory,const char* reportPath)
{
    try
    {
        std::vector<Case> cases;std::vector<glm::dvec3> expected;std::vector<std::string> suites,names;
        for(const char* suffix:{"","-stress","-random"})
        {
            auto path=std::filesystem::path(referenceDirectory)/(std::string("cloth-rectangle")+suffix+"-reference.json");
            std::ifstream file(path);Require(bool(file),"Cloth reference file");Json document;file>>document;
            for(const auto& row:document.at("results"))
            {
                const auto& p=row.at("parameters");Case c{};
                c.view=glm::vec4(row.at("nov").get<float>(),row.at("phi").get<float>(),0,0);
                c.surface=glm::vec4(p.at("roughness").get<float>(),p.at("anisotropy").get<float>(),
                    p.at("sheen_roughness").get<float>(),p.at("sheen_weight").get<float>());
                c.extra=glm::vec4(p.at("specular_weight").get<float>(),p.at("tau").get<float>(),0,0);
                for(int axis=0;axis<3;++axis) c.center[axis]=row.at("center").at(axis).get<float>();
                c.right=glm::vec4(1,0,0,row.at("half_size").at(0).get<float>());
                c.up=glm::vec4(0,1,0,row.at("half_size").at(1).get<float>());
                if(!row.at("rotation").is_null()) for(int axis=0;axis<3;++axis)
                {
                    c.right[axis]=row.at("rotation").at(axis).at(0).get<float>();
                    c.up[axis]=row.at("rotation").at(axis).at(1).get<float>();
                }
                glm::dvec3 truth;for(int axis=0;axis<3;++axis) truth[axis]=row.at("reference").at(axis).get<double>();
                // 同时覆盖四个视角象限；对光源和视向施加同一轴反射，光学结果应等价。
                for(int quadrant=0;quadrant<4;++quadrant)
                {
                    Case mirrored=c;float sx=(quadrant&1)?-1.f:1.f,sy=(quadrant&2)?-1.f:1.f;
                    mirrored.view.y=std::atan2(sy*std::sin(c.view.y),sx*std::cos(c.view.y));
                    mirrored.center.x*=sx;mirrored.center.y*=sy;
                    mirrored.right.x*=sx;mirrored.right.y*=sy;mirrored.up.x*=sx;mirrored.up.y*=sy;
                    cases.push_back(mirrored);expected.push_back(truth);suites.push_back(suffix);names.push_back(row.at("material").get<std::string>());
                }
            }
        }
        Require(!cases.empty(),"Cloth reference cases");
        const size_t referenceCases=cases.size();
        for(float nv:{.0001f,.015f,.05f,.5f,1.f}) for(float r:{.045f,.15f,.35f,.7f,1.f})
        for(float anisotropy:{0.f,.5f,.95f}) for(float sr:{.045f,.15f,.35f,.7f,1.f})
        for(float sw:{0.f,.5f,1.f}) for(float spec:{0.f,.5f,1.f}) for(float tau:{0.f,.7f,1.f})
        {
            Case c{};c.view=glm::vec4(nv,.73f,0,0);c.surface=glm::vec4(r,anisotropy,sr,sw);
            c.extra=glm::vec4(spec,tau,0,0);c.right=glm::vec4(1,0,0,100000);c.up=glm::vec4(0,1,0,100000);
            for(float z:{1.f,-1.f})
            {
                c.center=glm::vec4(0,0,z,0);cases.push_back(c);expected.emplace_back(0);
                suites.emplace_back("white-furnace");names.emplace_back("white-furnace");
            }
        }
        const size_t lightingCases=cases.size();
        // Decal changes the shading normal, not the authored world fiber.
        // The independent expected value is its geometric projection. Include
        // normals on both sides of the stable-frame helper-axis seam.
        for(const glm::dvec3 rawNormal:{glm::dvec3(0,0,1),glm::dvec3(0,1,0),
            glm::dvec3(.6,0,.8),glm::dvec3(.035,0,.9994)})
        for(int angle=0;angle<8;++angle)
        {
            const glm::dvec3 sourceNormal=glm::normalize(rawNormal);
            const double phi=angle*3.141592653589793/4.;
            glm::dvec3 fiber(std::cos(phi),std::sin(phi),.37);
            fiber=glm::normalize(fiber-sourceNormal*glm::dot(sourceNormal,fiber));
            for(const glm::dvec3 rawShading:{sourceNormal,glm::dvec3(0,0,1),glm::dvec3(0,1,0),
                glm::dvec3(.02,0,.9998),glm::dvec3(.06,0,.9982),glm::dvec3(.7,-.3,.64),fiber})
            for(int half=0;half<2;++half)
            {
                const glm::dvec3 shadingNormal=glm::normalize(rawShading);
                const glm::dvec3 projected=fiber-shadingNormal*glm::dot(shadingNormal,fiber);
                const bool degenerate=glm::dot(projected,projected)<=1e-8;
                Case c{};c.extra.z=1;c.extra.w=degenerate?-1.f:(half?.004f:.0001f);
                c.view.z=static_cast<float>(half);c.center=glm::vec4(sourceNormal,0);
                c.right=glm::vec4(shadingNormal,0);c.up=glm::vec4(fiber,0);
                cases.push_back(c);expected.push_back(degenerate?glm::dvec3(0):glm::normalize(projected));
                suites.emplace_back("fiber-frame");names.emplace_back(half?"R16F angle":"FP32 angle");
            }
        }
        GpuScope gpu;gpu.Initialize();
        gpu.Buffer(gpu.input,cases.size()*sizeof(Case),cases.data());gpu.Buffer(gpu.output,cases.size()*sizeof(glm::vec4),nullptr);
        auto& command=gpu.device->GetImmediateGraphicsCommandBuffer();
        VansTexture::ResetUploadFailureCount();gpu.energy=std::make_unique<VansTexture>();
        VansTexture::TextureLoadDesc energyDesc{};energyDesc.path="EngineAssets/Textures/ClothBRDFLUT.png";
        energyDesc.isSRGB=false;energyDesc.importChannel=4;energyDesc.addressMode=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        gpu.energy->LoadTexture(command,energyDesc);gpu.lighting=std::make_unique<VansTexture>();
        gpu.lighting->LoadFromMemory(command,ClothLightingLUT::Pixels,sizeof(ClothLightingLUT::Pixels),
            ClothLightingLUT::Width,ClothLightingLUT::Height,VK_FORMAT_R32G32B32A32_SFLOAT);
        Require(VansTexture::GetUploadFailureCount()==0,"Cloth LUT uploads");
        auto* descriptors=VansVKDescriptorManager::GetInstance();
        std::vector<VkDescriptorSetLayoutBinding> bindings={
            {0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {8,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {44,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
        Require(descriptors->CreateDesciptorSetLayout(bindings,gpu.setLayout)&&
            descriptors->AllocateDescriptorSet({gpu.setLayout},gpu.sets),"Cloth descriptors");
        descriptors->BeginDescriptorUpdate();
        for(uint32_t i=0;i<2;++i)
        {
            auto& buffer=i==0?gpu.input:gpu.output;
            descriptors->WriteBufferDescriptor(gpu.sets[0],i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                {{buffer.GetNativeBuffer(),0,buffer.GetBufferSize()}});
        }
        for(auto pair:{std::pair<uint32_t,VansTexture*>{8,gpu.energy.get()},{44,gpu.lighting.get()}})
            descriptors->WriteImageDescriptor(gpu.sets[0],pair.first,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {{pair.second->GetImage().GetSampler(),pair.second->GetImage().GetImageView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
        descriptors->CommitDescriptorUpdates();
        std::ifstream file("Source/Tests/Shaders/ClothLightingContractcomp.spv",std::ios::binary|std::ios::ate);
        Require(bool(file),"Cloth GPU shader");size_t bytes=static_cast<size_t>(file.tellg());std::vector<uint32_t> code(bytes/4);
        file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),bytes);Require(bool(file),"Cloth GPU shader read");
        auto device=gpu.device->GetLogicDevice();VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module.codeSize=bytes;module.pCode=code.data();Require(vkCreateShaderModule(device,&module,nullptr,&gpu.module)==VK_SUCCESS,"Cloth shader module");
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&gpu.setLayout;
        Require(vkCreatePipelineLayout(device,&layout,nullptr,&gpu.pipelineLayout)==VK_SUCCESS,"Cloth pipeline layout");
        VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pipeline.layout=gpu.pipelineLayout;
        pipeline.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,gpu.module,"main",nullptr};
        Require(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pipeline,nullptr,&gpu.pipeline)==VK_SUCCESS,"Cloth compute pipeline");
        Require(command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT),"Cloth command begin");
        auto native=command.GetVKCommandBuffer();VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        host.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;host.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(native,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&host,0,nullptr,0,nullptr);
        vkCmdBindPipeline(native,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.pipeline);
        vkCmdBindDescriptorSets(native,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.pipelineLayout,0,1,gpu.sets.data(),0,nullptr);
        vkCmdDispatch(native,static_cast<uint32_t>((cases.size()+63)/64),1,1);
        host.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(native,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
        Require(command.EndCommandBufferRecord()&&VansVKCommandBuffer::SubmitCommands(gpu.device->GetGraphicsQueue(),device,
            {native},{},{},command.m_CommandBufferFinishSubmitFence)&&command.ResetCommandBuffer(false),"Cloth GPU submit/readback");
        auto* values=static_cast<const glm::vec4*>(gpu.output.GetMappedPtr());Json report;report["cases"]=cases.size();
        std::map<std::string,std::vector<double>> errors;double maxAbsolute=0;
        double maximumWhiteEnergy=0,maximumFiberError=0,maximumFiberPerpendicularError=0,maximumFiberUnitError=0;
        bool fiberPassed=true;
        for(size_t i=0;i<cases.size();++i)
        {
            if(i>=lightingCases)
            {
                const glm::dvec3 actual(values[i]);
                Require(std::isfinite(actual.x)&&std::isfinite(actual.y)&&std::isfinite(actual.z),"finite Cloth fiber frame");
                const double perpendicular=std::abs(glm::dot(actual,glm::dvec3(cases[i].right)));
                const double unitError=std::abs(glm::length(actual)-1.);
                // Fibers are unoriented axes: T and -T represent the same
                // optical direction, including the doubled-angle texture.
                const double error=cases[i].extra.w<0?0.:std::min(
                    glm::length(actual-expected[i]),glm::length(actual+expected[i]));
                maximumFiberError=std::max(maximumFiberError,error);
                maximumFiberPerpendicularError=std::max(maximumFiberPerpendicularError,perpendicular);
                maximumFiberUnitError=std::max(maximumFiberUnitError,unitError);
                fiberPassed=fiberPassed&&perpendicular<.0001&&unitError<.0001&&
                    (cases[i].extra.w<0||error<=cases[i].extra.w);
                report["fiber_results"].push_back({{"encoding",names[i]},
                    {"source_normal",{cases[i].center.x,cases[i].center.y,cases[i].center.z}},
                    {"shading_normal",{cases[i].right.x,cases[i].right.y,cases[i].right.z}},
                    {"reference",{expected[i].x,expected[i].y,expected[i].z}},
                    {"gpu",{actual.x,actual.y,actual.z}},{"axis_error",error},
                    {"degenerate_projection",cases[i].extra.w<0}});
                continue;
            }
            double truth=expected[i].x+expected[i].y+expected[i].z,value=values[i].x+values[i].y+values[i].z;
            Require(std::isfinite(value)&&values[i].x>=0&&values[i].y>=0&&values[i].z>=0,"finite nonnegative Cloth GPU result");
            if(i>=referenceCases)
            {
                if((i-referenceCases)%2==1)
                    maximumWhiteEnergy=std::max(maximumWhiteEnergy,value+values[i-1].x+values[i-1].y+values[i-1].z);
                continue;
            }
            double absolute=std::abs(value-truth),relative=absolute/std::max(truth,1e-4);maxAbsolute=std::max(maxAbsolute,absolute);
            if(truth>1e-4) errors[suites[i]].push_back(relative);
            report["results"].push_back({{"suite",suites[i]},{"material",names[i]},{"quadrant",i%4},
                {"reference",{expected[i].x,expected[i].y,expected[i].z}},{"gpu",{values[i].x,values[i].y,values[i].z}},
                {"absolute_error",absolute},{"relative_error",relative}});
        }
        bool passed=maximumWhiteEnergy<=1.001&&fiberPassed;
        report["white_furnace_cases"]=(lightingCases-referenceCases)/2;
        report["fiber_frame_cases"]=cases.size()-lightingCases;
        report["fiber_frame_passed"]=fiberPassed;
        report["maximum_fiber_axis_error"]=maximumFiberError;
        report["maximum_fiber_perpendicular_error"]=maximumFiberPerpendicularError;
        report["maximum_fiber_unit_error"]=maximumFiberUnitError;
        report["maximum_white_furnace_energy"]=maximumWhiteEnergy;
        for(const auto& suite:errors)
        {
            double median=Percentile(suite.second,.5),p95=Percentile(suite.second,.95);
            report["suites"][suite.first.empty()?"base":suite.first]={{"median",median},{"p95",p95}};
            passed=passed&&median<=.05&&p95<=.15;
        }
        report["maximum_absolute_error"]=maxAbsolute;report["passed"]=passed;
        std::ofstream output(reportPath);Require(bool(output),"Cloth GPU report output");output<<report.dump(2)<<'\n';
        std::cout<<"[ClothLightingGPU] "<<cases.size()<<" cases; "<<report.at("suites").dump()<<std::endl;
        return passed;
    }
    catch(const std::exception& error)
    {
        std::cerr<<"[ClothLightingGPU] FAILED: "<<error.what()<<std::endl;return false;
    }
}

bool TestClothMaterialLiveEditGpuContract(const char* projectPath,const char* reportPath)
{
    using namespace Vans;
    using namespace Vans::EditorAPI;
    struct AuthoringGuard
    {
        ~AuthoringGuard()
        {
            VansAssetDocumentEditService::ClearAllHistories();
            VansAssetDocumentRegistry::Get().Clear();
            VansProjectManager::Get().CloseProject();
        }
    } authoringGuard;
    try
    {
        VansIOAudit::Reset();
        auto& project=VansProjectManager::Get();
        project.CloseProject();
        VansProjectOpenRequest request;
        request.m_ProjectRootPath=projectPath;
        request.m_Options.m_UpdateRecentProjects=false;
        request.m_Options.m_LoadProjectSettings=false;
        Require(project.OpenProject(request).m_Opened,"read-only existing project open");
        auto* database=project.GetAssetDatabase();Require(database!=nullptr,"material test asset database");
        auto& repository=project.GetAssetObjectRepository();
        const auto testRoot=database->AssetsRoot()/"__ClothLiveEditContract";
        Require(!std::filesystem::exists(testRoot),"test path collides with source assets");
        const auto path=testRoot/"Cloth.mat";
        VansAssetRecord materialRecord;materialRecord.type=VansAssetType::Material;materialRecord.sourcePath=path;
        Require(VansAssetGuid::TryParse("31000000-0000-4000-8000-000000000100",materialRecord.guid),"test material GUID");
        std::string error;Require(database->RegisterMemoryAsset(materialRecord,error),"test material registration");
        std::array<VansAssetRecord,7> records;
        std::array<std::shared_ptr<const VansAssetMeta>,7> metadata;
        for(size_t i=0;i<records.size();++i)
        {
            auto& record=records[i];record.type=VansAssetType::Texture;
            record.sourcePath=testRoot/VansClothTextureSlots[i]/"shared.png";
            const std::string guid="31000000-0000-4000-8000-00000000010"+std::to_string(i+1);
            Require(VansAssetGuid::TryParse(guid,record.guid),"test texture GUID");
            auto meta=std::make_shared<VansAssetMeta>();meta->guid=record.guid;
            meta->importer=VansAssetDatabase::ImporterFor(record.type);
            meta->SetSerializedSettings(VansSerializedValue::Object({
                {"colorSpace",VansSerializedValue::String(i==0?"sRGB":"linear")},
                {"importChannel",VansSerializedValue::Int(i<2?4:i==5?2:1)}}));
            Require(VansAssetDatabase::ReadTextureImportSettings(*meta,record.textureImport,error),"test import metadata parse");
            Require(database->RegisterMemoryAsset(record,error),"test texture registration");
            Require(repository.Publish<VansAssetMeta>(record.guid,record.type,i+1,meta,{},error).IsValid(),"test texture metadata publication");
            metadata[i]=std::move(meta);
        }
        GpuScope gpu;gpu.Initialize();
        std::vector<std::unique_ptr<VansTexture>> textures;
        const auto makeTexture=[&](const std::string& name,bool srgb)
        {
            auto texture=std::make_unique<VansTexture>();texture->m_AssetName=name;
            const std::array<uint8_t,4> pixel{128,128,255,255};
            texture->LoadFromMemory(gpu.device->GetImmediateGraphicsCommandBuffer(),pixel.data(),pixel.size(),
                1,1,srgb?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM);
            auto* result=texture.get();textures.push_back(std::move(texture));return result;
        };
        std::array<VansTexture*,7> assigned;
        for(size_t i=0;i<assigned.size();++i) assigned[i]=makeTexture(records[i].guid.ToString(),i==0);
        std::array<VansTexture*,5> defaults;
        const std::array<const char*,4> defaultNames{"defaultAlbedo","defaultNormal","defaultRoughness","defaultAo"};
        for(size_t i=0;i<defaultNames.size();++i) defaults[i]=makeTexture(defaultNames[i],i==0);
        // sheenMask intentionally shares the engine's neutral AO fallback.
        defaults[4]=defaults[3];
        VansClothMaterial cloth;cloth.m_AssetName=materialRecord.guid.ToString();cloth.m_MaterialIndex=0;
        cloth.m_MaterialType=VAN_CLOTH;
        cloth.m_BaseColorTexture=defaults[0];cloth.m_NormalTexture=defaults[1];
        cloth.m_RoughnessTexture=defaults[2];cloth.m_AoTexture=defaults[3];cloth.m_SheenMaskTexture=defaults[4];
        VansScene scene;scene.AddMaterialAsset(&cloth);
        for(size_t i=0;i<assigned.size();++i) scene.AddTextureAsset(assigned[i],records[i].guid.ToString());
        for(size_t i=assigned.size();i<textures.size();++i) scene.AddTextureAsset(textures[i].get());
        auto* manager=scene.GetMaterialManager();
        manager->m_GlobalPBRParamData.resize(1);manager->m_GlobalClothParamData.resize(1);
        manager->m_GlobalPBRTextures={&defaults[0]->GetImage(),&defaults[1]->GetImage(),
            &defaults[0]->GetImage(),&defaults[2]->GetImage(),&defaults[3]->GetImage()};
        Require(manager->FlushMaterialPayload(cloth),"initial Cloth CPU payload");
        auto instance=GIInstanceMaterialGPU::ResolveCloth(0,false,.5f,false,{1,1,1},1);
        instance.clothMaterialIndex=0;scene.GetTLASInstanceMaterials().push_back(instance);
        for(size_t i:{0u,1u,3u,2u,3u}) scene.GetTLASInstanceTextures().push_back(defaults[i]->GetImage());
        MaterialTransactionBackend backend(*gpu.device);NoMaterialFrameSource source;
        VansRenderSystem renderSystem(backend,source,true);
        Require(renderSystem.InitializeFrameExecution(),"material RenderThread startup");
        Require(renderSystem.ExecuteRenderThreadTransaction(std::make_unique<ClothDescriptorBuildTransaction>(cloth)),
            "initial real Cloth descriptor build");
        EngineAPIImpl api(&scene,gpu.device.get());api.BindRenderSystem(&renderSystem);
        struct DetachGuard { EngineAPIImpl& api;~DetachGuard(){api.BindRuntime(nullptr,nullptr);} } detachGuard{api};
        struct IdleHookGuard
        {
            IdleHookGuard()
            {
                materialOriginalWaitIdle=vkDeviceWaitIdle;vkDeviceWaitIdle=&CountMaterialWaitIdle;
                materialOriginalUpdateDescriptors=vkUpdateDescriptorSets;vkUpdateDescriptorSets=&TrackMaterialDescriptorUpdate;
            }
            ~IdleHookGuard()
            {vkDeviceWaitIdle=materialOriginalWaitIdle;vkUpdateDescriptorSets=materialOriginalUpdateDescriptors;materialTrackedSet=VK_NULL_HANDLE;}
        } idleHook;
        materialTrackedSet=cloth.m_ClothOwnedDescSets[0];
        auto root=VansSerializedValue::Object({
            {"guid",VansSerializedValue::String(cloth.m_AssetName)},
            {"materialType",VansSerializedValue::String("cloth")},
            {"parameters",WriteClothMaterialParameters(cloth.m_Params)},
            {"textures",VansSerializedValue::Object({})}});
        auto metaRoot=VansSerializedValue::Object({{"guid",VansSerializedValue::String(cloth.m_AssetName)},
            {"importer",VansSerializedValue::String(VansAssetDatabase::ImporterFor(VansAssetType::Material))},
            {"settings",VansSerializedValue::Object({})}});
        auto documents=VansAssetDocumentRegistry::Get().CreateInMemory(path,root,metaRoot,false,error);
        Require(bool(documents),error.c_str());
        auto mask=root;auto parameters=cloth.m_Params;parameters.alphaMode=VansClothAlphaMode::Mask;
        parameters.sheenWeight=.63f;parameters.anisotropy=.4f;
        SetSerializedObjectField(mask,"parameters",WriteClothMaterialParameters(parameters));
        auto slots=VansSerializedValue::Object({});
        for(size_t i=0;i<records.size();++i) SetSerializedObjectField(slots,VansClothTextureSlots[i],
            VansSerializedValue::Object({{"guid",VansSerializedValue::String(records[i].guid.ToString())}}));
        SetSerializedObjectField(mask,"textures",slots);
        VansSerializedValue prepared;Require(PrepareClothMaterialAuthoringEdit(api,mask,prepared,error),"valid seven-slot Mask preparation");
        Require(bool(VansAssetDocumentEditService::ReplaceRoot(documents->sourceDocument,prepared)),
            "Mask authoring edit");
        const auto apply=[&](bool masked)
        {
            const auto beforeRevision=scene.GetRayTracingGeometryRevision();materialIdleCalls=0;
            materialDescriptorViews.fill(VK_NULL_HANDLE);materialDescriptorWrites=0;
            const auto previousSet=cloth.m_ClothOwnedDescSets[0];
            const auto previousLayout=cloth.m_ClothOwnedLayout;
            const auto change=BuildRuntimeMaterialPreviewChange(path,documents->sourceDocument.SerializedRootSnapshot());
            Require(api.ApplyRuntimeMaterialPreviewChange(change),"Cloth full runtime preview");
            Require(materialIdleCalls==1&&materialIdleThread!=std::this_thread::get_id(),"one Cloth batch wait on RenderThread");
            Require(scene.GetRayTracingGeometryRevision()==beforeRevision+1,"one Cloth visibility invalidation per texture batch");
            Require(cloth.m_ClothOwnedLayout==previousLayout&&cloth.m_ClothOwnedDescSets[0]==previousSet,
                "Cloth texture edit replaced its fixed descriptor layout/set");
            const std::array<VansTexture*,7> expected=masked?assigned:
                std::array<VansTexture*,7>{defaults[0],defaults[1],defaults[2],defaults[3],defaults[3],defaults[1],defaults[3]};
            Require(materialDescriptorWrites==7,"Cloth descriptor batch did not write exactly seven bindings");
            for(size_t slot=0;slot<expected.size();++slot)
                Require(materialDescriptorViews[slot]==expected[slot]->GetImage().GetImageView(),"native Cloth descriptor image mismatch");
            Require(cloth.m_Params.alphaMode==(masked?VansClothAlphaMode::Mask:VansClothAlphaMode::Opaque),"restored Cloth mode");
            Require(cloth.m_BaseColorTexture==(masked?assigned[0]:defaults[0])&&
                cloth.m_NormalTexture==(masked?assigned[1]:defaults[1])&&
                cloth.m_RoughnessTexture==(masked?assigned[2]:defaults[2])&&
                cloth.m_AoTexture==(masked?assigned[3]:defaults[3])&&
                cloth.m_SheenMaskTexture==(masked?assigned[4]:defaults[4])&&
                cloth.m_FiberDirectionTexture==(masked?assigned[5]:nullptr)&&
                cloth.m_OpacityTexture==(masked?assigned[6]:nullptr),"seven Cloth texture pointers restored");
            const auto snapshot=manager->CaptureRenderMaterialFrameData({&cloth});
            VansClothGPUParam snapshotParameters;
            Require(snapshot.cloth.bytes.size()==sizeof(snapshotParameters),"Cloth snapshot byte count");
            std::memcpy(&snapshotParameters,snapshot.cloth.bytes.data(),sizeof(snapshotParameters));
            Require(snapshotParameters.flags.x==uint32_t(masked)&&
                snapshotParameters.flags.z==uint32_t(masked),"Cloth snapshot flags restored");
            Require(scene.GetTLASInstanceTextures()[2].GetImageView()==
                (masked?assigned[6]:defaults[3])->GetImage().GetImageView(),"GI instance opacity/neutral texture refresh");
            return snapshot;
        };
        const auto firstMask=apply(true);
        const auto unchangedSet=cloth.m_ClothOwnedDescSets[0];
        materialIdleCalls=0;materialDescriptorWrites=0;
        Require(api.ApplyRuntimeMaterialPreviewChange(BuildRuntimeMaterialPreviewChange(path,
            documents->sourceDocument.SerializedRootSnapshot()))&&materialIdleCalls==1&&
            cloth.m_ClothOwnedDescSets[0]==unchangedSet&&materialDescriptorWrites==0,"unchanged snapshot rebuilt Cloth descriptors");
        for(int iteration=0;iteration<3;++iteration)
        {
            Require(bool(VansAssetDocumentEditService::Undo(documents->sourceDocument)),"Mask undo");apply(false);
            VansClothGPUParam savedParameters;
            std::memcpy(&savedParameters,firstMask.cloth.bytes.data(),sizeof(savedParameters));
            Require(savedParameters.flags.x==1&&savedParameters.flags.z==1,"published snapshot was mutated");
            Require(bool(VansAssetDocumentEditService::Redo(documents->sourceDocument)),"Mask redo");apply(true);
        }
        const auto beforeFailure=manager->CaptureRenderMaterialFrameData({&cloth});
        const auto beforeGeometry=scene.GetRayTracingGeometryRevision();
        RuntimeMaterialPreviewChange invalid;invalid.assetPath=path.string();
        invalid.parameters={{"alphaMode",std::string("opaque")},{"roughness",.25f}};
        invalid.textures={{"normal",""},{"roughness",records[0].guid.ToString()}};
        materialIdleCalls=0;materialDescriptorWrites=0;
        Require(!api.ApplyRuntimeMaterialPreviewChange(invalid),"invalid texture batch accepted");
        const auto afterFailure=manager->CaptureRenderMaterialFrameData({&cloth});
        Require(cloth.m_NormalTexture==assigned[1]&&cloth.m_Params.alphaMode==VansClothAlphaMode::Mask&&
            cloth.m_Params.roughness==parameters.roughness&&materialIdleCalls==1&&materialDescriptorWrites==0&&
            beforeFailure.clothLightingRevision==afterFailure.clothLightingRevision&&
            beforeGeometry==scene.GetRayTracingGeometryRevision(),"rejected Cloth batch partially changed live state");
        invalid.textures={{"opacity",""}};
        invalid.parameters={{"alphaMode",std::string("mask")}};
        Require(!api.ApplyRuntimeMaterialPreviewChange(invalid)&&cloth.m_OpacityTexture==assigned[6],"Mask accepted opacity clear");
        // The public lookup must expose the published working metadata, not its stale DB import copy.
        auto changedMeta=std::make_shared<VansAssetMeta>(*metadata[2]);changedMeta->SetStringSetting("colorSpace","sRGB");
        Require(repository.Publish<VansAssetMeta>(records[2].guid,VansAssetType::Texture,99,changedMeta,{},error).IsValid(),"working texture metadata publication");
        Require(!PrepareClothMaterialAuthoringEdit(api,mask,prepared,error)&&error.find("roughness")!=std::string::npos,
            "Cloth authoring validation read stale texture import metadata");
        const auto io=VansIOAudit::Snapshot();
        Require(std::none_of(io.begin(),io.end(),[](const auto& event){return event.operation==VansIOOperation::StageWrite;}),
            "live material edit wrote a file");
        Require(!std::filesystem::exists(testRoot),"material test created source data");
        Json report={{"passed",true},{"project",projectPath},{"seven_slots",true},{"mask_undo_redo_cycles",3},
            {"one_render_thread_wait_per_batch",true},{"failed_batch_preserves_state",true},
            {"gi_texture_refresh",true},{"immutable_material_snapshot",true},{"latest_import_metadata",true},
            {"real_material_descriptors_updated_in_place",true},{"unchanged_snapshot_preserves_descriptors",true},
            {"native_seven_binding_image_updates_checked",true},
            {"guid_identity_with_duplicate_file_stems",true},
            {"authoring_writes",0},{"new_project_or_assets_created",false},
            {"scope","real Vulkan images, production EngineAPI and RenderThread; no rendered frame or UI clicks"}};
        std::ofstream reportFile(reportPath);Require(bool(reportFile),"material edit report output");reportFile<<report.dump(2)<<'\n';
        renderSystem.ShutdownFrameExecution();
        std::cout<<"[ClothMaterialLiveEditGPU] seven slots, Mask undo/redo, failed-batch rollback, snapshot and GI texture refresh passed\n";
        return true;
    }
    catch(const std::exception& error)
    { std::cerr<<"[ClothMaterialLiveEditGPU] FAILED: "<<error.what()<<'\n';return false; }
}
