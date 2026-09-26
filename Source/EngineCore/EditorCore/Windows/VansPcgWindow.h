#pragma once
#include "VansBaseWindowComponent.h"

namespace Vans::EditorAPI { class IAssetEditorAPI; class IPcgEditorAPI; }

namespace VansGraphics
{
    class VansPcgWindow : public VansBaseWindowComponent
    {
    private:
        int m_Category = -1;
        Vans::EditorAPI::PcgSplineItem m_SplineDraft;
        std::uint64_t m_SplineDraftState=0;
        bool m_SplinePropertyDrag=false;
        std::string m_RoadMaterial;
        std::array<float,3> m_NewSplinePosition{};
        std::array<float,4> m_SplineFieldSettings{.5f,.5f,.025f,1.f};
        std::uint64_t m_SplineFieldSettingsState=0;
        void ShowSplines(Vans::EditorAPI::IEngineEditorAPI&,Vans::EditorAPI::PcgSplineKind);
        bool m_BrushDraftEditing = false;
        std::string m_Message;
        Vans::EditorAPI::PcgBrushTarget m_BrushTarget;
        Vans::EditorAPI::PcgBrushSettings m_BrushDraft;
        Vans::EditorAPI::PcgBrushTarget m_ConfigurationTarget;
        Vans::EditorAPI::PcgPlantConfiguration m_PlantDraft;
        Vans::EditorAPI::PcgLayerConfiguration m_LayerDraft;
        Vans::EditorAPI::PcgLayerCreateRequest m_CreateDraft;
        Vans::EditorAPI::PcgBrushTarget m_CanvasTarget;
        Vans::EditorAPI::PcgMaskDataRequest m_MaskDataDraft;
        bool m_CanvasDragging=false;
        float m_CanvasZoom=1;
        std::array<float,2> m_CanvasPan{};
		void ShowMaskCanvas(Vans::EditorAPI::IPcgEditorAPI&,const Vans::EditorAPI::PcgBrushSnapshot&);
        Vans::EditorAPI::PcgBrushTarget m_InstanceTarget;
        Vans::EditorAPI::PcgInstanceSnapshot m_Instances;
        Vans::EditorAPI::PcgInstanceItem m_InstanceDraft;
		void ShowInstances(Vans::EditorAPI::IPcgEditorAPI&,const Vans::EditorAPI::PcgBrushSnapshot&);
		void ShowLayerActions(Vans::EditorAPI::IPcgEditorAPI&,Vans::EditorAPI::IAssetEditorAPI&,
			const Vans::EditorAPI::PcgEditorSnapshot&,int category);
		void ShowConfiguration(Vans::EditorAPI::IPcgEditorAPI&,Vans::EditorAPI::IAssetEditorAPI&,
			const Vans::EditorAPI::PcgLayerSnapshot&);
        void ShowWindow(Vans::EditorAPI::IEngineEditorAPI&) override;
    };
}
