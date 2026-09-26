#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace VansGraphics
{
enum class VansEditorWindowId : std::uint8_t
{
	Light,
	Scriptor,
	Console,
	Profiler,
	ProjectSettings,
	UIEditor,
	AudioDebug,
	GAFDebugger,
	AIDebug,
	SkeletonDebug,
	MotionMatchingDebug,
	GBuffer,
	ShadowDebugger,
	WaterGBuffer,
	RenderDebug,
	ParticleDebug,
	HiZCull,
	HairDebug,
	Water,
	Terrain,
	Pcg,
	PostProcess,
	ReflectionProbe,
	GI,
	Count
};

enum class VansEditorWindowMenuGroup : std::uint8_t
{
	General,
	Animation,
	Rendering
};

struct VansEditorWindowDescriptor
{
	VansEditorWindowId id;
	std::string_view configurationKey;
	std::string_view menuLabel;
	VansEditorWindowMenuGroup menuGroup;
};

class VansEditorWindowCatalog final
{
public:
	bool IsOpen(VansEditorWindowId id) const { return m_Open[ToIndex(id)]; }
	bool* OpenState(VansEditorWindowId id) { return &m_Open[ToIndex(id)]; }
	void ApplyDefaults(const std::array<bool, static_cast<std::size_t>(VansEditorWindowId::Count)>& defaults)
	{
		m_Open = defaults;
	}

	static constexpr const std::array<VansEditorWindowDescriptor, 24>& All()
	{
		return Descriptors;
	}

private:
	static constexpr std::size_t ToIndex(VansEditorWindowId id)
	{
		return static_cast<std::size_t>(id);
	}

	inline static constexpr std::array<VansEditorWindowDescriptor, 24> Descriptors{{
		{ VansEditorWindowId::Light, "Light", "Light", VansEditorWindowMenuGroup::General },
		{ VansEditorWindowId::Scriptor, "Scriptor", "Scripts", VansEditorWindowMenuGroup::General },
		{ VansEditorWindowId::Console, "Console", "Console", VansEditorWindowMenuGroup::General },
		{ VansEditorWindowId::Profiler, "Profiler", "Profiler", VansEditorWindowMenuGroup::General },
		{ VansEditorWindowId::ProjectSettings, "ProjectSettings", "Project Settings", VansEditorWindowMenuGroup::General },
		{ VansEditorWindowId::UIEditor, "UIEditor", "UI Editor", VansEditorWindowMenuGroup::General },
		{ VansEditorWindowId::AudioDebug, "AudioDebug", "Audio Debug", VansEditorWindowMenuGroup::General },
		{ VansEditorWindowId::GAFDebugger, "GAFDebugger", "GAF Debugger", VansEditorWindowMenuGroup::General },
		{ VansEditorWindowId::AIDebug, "AIDebug", "AI Debug", VansEditorWindowMenuGroup::General },
		{ VansEditorWindowId::SkeletonDebug, "SkeletonDebug", "Skeleton Debug", VansEditorWindowMenuGroup::Animation },
		{ VansEditorWindowId::MotionMatchingDebug, "MotionMatchingDebug", "Motion Matching Debug", VansEditorWindowMenuGroup::Animation },
		{ VansEditorWindowId::GBuffer, "GBuffer", "GBuffer Visualization", VansEditorWindowMenuGroup::Rendering },
		{ VansEditorWindowId::ShadowDebugger, "ShadowDebugger", "Shadow Debugger", VansEditorWindowMenuGroup::Rendering },
		{ VansEditorWindowId::WaterGBuffer, "WaterGBuffer", "Water GBuffer Visualization", VansEditorWindowMenuGroup::Rendering },
		{ VansEditorWindowId::RenderDebug, "RenderDebug", "Render Debug", VansEditorWindowMenuGroup::Rendering },
		{ VansEditorWindowId::ParticleDebug, "ParticleDebug", "Particle Debug", VansEditorWindowMenuGroup::Rendering },
		{ VansEditorWindowId::HiZCull, "HiZCull", "HiZ Occlusion Culling", VansEditorWindowMenuGroup::Rendering },
		{ VansEditorWindowId::HairDebug, "HairDebug", "Hair Debug", VansEditorWindowMenuGroup::Rendering },
		{ VansEditorWindowId::Water, "Water", "Water", VansEditorWindowMenuGroup::Rendering },
		{ VansEditorWindowId::Terrain, "Terrain", "Terrain", VansEditorWindowMenuGroup::Rendering },
		{ VansEditorWindowId::Pcg, "Pcg", "PCG", VansEditorWindowMenuGroup::Rendering },
		{ VansEditorWindowId::PostProcess, "PostProcess", "Post Process", VansEditorWindowMenuGroup::Rendering },
		{ VansEditorWindowId::ReflectionProbe, "ReflectionProbe", "Reflection Probe Inspector", VansEditorWindowMenuGroup::Rendering },
		{ VansEditorWindowId::GI, "GI", "GI Inspector", VansEditorWindowMenuGroup::Rendering }
	}};

	std::array<bool, static_cast<std::size_t>(VansEditorWindowId::Count)> m_Open{};
};
}
