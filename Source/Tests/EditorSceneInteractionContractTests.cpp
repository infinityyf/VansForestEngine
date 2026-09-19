#include "../EngineCore/EditorCore/VansEditorSceneMath.h"
#include "../EngineCore/EditorCore/VansEditorSelectionService.h"
#include "../EngineCore/EditorCore/VansEditorCameraController.h"
#include "../EngineCore/EngineAPILayer/Private/VansEditorSceneQuery.h"
#include "../EngineCore/RenderCore/VansCamera.h"
#include "../EngineCore/RenderCore/VansCameraControlArbiter.h"
#include "../EngineCore/RenderCore/VansRenderNode.h"
#include "../EngineCore/RenderCore/VulkanCore/VansMesh.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/SceneRuntime/VansRuntimeWorld.h"
#include "../EngineCore/SceneRuntime/VansRuntimeComponentTypes.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Vans;
using namespace Vans::EditorAPI;
using namespace VansGraphics;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
glm::vec3 Vec(Vec3 v) { return {v.x, v.y, v.z}; }
class CameraDevice final : public VansGraphicsDevice
{
public:
    CameraDevice() { m_RenderWidth = 1280; m_RenderHeight = 720; }
    bool BeforeRendering() override { return true; }
    void Rendering() override {}
    void Present() override {}
    void AfterRendering() override {}
    void* GetNativeGraphicsDevice() override { return nullptr; }
    void* GetNativeCommandBuffer() override { return nullptr; }
};
class GeometryNode final : public VansRenderNode
{
public:
    explicit GeometryNode(VkDevice& device) : VansRenderNode(device, OPAQUE_NODE)
    {
        // 无绘制资源的查询 fixture，仍使用真实节点变换与 GPU 网格。
        modelBufferLayout = textureResourceLayout = frameBufferInputLayout = VK_NULL_HANDLE;
    }
};
class GeometryExecutor final : public IVansRenderThreadTransactionExecutor
{
public:
    explicit GeometryExecutor(VansVKDevice& device) : device(device) {}
    bool ExecuteRenderThreadTransaction(std::unique_ptr<IVansRenderThreadTransaction> transaction) override
    { ++reads; return transaction->Execute(device); }
    VansVKDevice& device;
    int reads = 0;
};
}

