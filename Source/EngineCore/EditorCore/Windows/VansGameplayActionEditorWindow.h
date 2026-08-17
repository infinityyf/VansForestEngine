#pragma once

#include "VansBaseWindowComponent.h"

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
class VansGameplayActionEditorWindow final : public VansBaseWindowComponent
{
public:
	void Open(const std::string& sourcePath);
	void Close();
	bool IsOpen() const { return m_IsOpen; }
	void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;

private:
	void Refresh(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
	void ApplyOperation(const Vans::EditorAPI::GAFEditorOperationResult& result);
	void DrawMenuBar(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
	void DrawToolbar(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
	void DrawOverview();
	void DrawProperties(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
	void DrawProperty(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI,
		Vans::EditorAPI::GAFEditorFieldSnapshot field);
	void DrawStructuredChildren(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI,
		const std::string& tableId,
		const std::vector<Vans::EditorAPI::GAFEditorFieldSnapshot>& children);
	void DrawDiagnostics() const;
	void DrawDiff(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
	void DrawDebugger(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
	void DrawSimulator(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
	void DrawGraph(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
	void DrawGraphPropertyEditor(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
	void ApplyGraphOperation(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI,
		Vans::EditorAPI::GAFGraphEditRequest request);
	void OpenStructuredEditor(const Vans::EditorAPI::GAFEditorFieldSnapshot& field);
	void DrawStructuredEditor(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
	void DrawCloseConfirmation(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
	void SetField(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI,
		const std::string& path,
		Vans::EditorAPI::GAFEditorValue value);

	bool m_IsOpen = false;
	bool m_NeedsRefresh = false;
	bool m_CloseRequested = false;
	std::string m_Path;
	std::string m_BaselineCanonicalJson;
	std::string m_LastError;
	Vans::EditorAPI::GAFEditorDocumentSnapshot m_Document;
	Vans::EditorAPI::GAFSemanticDiffResult m_Diff;
	std::unordered_map<std::string, std::size_t> m_ArraySelection;
	std::vector<std::string> m_TagCatalog;
	bool m_StructuredEditorOpen = false;
	std::string m_StructuredPath;
	Vans::EditorAPI::GAFEditorValueKind m_StructuredKind =
		Vans::EditorAPI::GAFEditorValueKind::Object;
	std::vector<char> m_StructuredBuffer;
	std::vector<Vans::EditorAPI::GAFGraphNodeTypeSnapshot> m_GraphCatalog;
	std::string m_SelectedGraphNode;
	std::string m_GraphConnectOutput;
	std::string m_GraphConnectTarget;
	char m_GraphSearch[128]{};
	float m_GraphPanX = 30.0f;
	float m_GraphPanY = 30.0f;
	float m_GraphZoom = 1.0f;
	std::unordered_map<std::string, std::array<float, 2>> m_GraphDragPositions;
	bool m_GraphPropertyEditorOpen = false;
	std::string m_GraphPropertyNode;
	std::string m_GraphPropertyName;
	Vans::EditorAPI::GAFEditorValueKind m_GraphPropertyKind =
		Vans::EditorAPI::GAFEditorValueKind::Json;
	std::vector<char> m_GraphPropertyBuffer;
	Vans::EditorAPI::GAFRuntimeDebugSnapshot m_DebugSnapshot;
	std::array<char, 1024> m_TracePath{};
	std::array<char, 128> m_DebugFilter{};
	std::string m_DebugMessage;
	std::vector<Vans::EditorAPI::GAFDebugBreakpointSnapshot> m_DebugBreakpoints;
	std::array<char, 256> m_DebugBreakpointExpression{};
	int m_DebugBreakpointKind = 0;
	int m_DebugBreakpointComparison = 0;
	double m_DebugBreakpointValue = 0.0;
	double m_DebugBreakpointEpsilon = 1e-6;
	Vans::EditorAPI::GAFSimulationRequest m_SimulationRequest;
	Vans::EditorAPI::GAFSimulationResult m_SimulationResult;
	std::array<char, 256> m_SimulationAction{};
	std::array<char, 8192> m_SimulationPayload{};
	std::array<char, 128> m_SimulationNewTag{};
	std::array<char, 128> m_SimulationNewAttribute{};
	std::uint32_t m_SimulationNewTagCount = 1;
	double m_SimulationNewAttributeValue = 0.0;
	std::size_t m_SimulationStep = 0;
};
}
