#include "VansGroundingTypes.h"

#include <cmath>
#include <unordered_set>

namespace VansGraphics
{
	bool VansCompileGroundingSettings(
		const VansGroundingSettings& settings,
		const VansCompiledAnimationRig& rig,
		VansCompiledGroundingSettings& outSettings,
		std::string& error)
	{
		outSettings = {};
		error.clear();
		if (settings.contacts.empty() || settings.query.profile.empty()
			|| settings.query.collisionMask == 0 || !std::isfinite(settings.weight)
			|| settings.weight < 0.0f || settings.weight > 1.0f)
		{
			error = "Grounding requires contacts, a resolved query profile, and weight in [0,1]";
			return false;
		}
		auto finiteNonNegative = [](float value)
		{ return std::isfinite(value) && value >= 0.0f; };
		if (!finiteNonNegative(settings.query.startDistanceAgainstApproach)
			|| !finiteNonNegative(settings.query.endDistanceAlongApproach)
			|| settings.query.startDistanceAgainstApproach
				+ settings.query.endDistanceAlongApproach <= 0.0f
			|| !finiteNonNegative(settings.query.maxStepUp)
			|| !finiteNonNegative(settings.query.maxStepDown)
			|| !finiteNonNegative(settings.query.maxPlaneResidual)
			|| !std::isfinite(settings.query.maxNormalDeviationDegrees)
			|| settings.query.maxNormalDeviationDegrees < 0.0f
			|| settings.query.maxNormalDeviationDegrees >= 90.0f
			|| !std::isfinite(settings.query.maxSlopeDegrees)
			|| settings.query.maxSlopeDegrees < 0.0f || settings.query.maxSlopeDegrees >= 90.0f
			|| !finiteNonNegative(settings.plant.unplantDistance)
			|| !finiteNonNegative(settings.plant.replantDistance)
			|| settings.plant.replantDistance > settings.plant.unplantDistance
			|| !finiteNonNegative(settings.plant.unplantAngleDegrees)
			|| !finiteNonNegative(settings.plant.replantAngleDegrees)
			|| settings.plant.unplantAngleDegrees > 180.0f
			|| settings.plant.replantAngleDegrees > settings.plant.unplantAngleDegrees
			|| !finiteNonNegative(settings.plant.weightHalfLife)
			|| !finiteNonNegative(settings.alignment.fullContactHeight)
			|| !finiteNonNegative(settings.alignment.contactFadeHeight)
			|| settings.alignment.contactFadeHeight <= settings.alignment.fullContactHeight
			|| !finiteNonNegative(settings.alignment.normalHalfLife)
			|| !std::isfinite(settings.alignment.rotationWeight)
			|| settings.alignment.rotationWeight < 0.0f
			|| settings.alignment.rotationWeight > 1.0f
			|| !std::isfinite(settings.plant.enterPhase) || !std::isfinite(settings.plant.exitPhase)
			|| settings.plant.enterPhase <= settings.plant.exitPhase
			|| settings.plant.exitPhase < 0.0f || settings.plant.enterPhase > 1.0f
			|| !finiteNonNegative(settings.pelvis.maxUpOffset)
			|| !finiteNonNegative(settings.pelvis.maxDownOffset)
			|| !finiteNonNegative(settings.pelvis.maxHorizontalOffset)
			|| !finiteNonNegative(settings.pelvis.halfLife)
			|| (settings.plant.pivot != VansPlantPivot::Heel
				&& settings.plant.pivot != VansPlantPivot::Ball
				&& settings.plant.pivot != VansPlantPivot::Ankle))
		{
			error = "Grounding query, alignment, plant hysteresis, or pelvis settings are invalid";
			return false;
		}
		if (settings.plant.lockEnabled && settings.plantSignal.empty())
		{
			error = "Grounding plant lock requires an explicit plantSignal";
			return false;
		}
		const auto pelvisFound = rig.semanticBoneIndices.find("pelvis");
		if (pelvisFound == rig.semanticBoneIndices.end())
		{
			error = "Grounding requires the Animation Rig semantic bone 'pelvis'";
			return false;
		}
		std::unordered_set<int> uniqueContacts;
		for (const std::string& id : settings.contacts)
		{
			const int contactIndex = rig.FindContact(id);
			if (contactIndex < 0 || !uniqueContacts.insert(contactIndex).second)
			{
				error = "Grounding contact ids must be unique and exist in the Animation Rig";
				return false;
			}
			const VansCompiledRigContact& contact = rig.contacts[static_cast<std::size_t>(contactIndex)];
			const VansCompiledRigChain& chain = rig.chains[static_cast<std::size_t>(contact.chainIndex)];
			int ancestor = chain.boneIndices.front();
			while (ancestor >= 0 && ancestor != pelvisFound->second)
				ancestor = rig.skeleton->bones[static_cast<std::size_t>(ancestor)].parentIndex;
			if (ancestor != pelvisFound->second)
			{
				error = "Grounding contact '" + id
					+ "' must use a Limb chain below the semantic pelvis";
				return false;
			}
			if (settings.plant.pivot == VansPlantPivot::Ball && contact.ballBoneIndex < 0)
			{
				error = "Grounding Ball pivot requires ballBone in contact '" + id + "'";
				return false;
			}
			outSettings.contactIndices.push_back(contactIndex);
		}
		outSettings.query = settings.query;
		outSettings.plantSignal = settings.plantSignal;
		outSettings.plant = settings.plant;
		outSettings.alignment = settings.alignment;
		outSettings.pelvis = settings.pelvis;
		outSettings.weight = settings.weight;
		return true;
	}
}