bool TestEditorSceneInteractionContract()
{
    try
    {
        const glm::vec3 cameraPosition(4, 7, 15), target(-2, 1, 0);
        const glm::mat4 view = glm::lookAt(cameraPosition, target, glm::vec3(0, 1, 0));
        const auto projection = glm::perspective(glm::radians(53.f), 1.7f, .1f, 200.f);
        for (const auto uv : {glm::vec2(.5f), glm::vec2(.1f,.8f), glm::vec2(.9f,.2f)})
        {
            Ray ray; float length;
            Check(BuildEditorSceneRay(projection * view, uv, ray, length), "perspective unprojection");
            const auto point = projection * view * glm::vec4(Vec(ray.origin) + Vec(ray.direction) * 5.f, 1);
            Check(glm::length(glm::vec2(point.x/point.w*.5f+.5f, .5f-point.y/point.w*.5f)-uv)<1e-4f, "ray/image reprojection mismatch");
        }
        Ray a,b; float length;
        const auto ortho = glm::ortho(-5.f,5.f,-3.f,3.f,.1f,100.f)*view;
        Check(BuildEditorSceneRay(ortho,{.1f,.3f},a,length) && BuildEditorSceneRay(ortho,{.9f,.7f},b,length), "orthographic unprojection");
        Check(glm::length(Vec(a.direction)-Vec(b.direction))<1e-5f && glm::length(Vec(a.origin)-Vec(b.origin))>1, "orthographic ray origins");
        Check(!BuildEditorSceneRay(projection*view,{-1,.5f},a,length), "letterbox click accepted");
        Check(!BuildEditorSceneRay(glm::mat4(0),{.5f,.5f},a,length), "singular camera accepted");

        EditorSceneBounds bounds{true, {-10,-2,-1}, {10,2,1}};
        for (float aspect : {.3f,1.f,3.f})
        {
            glm::vec3 position; float farClip;
            Check(CalculateEditorFramePosition(bounds,{0,0,-1},45,aspect,.1f,position,farClip), "frame calculation");
            const auto vp=glm::perspective(glm::radians(45.f),aspect,.1f,farClip)*glm::lookAt(position,glm::vec3(0),glm::vec3(0,1,0));
            for (int i=0;i<8;++i)
            {
                const auto p=vp*glm::vec4(i&1?10.f:-10.f,i&2?2.f:-2.f,i&4?1.f:-1.f,1);
                Check(std::abs(p.x/p.w)<1 && std::abs(p.y/p.w)<1 && p.z/p.w>=0 && p.z/p.w<1, "framed corner clipped");
            }
        }
        CameraDevice device; VansCamera camera(&device); VansEditorCameraController control;
        const auto rotation=camera.CaptureControlPose().rotationDegrees;
        Check(control.Frame(&camera,bounds,camera.GetAspectRatio()), "camera rejected frame");
        VansEditorCameraInputState input; input.editMode=true; input.deltaTime=.05f;
        for(int i=0;i<5;++i) control.Update(&camera,input);
        const auto framed=camera.CaptureControlPose();
        Check(glm::length(rotation-framed.rotationDegrees)<1e-5f, "frame changed view orientation");
        bounds.minimum.x+=100; bounds.maximum.x+=100;
        Check(control.Frame(&camera,bounds,camera.GetAspectRatio()), "second frame");
        input.cancelFraming=true; control.Update(&camera,input);
        Check(glm::length(camera.CaptureControlPose().position-framed.position)<1e-5f, "cancel did not stop focus");
        Check(control.Frame(&camera,bounds,camera.GetAspectRatio()), "third frame");
        input.cancelFraming=false; input.editMode=false; control.Update(&camera,input);
        Check(glm::length(camera.CaptureControlPose().position-framed.position)<1e-5f, "focus leaked into play mode");

        auto& selection=VansEditorSelectionService::Get();
        EditorObjectHandle first,second;
        first.domain=second.domain=EditorObjectDomain::SceneEntity;
        first.guid=first.entityGuid="first"; second.guid=second.entityGuid="second";
        selection.Apply(EditorSelectionOperation::Replace,{first,second},second,"test");
        selection.Apply(EditorSelectionOperation::Toggle,{second},second,"test");
        Check(selection.Contains(first) && !selection.Contains(second) && selection.EntityGuid()=="first", "toggle resurrected active selection");
        selection.Clear("test");
        std::cout<<"EDITOR_SCENE_INTERACTION_PASS ray_projection=perspective+orthographic frame=wide+tall cancel=pass play_isolation=pass selection_toggle=pass\n";
        return true;
    }
    catch(const std::exception& e) { std::cerr<<"EDITOR_SCENE_INTERACTION_FAIL "<<e.what()<<'\n'; return false; }
}

