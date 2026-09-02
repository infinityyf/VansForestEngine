#pragma once

#include "VansBaseWindowComponent.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace VansGraphics
{
class VansGAFDebuggerWindow final : public VansBaseWindowComponent
{
public:
	VansGAFDebuggerWindow();

	void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;
	void SetSimulationSourcePath(const std::string& sourcePath);

	static bool CombatOverlayEnabled();
	static bool ShowCombatSector();
	static bool ShowCombatWeaponPath();
	static bool ShowCombatHurtBodies();

private:
	void DrawRuntimeDebugger(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
	void DrawSimulator(Vans::EditorAPI::IEngineEditorAPI& editorAPI);

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
	std::array<char, 1024> m_SimulationSourcePath{};
	std::array<char, 256> m_SimulationAction{};
	std::array<char, 8192> m_SimulationPayload{};
	std::array<char, 128> m_SimulationNewTag{};
	std::array<char, 128> m_SimulationNewAttribute{};
	std::uint32_t m_SimulationNewTagCount = 1;
	double m_SimulationNewAttributeValue = 0.0;
	std::size_t m_SimulationStep = 0;
};
}
