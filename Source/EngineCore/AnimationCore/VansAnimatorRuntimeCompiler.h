#pragma once

#include "VansAnimatorIO.h"

#include <filesystem>
#include <functional>
#include <memory>

namespace VansGraphics
{
	enum class VansAnimatorRuntimeCompileMode
	{
		FullGraph,
		ExternalPoseTarget
	};

	struct VansAnimatorRuntimeCompileOptions
	{
		VansAnimatorRuntimeCompileMode mode = VansAnimatorRuntimeCompileMode::FullGraph;
		bool enableTargetPostProcess = true;
		bool enableRootMotion = false;
		bool enableDebugMetrics = false;
	};

	using VansAnimatorClipResolver = std::function<bool(
		const AnimatorClipRef&,
		std::filesystem::path&,
		std::string&)>;
	using VansAnimatorMaskResolver = std::function<bool(
		const VansAnimationLayerDefinition&,
		std::filesystem::path&,
		std::string&)>;

	// The single Definition -> runtime Instance boundary shared by Scene build,
	// Retarget sources and isolated editor preview sessions.
	class VansAnimatorRuntimeCompiler
	{
	public:
		static std::unique_ptr<VansAnimationController> Compile(
			const AnimatorAssetData& asset,
			const Skeleton& skeleton,
			const VansAnimatorClipResolver& clipResolver,
			const VansAnimatorMaskResolver& maskResolver,
			const VansAnimatorRuntimeCompileOptions& options,
			std::string& error);
	};
}
