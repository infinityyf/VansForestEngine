#include "VansSceneWindow.h"
#include "../VansEditorDebugViewState.h"
#include "../../EngineAPILayer/Public/INavigationEditorAPI.h"
#include "../../Util/VansSceneViewMath.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstring>

namespace VansGraphics
{
using namespace Vans::EditorAPI;
namespace
{
glm::vec3 Position(const Vec3& p) { return {p.x,p.y,p.z}; }
bool InputPosition(const char* label,Vec3& value)
{
    float values[]{value.x,value.y,value.z};
    if(!ImGui::InputFloat3(label,values)) return false;
    value={values[0],values[1],values[2]}; return true;
}
bool NeedsGoal(NavigationPreviewQueryKind kind)
{ return kind==NavigationPreviewQueryKind::Path || kind==NavigationPreviewQueryKind::Raycast; }
const char* QueryName(NavigationPreviewQueryKind kind)
{
    switch(kind)
    {
    case NavigationPreviewQueryKind::Path: return "Path";
    case NavigationPreviewQueryKind::PointProjection: return "Project Point";
    case NavigationPreviewQueryKind::Raycast: return "Nav Raycast";
    case NavigationPreviewQueryKind::NearestBoundary: return "Nearest Boundary";
    case NavigationPreviewQueryKind::ReachablePoint: return "Reachable Point";
    }
    return "Invalid";
}
}

struct VansSceneWindow::NavigationToolState
{
    NavigationPreviewQueryRequest request;
    bool hasStart=false,hasGoal=false,drawTiles=true;
    int pick=0,maximumTiles=64,maximumTriangles=8192,maximumLinks=128;
    std::string message;
};

void VansSceneWindow::DrawNavigationPreviewToolbar(INavigationEditorAPI& api)
{
    if(!m_DebugViewState.navigationPreviewGizmos)
    { if(m_NavigationTools) m_NavigationTools->pick=0; return; }
    if(!m_NavigationTools) m_NavigationTools=std::make_shared<NavigationToolState>();
    auto& state=*m_NavigationTools;
    const auto preview=api.GetNavigationBuildPreview();
    if(state.request.previewId!=preview.requestId)
    {
        state.request.previewId=preview.requestId; state.hasStart=false; state.hasGoal=false;
        state.pick=0; state.message.clear();
    }
    if(!preview.ready) state.pick=0;
    const auto choice=[](const char* label,std::string& selected,const std::vector<std::string>& values)
    {
        if(std::find(values.begin(),values.end(),selected)==values.end()) selected=values.empty()?std::string{}:values.front();
        if(ImGui::BeginCombo(label,selected.c_str()))
        {
            for(const auto& value:values) if(ImGui::Selectable(value.c_str(),value==selected)) selected=value;
            ImGui::EndCombo();
        }
    };
    std::vector<std::string> profiles;
    for(const auto& profile:preview.profiles) profiles.push_back(profile.id);
    if(std::find(profiles.begin(),profiles.end(),state.request.profileId)==profiles.end())
        state.request.profileId=profiles.empty()?std::string{}:profiles.front();
    if(std::find(preview.filters.begin(),preview.filters.end(),state.request.filterId)==preview.filters.end())
        state.request.filterId=preview.filters.empty()?std::string{}:preview.filters.front();
    ImGui::SameLine();
    if(ImGui::Button("Navigation Tools")) ImGui::OpenPopup("NavigationTools");
    if(!ImGui::BeginPopup("NavigationTools")) return;
    ImGui::Text("%s: %s",preview.mapId.c_str(),preview.state.c_str());
    if(!preview.ready) ImGui::TextWrapped("Build a current candidate in Project Settings > Navigation.");
    if(!preview.diagnostic.empty()) ImGui::TextWrapped("%s",preview.diagnostic.c_str());
    if(!preview.exclusions.empty() && ImGui::TreeNode("Excluded PCG instances"))
    {
        for(const auto& excluded:preview.exclusions)
            ImGui::TextWrapped("%s: %llu instances - %s",excluded.id.c_str(),
                static_cast<unsigned long long>(excluded.instanceCount),excluded.reason.c_str());
        ImGui::TreePop();
    }
    ImGui::BeginDisabled(!preview.ready);
    choice("Profile",state.request.profileId,profiles); choice("Filter",state.request.filterId,preview.filters);
    ImGui::Checkbox("Tile Seams",&state.drawTiles);
    ImGui::InputInt("Tile snapshot limit",&state.maximumTiles);
    ImGui::InputInt("Triangle snapshot limit",&state.maximumTriangles);
    ImGui::InputInt("Link snapshot limit",&state.maximumLinks);
    ImGui::Separator();
    if(ImGui::BeginCombo("Query",QueryName(state.request.kind)))
    {
        for(const auto kind:{NavigationPreviewQueryKind::Path,NavigationPreviewQueryKind::PointProjection,
            NavigationPreviewQueryKind::Raycast,NavigationPreviewQueryKind::NearestBoundary,NavigationPreviewQueryKind::ReachablePoint})
            if(ImGui::Selectable(QueryName(kind),kind==state.request.kind)) { state.request.kind=kind; state.pick=0; }
        ImGui::EndCombo();
    }
    state.hasStart=InputPosition("Requested Start",state.request.start) || state.hasStart;
    if(ImGui::Button("Pick Start")) { state.pick=1; state.message="Pick start on the visible navigation mesh. Escape cancels."; ImGui::CloseCurrentPopup(); }
    if(NeedsGoal(state.request.kind))
    {
        state.hasGoal=InputPosition("Requested End",state.request.goal) || state.hasGoal;
        ImGui::SameLine();
        if(ImGui::Button("Pick End")) { state.pick=2; state.message="Pick end on the visible navigation mesh. Escape cancels."; ImGui::CloseCurrentPopup(); }
    }
    InputPosition("Projection Extents (m)",state.request.nearestExtents);
    if(state.request.kind==NavigationPreviewQueryKind::Path || state.request.kind==NavigationPreviewQueryKind::ReachablePoint)
    {
        ImGui::Checkbox("Require Requested Goal",&state.request.requireRequestedGoal);
        if(state.request.requireRequestedGoal) ImGui::InputFloat("Goal projection tolerance (m)",&state.request.goalProjectionToleranceMeters);
    }
    if(state.request.kind==NavigationPreviewQueryKind::NearestBoundary || state.request.kind==NavigationPreviewQueryKind::ReachablePoint)
        ImGui::InputFloat("Radius (m)",&state.request.radius);
    if(state.request.kind==NavigationPreviewQueryKind::ReachablePoint)
    {
        ImGui::InputScalar("Seed",ImGuiDataType_U32,&state.request.seed);
        ImGui::InputInt("Maximum Attempts",&state.request.maximumAttempts);
    }
    const auto query=api.GetNavigationPreviewQuery();
    ImGui::BeginDisabled(query.busy || !state.hasStart || (NeedsGoal(state.request.kind) && !state.hasGoal));
    if(ImGui::Button("Run Query Preview"))
    {
        const auto submitted=api.SubmitNavigationPreviewQuery(state.request);
        state.message=submitted.valid?"Query queued":submitted.error; state.pick=0;
    }
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(!query.busy);
    if(ImGui::Button("Cancel Query")) api.CancelNavigationPreviewQuery();
    ImGui::EndDisabled(); ImGui::EndDisabled();
    if(query.available)
    {
        ImGui::Text("%s / %s: %s, %s",query.profileId.c_str(),query.filterId.c_str(),query.state.c_str(),query.result.c_str());
        ImGui::Text("Iterations: %u; attempts: %u; path tiles: %zu",query.iterations,query.attempts,query.tileCount);
        ImGui::Text("Failure: %s",query.failure.c_str());
        if(query.hasProjection)
        {
            ImGui::Text("Projected start: %.3f, %.3f, %.3f (offset %.3f m)",query.projectedStart.x,query.projectedStart.y,query.projectedStart.z,query.startProjectionDistance);
            if(query.kind==NavigationPreviewQueryKind::Path || query.kind==NavigationPreviewQueryKind::ReachablePoint)
                ImGui::Text("Projected end: %.3f, %.3f, %.3f (offset %.3f m)",query.projectedEnd.x,query.projectedEnd.y,query.projectedEnd.z,query.endProjectionDistance);
        }
        if(query.hasResult) ImGui::Text("Reached result: %.3f, %.3f, %.3f",query.reachedEnd.x,query.reachedEnd.y,query.reachedEnd.z);
        if(query.kind==NavigationPreviewQueryKind::Raycast && query.hasResult) ImGui::Text("Ray fraction: %.3f",query.hitFraction);
        if(query.kind==NavigationPreviewQueryKind::NearestBoundary && query.hasResult) ImGui::Text("Boundary distance: %.3f m",query.boundaryDistanceMeters);
    }
    if(!query.diagnostic.empty()) ImGui::TextWrapped("%s",query.diagnostic.c_str());
    if(!state.message.empty()) ImGui::TextWrapped("%s",state.message.c_str());
    ImGui::EndPopup();
}

bool VansSceneWindow::DrawNavigationPreview(INavigationEditorAPI& api,const glm::mat4& viewProjection,glm::vec2 origin,glm::vec2 size,bool interactive)
{
    if(!m_DebugViewState.navigationPreviewGizmos || !m_NavigationTools || size.x<=0 || size.y<=0) return false;
    auto& state=*m_NavigationTools;
    if(ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::GetIO().AppFocusLost) { state.pick=0; state.message.clear(); }
    const auto rows=glm::transpose(viewProjection);
    const std::array<glm::vec4,6> planes{rows[3]+rows[0],rows[3]-rows[0],rows[3]+rows[1],rows[3]-rows[1],rows[2],rows[3]-rows[2]};
    NavigationPreviewView view; view.previewId=state.request.previewId;
    view.profileId=state.request.profileId; view.filterId=state.request.filterId;
    view.maximumTiles=static_cast<std::size_t>(state.maximumTiles); view.maximumTriangles=static_cast<std::size_t>(state.maximumTriangles);
    view.maximumLinks=static_cast<std::size_t>(state.maximumLinks);
    for(std::size_t i=0;i<planes.size();++i)
    {
        const auto length=glm::length(glm::vec3(planes[i]));
        if(!std::isfinite(length) || length<=1.e-8f) { state.message="Navigation viewport camera is invalid"; return false; }
        const auto p=planes[i]/length; view.planes[i]={p.x,p.y,p.z,p.w};
    }
    auto* draw=ImGui::GetWindowDrawList();
    if(sizeof(ImDrawIdx)==2 && !(ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset))
    {
        // 给同一视口后续的既有 Gizmo/其他调试绘制留出空间。
        // 抗锯齿填充最多 6 个顶点，三条接缝各最多 8 个顶点。
        const int triangles=(static_cast<int>((std::numeric_limits<ImDrawIdx>::max)())-draw->VtxBuffer.Size-16384)/30;
        if(triangles<=0) { state.message="Navigation viewport vertex capacity is exhausted"; return false; }
        if(state.maximumTriangles>0) view.maximumTriangles=(std::min)(view.maximumTriangles,static_cast<std::size_t>(triangles));
    }
    const auto geometry=api.GetNavigationPreviewGeometry(view);
    if(!geometry.available)
    {
        state.pick=0;
        draw->AddText(ImVec2(origin.x+10,origin.y+10),IM_COL32(255,190,90,255),geometry.diagnostic.c_str());
        return false;
    }
    const auto screen=[&](const glm::vec3& point,ImVec2& result)
    {
        const auto clip=viewProjection*glm::vec4(point,1);
        if(!std::isfinite(clip.w) || clip.w<=1.e-5f) return false;
        const auto p=glm::vec3(clip)/clip.w;
        if(!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || p.z<0 || p.z>1) return false;
        result=ImVec2(origin.x+(p.x*.5f+.5f)*size.x,origin.y+(-p.y*.5f+.5f)*size.y); return true;
    };
    bool drawingLimited=false;
    const auto capacity=[&](int vertices)
    {
        if(sizeof(ImDrawIdx)>2 || (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset) ||
            draw->VtxBuffer.Size+vertices<=static_cast<int>((std::numeric_limits<ImDrawIdx>::max)())-16384) return true;
        drawingLimited=true; return false;
    };
    const auto text=[&](ImVec2 at,ImU32 color,const char* label)
    { if(capacity(static_cast<int>(std::strlen(label))*4)) draw->AddText(at,color,label); };
    const auto projectTriangle=[&](const NavigationPreviewTriangle& triangle,std::array<ImVec2,3>& points)
    {
        for(int corner=0;corner<3;++corner)
        { auto p=Position(triangle.positions[corner]); p.y+=.03f; if(!screen(p,points[corner])) return false; }
        return true;
    };
    const auto line=[&](glm::vec3 a,glm::vec3 b,ImU32 color,float width)
    { ImVec2 first,second; a.y+=.03f; b.y+=.03f; if(screen(a,first) && screen(b,second) && capacity(8)) draw->AddLine(first,second,color,width); };
    const auto marker=[&](const Vec3& p,ImU32 color,const char* label)
    {
        ImVec2 at; auto world=Position(p); world.y+=.04f;
        if(screen(world,at))
        { if(capacity(24)) draw->AddCircleFilled(at,4,color,12); text(ImVec2(at.x+7,at.y-8),color,label); }
    };
    draw->PushClipRect(ImVec2(origin.x,origin.y),ImVec2(origin.x+size.x,origin.y+size.y),true);
    for(const auto& triangle:geometry.triangles)
    {
        std::array<ImVec2,3> points;
        if(!projectTriangle(triangle,points)) continue;
        const auto color=triangle.traversable?IM_COL32(50+(triangle.area*37)%140,140+(triangle.area*19)%80,90+(triangle.area*29)%100,65):IM_COL32(190,65,65,90);
        draw->AddTriangleFilled(points[0],points[1],points[2],color);
        if(state.drawTiles) for(int edge=0;edge<3;++edge) if(triangle.tileEdges[edge])
            draw->AddLine(points[edge],points[(edge+1)%3],IM_COL32(90,205,255,160),1);
    }
    for(const auto& link:geometry.links)
    {
        const auto color=link.areaAllowed?IM_COL32(195,130,255,235):IM_COL32(140,140,140,200);
        line(Position(link.positions[0]),Position(link.positions[1]),color,2);
        ImVec2 first,second;
        auto start=Position(link.positions[0]),end=Position(link.positions[1]); start.y+=.03f; end.y+=.03f;
        if(!screen(start,first) || !screen(end,second)) continue;
        const auto arrow=[&](ImVec2 from,ImVec2 to)
        {
            glm::vec2 direction(to.x-from.x,to.y-from.y); const auto length=glm::length(direction);
            if(length<12 || !capacity(16)) return;
            direction/=length; const glm::vec2 side(-direction.y,direction.x),tip(to.x,to.y);
            const auto a=tip-direction*9.f+side*4.f,b=tip-direction*9.f-side*4.f;
            draw->AddLine(to,ImVec2(a.x,a.y),color,2); draw->AddLine(to,ImVec2(b.x,b.y),color,2);
        };
        arrow(first,second); if(link.bidirectional) arrow(second,first);
        const auto mouse=ImGui::GetIO().MousePos;
        if(glm::distance(glm::vec2(mouse.x,mouse.y),glm::vec2(first.x,first.y))<8 ||
            glm::distance(glm::vec2(mouse.x,mouse.y),glm::vec2(second.x,second.y))<8)
            ImGui::SetTooltip("Link %u: %s, radius %.2f m, Area %s",link.identity,link.bidirectional?"bidirectional":"one-way",
                link.radiusMeters,link.areaAllowed?"included":"excluded");
    }
    const auto query=api.GetNavigationPreviewQuery();
    const bool currentQuery=query.available && query.profileId==state.request.profileId && query.filterId==state.request.filterId;
    if(state.hasStart && (!currentQuery || Position(state.request.start)!=Position(query.requestedStart)))
        marker(state.request.start,IM_COL32(240,240,240,255),"selected start");
    if(state.hasGoal && NeedsGoal(state.request.kind) && (!currentQuery || Position(state.request.goal)!=Position(query.requestedEnd)))
        marker(state.request.goal,IM_COL32(245,175,90,255),"selected end");
    if(currentQuery)
    {
        marker(query.requestedStart,IM_COL32(240,240,240,255),"requested start");
        if(NeedsGoal(query.kind)) marker(query.requestedEnd,IM_COL32(245,175,90,255),"requested end");
        if(query.hasProjection)
        {
            marker(query.projectedStart,IM_COL32(75,210,255,255),"projected start");
            line(Position(query.requestedStart),Position(query.projectedStart),IM_COL32(90,195,245,220),1);
            if(query.kind==NavigationPreviewQueryKind::Path || query.kind==NavigationPreviewQueryKind::ReachablePoint)
            {
                marker(query.projectedEnd,IM_COL32(130,175,255,255),"projected end");
                line(Position(query.requestedEnd),Position(query.projectedEnd),IM_COL32(130,175,255,220),1);
            }
        }
        for(std::size_t i=1;i<query.points.size();++i)
        {
            line(Position(query.points[i-1]),Position(query.points[i]),IM_COL32(255,220,90,255),3);
            if(drawingLimited) break;
        }
        if(query.hasResult)
        {
            marker(query.reachedEnd,IM_COL32(120,245,150,255),query.result.c_str());
            if(query.kind==NavigationPreviewQueryKind::Raycast || query.kind==NavigationPreviewQueryKind::NearestBoundary)
                line(Position(query.projectedStart),Position(query.reachedEnd),IM_COL32(255,220,90,255),2);
            if(query.hit) line(Position(query.reachedEnd),Position(query.reachedEnd)+Position(query.normal),IM_COL32(255,95,95,255),2);
        }
    }
    char summary[256]; std::snprintf(summary,sizeof(summary),"Navigation %s / %s: %zu tiles, %zu triangles%s",state.request.profileId.c_str(),state.request.filterId.c_str(),geometry.tileCount,geometry.triangles.size(),(geometry.limited || drawingLimited)?" (limited)":"");
    text(ImVec2(origin.x+10,origin.y+10),geometry.limited?IM_COL32(255,190,90,255):IM_COL32(200,240,215,255),summary);
    if(!state.message.empty()) text(ImVec2(origin.x+10,origin.y+30),IM_COL32(245,230,190,255),state.message.c_str());
    draw->PopClipRect();
    const bool picking=state.pick!=0;
    if(picking && interactive && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::GetIO().KeyAlt &&
        !ImGui::GetDragDropPayload() && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const auto mouse=ImGui::GetMousePos(); glm::vec3 rayOrigin,rayDirection; float length=0;
        bool found=false; float nearest=0;
        if(Vans::VansSceneViewMath::BuildRay(viewProjection,{(mouse.x-origin.x)/size.x,(mouse.y-origin.y)/size.y},rayOrigin,rayDirection,length))
        {
            nearest=length;
            for(const auto& triangle:geometry.triangles)
            {
                std::array<ImVec2,3> projected;
                if(!projectTriangle(triangle,projected)) continue;
                const std::array<glm::vec3,3> points{Position(triangle.positions[0]),Position(triangle.positions[1]),Position(triangle.positions[2])};
                float distance=0;
                if(Vans::VansSceneViewMath::IntersectTriangle(rayOrigin,rayDirection,points,nearest,distance)) { found=true; nearest=distance; }
            }
        }
        if(found)
        {
            const auto p=rayOrigin+rayDirection*nearest;
            if(state.pick==1) { state.request.start={p.x,p.y,p.z}; state.hasStart=true; }
            else { state.request.goal={p.x,p.y,p.z}; state.hasGoal=true; }
            state.pick=0; state.message="Point selected. Run Query Preview in Navigation Tools.";
        }
        else state.message="No visible navigation triangle was hit. Adjust the view or snapshot limit.";
    }
    return picking;
}
}
