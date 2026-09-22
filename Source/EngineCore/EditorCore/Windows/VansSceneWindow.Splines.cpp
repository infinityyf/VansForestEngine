#include "VansSceneWindow.h"
#include "imgui.h"
#include "ImGuizmo.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace VansGraphics
{
void VansSceneWindow::FinishSplineGizmo(Vans::EditorAPI::IEngineEditorAPI& api,bool cancel)
{
    if (!m_SplineGizmoDragging) return;
    m_SplineGizmoEdit.phase=cancel?Vans::EditorAPI::PcgSplineEditPhase::Cancel:Vans::EditorAPI::PcgSplineEditPhase::Commit;
    const auto result=api.ApplyPcgSplineEdit(m_SplineGizmoEdit);
    if (!result.success)
    {
        m_SplineGizmoEdit.phase=Vans::EditorAPI::PcgSplineEditPhase::Cancel;api.ApplyPcgSplineEdit(m_SplineGizmoEdit);
    }
    m_SplineGizmoDragging=false;
}

void VansSceneWindow::DrawSplineTools(Vans::EditorAPI::IEngineEditorAPI& api,const Vans::EditorAPI::PcgSplineSnapshot& snapshot,
    glm::vec2 origin,glm::vec2 size,bool inside)
{
    using namespace Vans::EditorAPI;
    if (!m_Camera) return;
    if (inside && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F))
    {
        PcgSplineCommandRequest request;request.command=PcgSplineCommand::Focus;request.splineId=snapshot.selectedSpline;
        api.ExecutePcgSplineCommand(request);
    }
    const glm::mat4 view=m_Camera->GetViewMatrix(),projection=m_Camera->GetProjectiveMatrix();
    const auto project=[&](glm::vec3 world,ImVec2& screen) {
        const glm::vec4 clip=projection*view*glm::vec4(world,1);
        if (clip.w<=.001f) return false;
        screen={origin.x+(clip.x/clip.w*.5f+.5f)*size.x,origin.y+(1-(clip.y/clip.w*.5f+.5f))*size.y};
        return true;
    };
    const auto vec=[](const std::array<float,3>& p){return glm::vec3(p[0],p[1],p[2]);};
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect({origin.x,origin.y},{origin.x+size.x,origin.y+size.y},true);
    const auto line=[&](glm::vec3 a,glm::vec3 b,ImU32 color,float thickness) {
        ImVec2 pa,pb;if(project(a,pa)&&project(b,pb))draw->AddLine(pa,pb,color,thickness);
    };
    for (const auto& guide:snapshot.guides)
    {
        for (std::size_t i=0;i<guide.flowOrigins.size();++i)
        {
            const auto a=vec(guide.flowOrigins[i]),velocity=vec(guide.flowVectors[i]);
            ImVec2 pa,pb;
            if (!project(a,pa) || !project(a+velocity,pb)) continue;
            const auto color=IM_COL32(90,240,205,225);
            if (glm::length(velocity)<.01f) {draw->AddCircle(pa,3,color);continue;}
            draw->AddLine(pa,pb,color,1.5f);
            const glm::vec2 delta(pb.x-pa.x,pb.y-pa.y);
            if (glm::length(delta)>2)
            {
                const auto direction=glm::normalize(delta),side=glm::vec2(-direction.y,direction.x);
                for (float sign:{-1.f,1.f})
                {const auto tip=glm::vec2(pb.x,pb.y)-direction*6.f+side*(3.f*sign);draw->AddLine(pb,{tip.x,tip.y},color,1.5f);}
            }
        }
        const bool selected=guide.id==snapshot.selectedSpline;
        for (std::size_t i=1;i<guide.center.size();++i)
        {
            line(vec(guide.center[i-1]),vec(guide.center[i]),selected?IM_COL32(255,195,80,255):IM_COL32(160,190,210,170),selected?2.f:1.f);
            line(vec(guide.left[i-1]),vec(guide.left[i]),IM_COL32(90,210,240,180),1);
            line(vec(guide.right[i-1]),vec(guide.right[i]),IM_COL32(90,210,240,180),1);
        }
        if (selected)
        {
            const auto spline=std::find_if(snapshot.splines.begin(),snapshot.splines.end(),
                [&](const auto& item){return item.id==guide.id;});
            if (spline!=snapshot.splines.end() && spline->kind==PcgSplineKind::Road &&
                spline->roadRenderMode==PcgRoadRenderMode::ProjectedDecal)
            {
                const float depth=std::max(spline->projectedDepth,.05f);
                const auto lower=[&](const std::array<float,3>& value) {
                    auto p=vec(value);p.y-=depth;return p;
                };
                const auto proxyColor=IM_COL32(255,120,80,210);
                for (std::size_t i=1;i<guide.center.size();++i)
                {
                    line(lower(guide.left[i-1]),lower(guide.left[i]),proxyColor,1.5f);
                    line(lower(guide.right[i-1]),lower(guide.right[i]),proxyColor,1.5f);
                }
                for (std::size_t i=0;i<guide.center.size();i+=std::max<std::size_t>(1,guide.center.size()/48))
                {
                    line(vec(guide.left[i]),lower(guide.left[i]),proxyColor,1);
                    line(vec(guide.right[i]),lower(guide.right[i]),proxyColor,1);
                }
                if (!guide.center.empty())
                    draw->AddText(ImVec2(origin.x+12,origin.y+32),proxyColor,
                        "Road decal proxy volume (downward)");
            }
        }
    }
    std::string hitSpline,hitPoint;int hitHandle=0;float nearest=100;
    for (const auto& p:snapshot.uncoveredBankPoints)
    {
        ImVec2 screen;if (!project(vec(p),screen)) continue;
        const auto color=IM_COL32(255,95,80,255);
        draw->AddLine({screen.x-7,screen.y-7},{screen.x+7,screen.y+7},color,2);
        draw->AddLine({screen.x-7,screen.y+7},{screen.x+7,screen.y-7},color,2);
        draw->AddText({screen.x+10,screen.y-6},color,"Lower river level: bank clearance");
    }
    const auto marker=[&](const PcgSplineItem& spline,const PcgSplinePoint& point,glm::vec3 world,int handle,ImU32 color) {
        ImVec2 screen;if (!project(world,screen))return;
        const bool selected=spline.id==snapshot.selectedSpline&&point.id==snapshot.selectedPoint&&handle==m_SplineHandle;
        draw->AddCircleFilled(screen,selected?6.f:4.f,color);
        if (spline.locked) return;
        const auto mouse=ImGui::GetMousePos();const float distance=(mouse.x-screen.x)*(mouse.x-screen.x)+(mouse.y-screen.y)*(mouse.y-screen.y);
        if(distance<nearest) {nearest=distance;hitSpline=spline.id;hitPoint=point.id;hitHandle=handle;}
    };
    for (const auto& spline:snapshot.splines) if(spline.enabled)
    {
        for (const auto& point:spline.points)
        {
            const auto center=vec(point.position);
            marker(spline,point,center,0,IM_COL32(255,220,130,255));
            if(spline.id!=snapshot.selectedSpline||point.id!=snapshot.selectedPoint)continue;
            const auto incoming=center+vec(point.arrive),outgoing=center+vec(point.leave);
            line(incoming,center,IM_COL32(230,130,230,220),1);line(center,outgoing,IM_COL32(230,130,230,220),1);
            marker(spline,point,incoming,-1,IM_COL32(230,130,230,255));marker(spline,point,outgoing,1,IM_COL32(230,130,230,255));
            auto tangent=vec(point.leave);if(glm::length(tangent)<1e-5f)tangent=-vec(point.arrive);
            auto right=glm::cross(tangent,glm::vec3(0,1,0));
            right=glm::length(right)>1e-5f?glm::normalize(right):glm::vec3(0,0,1);
            right.y=std::tan(glm::radians(point.bankAngleDegrees));
            line(center-right*point.leftWidth,center+right*point.rightWidth,IM_COL32(90,210,240,200),1);
            marker(spline,point,center-right*point.leftWidth,-2,IM_COL32(90,210,240,255));
            marker(spline,point,center+right*point.rightWidth,2,IM_COL32(90,210,240,255));
        }
    }
    draw->AddText({origin.x+12,origin.y+12},IM_COL32(255,235,180,255),"Spline: click point / handle | Ctrl+click terrain: extend | Esc: cancel");
    draw->PopClipRect();
    if(inside&&!m_SplineGizmoDragging&&!ImGuizmo::IsOver()&&!ImGui::IsKeyDown(ImGuiKey_LeftCtrl)&&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)&&!hitPoint.empty())
    {
        if(api.SelectPcgSpline(hitSpline,hitPoint,true).success)m_SplineHandle=hitHandle;
        return;
    }
    const auto selected=std::find_if(snapshot.splines.begin(),snapshot.splines.end(),[&](const auto& spline){return spline.id==snapshot.selectedSpline;});
    if(selected==snapshot.splines.end()||selected->locked)return;
    auto source=*selected;
    if(m_SplineGizmoDragging)source=m_SplineGizmoEdit.spline;
    auto found=std::find_if(source.points.begin(),source.points.end(),[&](const auto& point){return point.id==snapshot.selectedPoint;});
    if(found==source.points.end())return;
    auto center=vec(found->position),position=center;
    auto right=glm::cross(vec(found->leave),glm::vec3(0,1,0));
    right=glm::length(right)>1e-5f?glm::normalize(right):glm::vec3(0,0,1);
    right.y=std::tan(glm::radians(found->bankAngleDegrees));
    if(m_SplineHandle==-1)position+=vec(found->arrive);
    if(m_SplineHandle==1)position+=vec(found->leave);
    if(m_SplineHandle==-2)position-=right*found->leftWidth;
    if(m_SplineHandle==2)position+=right*found->rightWidth;
    auto matrix=glm::translate(glm::mat4(1),position);
    ImGuizmo::SetDrawlist();ImGuizmo::SetRect(origin.x,origin.y,size.x,size.y);ImGuizmo::SetOrthographic(false);
    const bool moved=ImGuizmo::Manipulate(glm::value_ptr(view),glm::value_ptr(projection),ImGuizmo::TRANSLATE,ImGuizmo::WORLD,glm::value_ptr(matrix));
    if(ImGuizmo::IsUsing()&&!m_SplineGizmoDragging)
    {
        m_SplineGizmoEdit={};m_SplineGizmoEdit.spline=source;m_SplineGizmoEdit.documentState=snapshot.documentState;
        m_SplineGizmoEdit.phase=PcgSplineEditPhase::Begin;m_SplineGizmoPoint=found->id;
        m_SplineGizmoDragging=api.ApplyPcgSplineEdit(m_SplineGizmoEdit).success;
    }
    if(moved&&m_SplineGizmoDragging)
    {
        const glm::vec3 value(matrix[3]);
        const auto array=[](glm::vec3 p){return std::array<float,3>{p.x,p.y,p.z};};
        if(m_SplineHandle==0)found->position=array(value);
        else if(std::abs(m_SplineHandle)==1)
        {
            if(found->tangentMode==PcgSplineTangentMode::Auto)found->tangentMode=PcgSplineTangentMode::Aligned;
            (m_SplineHandle<0?found->arrive:found->leave)=array(value-center);
        }
        else
        {
            float width=std::max(.05f,glm::dot(value-center,right)/glm::dot(right,right)*(m_SplineHandle<0?-1.f:1.f));
            (m_SplineHandle<0?found->leftWidth:found->rightWidth)=width;
            if(found->linkedWidth)found->leftWidth=found->rightWidth=width;
        }
        m_SplineGizmoEdit.spline=source;m_SplineGizmoEdit.phase=PcgSplineEditPhase::Update;
        api.ApplyPcgSplineEdit(m_SplineGizmoEdit);
    }
    if(m_SplineGizmoDragging&&!ImGuizmo::IsUsing())FinishSplineGizmo(api,false);
}
}
