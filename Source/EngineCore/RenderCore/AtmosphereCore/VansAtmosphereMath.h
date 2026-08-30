#pragma once

#include "VansAtmosphereTypes.h"

#include <optional>

namespace VansGraphics
{
	struct VansRaySphereInterval
	{
		double entryMeters = 0.0;
		double exitMeters = 0.0;
	};

	std::optional<VansRaySphereInterval> IntersectRaySphere(
		const glm::dvec3& rayOrigin,
		const glm::dvec3& rayDirection,
		const glm::dvec3& sphereCenter,
		double sphereRadiusMeters);

	double ComputeRadialAltitudeMeters(
		const Vans::VansScenePlanetSettingsConfig& planet,
		const glm::dvec3& worldPositionMeters);

	VansAtmosphereMediumSample EvaluateAtmosphereMedium(
		const Vans::VansSceneEnvironmentSettingsConfig& environment,
		double altitudeMeters);
	double EvaluateHeightFogDensity(
		const Vans::VansSceneHeightFogSettingsConfig& fog,
		double worldHeightMeters);
	double EvaluateHeightFogDistanceWeight(
		const Vans::VansSceneHeightFogSettingsConfig& fog,
		double distanceMeters);
	double ComputeHeightFogSegmentOpticalDepth(
		const Vans::VansSceneEnvironmentSettingsConfig& environment,
		const glm::dvec3& rayOriginWorldMeters,
		const glm::dvec3& rayDirection,
		double segmentStartDistanceMeters,
		double segmentEndDistanceMeters);
	VansAtmosphereMediumSample EvaluatePhysicalAerialPerspectiveSegmentMedium(
		const Vans::VansSceneEnvironmentSettingsConfig& environment,
		const glm::dvec3& rayOriginWorldMeters,
		const glm::dvec3& rayDirection,
		double segmentStartDistanceMeters,
		double segmentEndDistanceMeters);
	VansAtmosphereMediumSample EvaluateNearMediaSegmentMedium(
		const Vans::VansSceneEnvironmentSettingsConfig& environment,
		const glm::dvec3& rayOriginWorldMeters,
		const glm::dvec3& rayDirection,
		double segmentStartDistanceMeters,
		double segmentEndDistanceMeters);

	glm::dvec3 IntegrateAtmosphereOpticalDepth(
		const Vans::VansSceneEnvironmentSettingsConfig& environment,
		const glm::dvec3& rayOriginWorldMeters,
		const glm::dvec3& rayDirection,
		double distanceMeters,
		std::uint32_t sampleCount);

	VansAtmosphereInterval ExtractAtmosphereInterval(
		const VansAtmosphereInterval& cumulativeNear,
		const VansAtmosphereInterval& cumulativeFar);

	VansAtmosphereInterval ComposeAtmosphereIntervals(
		const VansAtmosphereInterval& nearInterval,
		const VansAtmosphereInterval& farInterval);

	double EncodeAerialPerspectiveSliceDistance(
		double normalizedSlice,
		double maxDistanceMeters);

	double DecodeAerialPerspectiveSliceDistance(
		double distanceMeters,
		double maxDistanceMeters);

	glm::dvec3 CompositeAtmosphereInterval(
		const glm::dvec3& backgroundRadiance,
		const VansAtmosphereInterval& interval);
}
