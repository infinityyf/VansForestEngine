#pragma once

#include "EngineDTOs.h"

#include <string>

namespace Vans::EditorAPI
{
	class IUIEditorAPI
	{
	public:
		virtual ~IUIEditorAPI() = default;
		virtual UIDocumentOpenResult OpenUIDocument(const std::string& path) = 0;
		virtual void CloseUIDocument(UIDocumentId documentId) = 0;
		virtual void SetUIDocumentVisible(UIDocumentId documentId, bool visible) = 0;
		virtual UIDocumentSnapshot GetUIDocumentSnapshot(UIDocumentId documentId) const = 0;
		virtual UIDiagnosticsSnapshot GetUIDiagnostics(UIDocumentId documentId) const = 0;
		virtual UIPreviewResult RequestUIPreview(const UIPreviewRequest& request) = 0;
		virtual EditorTextureHandle GetUIPreviewTexture(UIPreviewId id) const = 0;
	};
}
