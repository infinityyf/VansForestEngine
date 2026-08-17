#include "../TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "VansTimelineExtensionContributors.h"
#include "../TimelineRuntime/VansTimelineBuiltInRegistry.h"
#include "../RenderCore/Timeline/VansCameraTimelineIntegration.h"
#include "../ParticleCore/Timeline/VansParticleTimelineIntegration.h"
#include "../RuntimeUI/Timeline/VansUITimelineIntegration.h"
#include "../AnimationCore/Timeline/VansAnimationTimelineIntegration.h"
#include "../GameplayActionTimeline/VansGameplayActionTimelineIntegration.h"
#include "../RenderCore/Timeline/VansMediaTimelineIntegration.h"
#include "../RenderCore/Timeline/VansRenderPropertyTimelineIntegration.h"

#include <stdexcept>

namespace Vans
{
bool VansRegisterSceneTimelineExtensions(VansTimelineTrackExtensionRegistry&, std::string&);
bool VansRegisterAudioTimelineExtensions(VansTimelineTrackExtensionRegistry&, std::string&);

VansTimelineTrackExtensionRegistry& VansTimelineTrackExtensionRegistry::BuiltIns()
{
	static VansTimelineTrackExtensionRegistry registry;
	static const bool initialized = []
	{
		std::string error;
		auto& contributors = VansTimelineExtensionContributors::Startup();
		if (!contributors.Register("Timeline.Runtime", VansRegisterTimelineRuntimeExtensions, error) ||
			!contributors.Register("Timeline.Scene", VansRegisterSceneTimelineExtensions, error) ||
			!contributors.Register("Timeline.Animation", VansRegisterAnimationTimelineExtensions, error) ||
			!contributors.Register("Timeline.Audio", VansRegisterAudioTimelineExtensions, error) ||
			!contributors.Register("Timeline.Particle", VansRegisterParticleTimelineExtensions, error) ||
			!contributors.Register("Timeline.Render", VansGraphics::VansRegisterRenderTimelineExtensions, error) ||
			!contributors.Register("Timeline.RenderProperty", VansGraphics::VansRegisterRenderPropertyTimelineExtensions, error) ||
			!contributors.Register("Timeline.Media", VansGraphics::VansRegisterMediaTimelineExtension, error) ||
			!contributors.Register("Timeline.UI", VansRegisterUITimelineExtensions, error) ||
			!contributors.Register("Timeline.GameplayAction", VansRegisterGameplayActionTimelineExtensions, error) ||
			!contributors.ApplyAndSeal(registry, error))
			throw std::logic_error(error);
		return true;
	}();
	(void)initialized;
	return registry;
}
}
