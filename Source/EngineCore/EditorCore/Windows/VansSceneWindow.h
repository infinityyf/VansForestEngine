#pragma once
#include "VansBaseWindowComponent.h"
#include "../VansGizmos.h"
#include <string>
#include <vector>
#include <cstdint>
namespace VansGraphics
{
	class VansCamera;

	class VansSceneWindow : public VansBaseWindowComponent
	{
	public:

		void RegistCamera(VansCamera* camera)
		{
			m_Camera = camera;
		}

	private:

		VansGraphics::VansCamera* m_Camera = nullptr;
		std::uint32_t m_ViewportCandidateWidth = 0;
		std::uint32_t m_ViewportCandidateHeight = 0;
		std::uint32_t m_LastRequestedViewportWidth = 0;
		std::uint32_t m_LastRequestedViewportHeight = 0;
		std::uint32_t m_ViewportStableFrames = 0;

		VansGizmos m_Gizmos;
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;
	};
}