// 由已有 Vulkan fixture 调用，验证与编辑器相同的查询实现，而非另写一个测试用算法。
void TestEditorScenePickingGpu(VansGraphics::VansVKDevice& device, VansGraphics::VansMesh& imported)
{
    GeometryExecutor executor(device);
    VansRuntimeWorld world;
    const auto parent=world.CreateEntity({"parent","Parent"});
    const auto nearEntity=world.CreateEntity({"near","Near",parent});
    const auto farEntity=world.CreateEntity({"far","Far"});
    GeometryNode nearNode(device.GetLogicDevice()),farNode(device.GetLogicDevice());
    nearNode.m_Mesh=farNode.m_Mesh=&imported;
    nearNode.m_EntityGuid="near"; farNode.m_EntityGuid="far";
    farNode.SetTransformData({0,-4,0},{0,0,0},{1,1,1});
    const auto nearComponent=world.AddComponent(nearEntity,VansRuntimeComponentType_Render,VansRuntimeRenderComponent{&nearNode,{}});
    world.AddComponent(farEntity,VansRuntimeComponentType_Render,VansRuntimeRenderComponent{&farNode,{}});
    VansEditorSceneQuery query;
    EditorScenePickRequest request; request.ray={{-1,-10,-1},{0,-1,0}}; request.maxDistance=20; request.selectableEntities={"near","far"};
    auto result=query.Pick(world,request,&executor);
    Check(result.success && result.entityGuid=="near", "GPU mesh without CPU data was not picked");
    Check(executor.reads==1,"shared mesh was read more than once");
    Check(query.Pick(world,request,&executor).entityGuid=="near" && executor.reads==1,"warm pick performed GPU readback");
    request.ray.origin={1.5f,-10,1.5f};
    Check(query.Pick(world,request,&executor).entityGuid.empty(),"empty part of triangle bounds was picked");
    request.ray.origin={-1,-10,-1};
    world.SetComponentEnabled(nearComponent,false);
    Check(query.Pick(world,request,&executor).entityGuid=="far","disabled component intercepted pick");
    world.SetComponentEnabled(nearComponent,true);
    request.selectableEntities={"parent","far"};
    Check(query.Pick(world,request,&executor).entityGuid=="parent","runtime child did not resolve to document ancestor");
    request.selectableEntities={"far"};
    Check(query.Pick(world,request,&executor).entityGuid=="far","unmapped near node swallowed mapped hit");
    request.selectableEntities={"near","far"};
    nearNode.SetTransformData({8,2,-3},{25,70,35},{-2,.5f,3});
    const auto model=nearNode.GetTransformMatrix();
    const glm::vec3 point(model*glm::vec4(-1,-12,-1,1));
    const glm::vec3 normal=glm::normalize(glm::transpose(glm::inverse(glm::mat3(model)))*glm::vec3(0,1,0));
    request.ray.origin={point.x+normal.x*2,point.y+normal.y*2,point.z+normal.z*2};
    request.ray.direction={-normal.x,-normal.y,-normal.z};
    result=query.Pick(world,request,&executor);
    Check(result.success && result.entityGuid=="near","rotated offset mesh with negative/nonuniform scale missed");
    Check(executor.reads==1,"transform change rebuilt shared geometry");
    const auto bounds=query.Bounds(world,{"parent"},&executor);
    Check(bounds.available && point.x>=bounds.minimum.x && point.x<=bounds.maximum.x &&
        point.y>=bounds.minimum.y && point.y<=bounds.maximum.y && point.z>=bounds.minimum.z && point.z<=bounds.maximum.z,
        "parent focus excluded rotated child");
    const auto emptyEntity=world.CreateEntity({"empty","Empty"});
    const auto transformId=VansTransformStore::AllocateTransform();
    VansTransformStore::GetTransform(transformId).m_Position={500,600,700};
    world.AddComponent(emptyEntity,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{transformId});
    const auto mixedBounds=query.Bounds(world,{"parent","empty"},&executor);
    VansTransformStore::FreeTransform(transformId);
    Check(mixedBounds.available && mixedBounds.maximum.x>=500 && mixedBounds.maximum.y>=600 &&
        mixedBounds.maximum.z>=700 && mixedBounds.minimum.x<=point.x,"mixed selection excluded empty object");
    request.ray.direction={0,0,0};
    Check(!query.Pick(world,request,&executor).success,"invalid ray mutated selection as a miss");
    world.DestroyEntity(nearEntity);
    request.ray={{-1,-10,-1},{0,-1,0}};
    Check(query.Pick(world,request,&executor).entityGuid=="far","deleted node survived in pick candidates");
    std::cout<<"EDITOR_SCENE_PICKING_GPU_PASS cpu_data=released triangles=precise shared_cache=1_read transforms=rotated+negative+nonuniform disabled=filtered owner_mapping=pass deletion=pass parent_bounds=pass\n";
}
