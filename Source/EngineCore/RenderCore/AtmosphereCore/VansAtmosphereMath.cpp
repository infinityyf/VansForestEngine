#include "VansAtmosphereMath.h"

#include <algorithm>
#include <cmath>

namespace VansGraphics
{
namespace
{
	glm::dvec3 ToDouble3(const std::array<float, 3>& value)
	{
		return { value[0], value[1], value[2] };
	}

	glm::dvec3 PlanetCenter(const Vans::VansScenePlanetSettingsConfig& planet)
	{
		return {
			planet.centerWorldMeters[0],
			planet.centerWorldMeters[1],
			planet.centerWorldMeters[2]
		};
	}

	double SmoothStep(double edge0, double edge1, double value)
	{
		if (edge1 <= edge0)
			return value >= edge1 ? 1.0 : 0.0;
		const double t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
		return t * t * (3.0 - 2.0 * t);
	}

	VansAtmosphereMediumSample EvaluatePhysicalMedium(
		const Vans::VansSceneEnvironmentSettingsConfig& environment,
		double altitudeMeters)
	{
		VansAtmosphereMediumSample result;
		const auto& atmosphere = environment.physicalAtmosphere;
		if (!atmosphere.enabled || altitudeMeters < 0.0 ||
			altitudeMeters > environment.planet.atmosphereHeightMeters)
		{
			return result;
		}

		const double rayleighDensity = std::exp(-altitudeMeters /
			static_cast<double>(atmosphere.rayleigh.densityScaleHeightMeters));
		const double mieDensity = std::exp(-altitudeMeters /
			static_cast<double>(atmosphere.mie.densityScaleHeightMeters));
		const double ozoneDensity = std::max(
			1.0 - std::abs(altitudeMeters -
				static_cast<double>(atmosphere.ozone.centerAltitudeMeters)) /
				static_cast<double>(atmosphere.ozone.halfWidthMeters),
			0.0);
		const glm::dvec3 rayleigh =
			ToDouble3(atmosphere.rayleigh.scatteringPerMeterAtGround) *
			rayleighDensity;
		const glm::dvec3 mieScattering =
			ToDouble3(atmosphere.mie.scatteringPerMeterAtGround) * mieDensity;
		const glm::dvec3 mieAbsorption =
			ToDouble3(atmosphere.mie.absorptionPerMeterAtGround) * mieDensity;
		const glm::dvec3 ozoneAbsorption =
			ToDouble3(atmosphere.ozone.absorptionPerMeter) * ozoneDensity;
		result.scatteringPerMeter = rayleigh + mieScattering;
		result.extinctionPerMeter =
			rayleigh + mieScattering + mieAbsorption + ozoneAbsorption;
		return result;
	}

	VansAtmosphereMediumSample EvaluateHeightFogSegment(
		const Vans::VansSceneEnvironmentSettingsConfig& environment,
		const glm::dvec3& rayOriginWorldMeters,
		const glm::dvec3& rayDirection,
		double segmentStartDistanceMeters,
		double segmentEndDistanceMeters)
	{
		VansAtmosphereMediumSample result;
		const auto& fog = environment.heightFog;
		const double segmentLength =
			std::max(segmentEndDistanceMeters - segmentStartDistanceMeters, 0.0);
		if (!fog.enabled || segmentLength <= 0.0)
			return result;

		// 距离淡入/淡出与垂直指数密度相乘，数值参考积分避免在淡变边界
		// 引入另一套近似公式。GPU Froxel 使用同一中点采样语义。
		constexpr std::uint32_t SampleCount = 16;
		const glm::dvec3 direction = glm::normalize(rayDirection);
		double integratedDensity = 0.0;
		for (std::uint32_t index = 0; index < SampleCount; ++index)
		{
			const double u = (static_cast<double>(index) + 0.5) /
				static_cast<double>(SampleCount);
			const double distance = segmentStartDistanceMeters +
				(segmentEndDistanceMeters - segmentStartDistanceMeters) * u;
			const double height = rayOriginWorldMeters.y + direction.y * distance;
			integratedDensity += EvaluateHeightFogDensity(fog, height) *
				EvaluateHeightFogDistanceWeight(fog, distance);
		}
		integratedDensity *= segmentLength / static_cast<double>(SampleCount);
		const double opticalDepth =
			integratedDensity / static_cast<double>(fog.visibilityAtGroundMeters);
		const double averageExtinction = opticalDepth / segmentLength;
		result.extinctionPerMeter = glm::dvec3(averageExtinction);
		result.scatteringPerMeter =
			averageExtinction * ToDouble3(fog.singleScatteringAlbedo);
		return result;
	}

	VansAtmosphereMediumSample CombineMedium(
		const VansAtmosphereMediumSample& first,
		const VansAtmosphereMediumSample& second)
	{
		VansAtmosphereMediumSample result;
		result.scatteringPerMeter =
			first.scatteringPerMeter + second.scatteringPerMeter;
		result.extinctionPerMeter =
			first.extinctionPerMeter + second.extinctionPerMeter;
		return result;
	}
}

std::optional<VansRaySphereInterval> IntersectRaySphere(
	const glm::dvec3& rayOrigin,
	const glm::dvec3& rayDirection,
	const glm::dvec3& sphereCenter,
	double sphereRadiusMeters)
{
	const glm::dvec3 direction = glm::normalize(rayDirection);
	const glm::dvec3 relativeOrigin = rayOrigin - sphereCenter;
	const double projected = glm::dot(relativeOrigin, direction);
	const double discriminant = projected * projected -
		(glm::dot(relativeOrigin, relativeOrigin) -
			sphereRadiusMeters * sphereRadiusMeters);
	if (discriminant < 0.0)
		return std::nullopt;
	const double root = std::sqrt(discriminant);
	const double entry = -projected - root;
	const double exit = -projected + root;
	if (exit < 0.0)
		return std::nullopt;
	return VansRaySphereInterval{ std::max(entry, 0.0), exit };
}

double ComputeRadialAltitudeMeters(
	const Vans::VansScenePlanetSettingsConfig& planet,
	const glm::dvec3& worldPositionMeters)
{
	return glm::length(worldPositionMeters - PlanetCenter(planet)) -
		planet.bottomRadiusMeters;
}

VansAtmosphereMediumSample EvaluateAtmosphereMedium(
	const Vans::VansSceneEnvironmentSettingsConfig& environment,
	double altitudeMeters)
{
	return EvaluatePhysicalMedium(environment, altitudeMeters);
}

double EvaluateHeightFogDensity(
	const Vans::VansSceneHeightFogSettingsConfig& fog,
	double worldHeightMeters)
{
	const double heightAboveGround = std::max(
		worldHeightMeters - static_cast<double>(fog.groundHeightWorldMeters), 0.0);
	return std::exp(-heightAboveGround /
		static_cast<double>(fog.densityFalloffHeightMeters));
}

double EvaluateHeightFogDistanceWeight(
	const Vans::VansSceneHeightFogSettingsConfig& fog,
	double distanceMeters)
{
	const double start = static_cast<double>(fog.startDistanceMeters);
	const double maximum = static_cast<double>(fog.maximumDistanceMeters);
	if (distanceMeters < start || distanceMeters > maximum)
		return 0.0;
	const double nearWeight = fog.nearFadeDistanceMeters > 0.0f
		? SmoothStep(start, start + fog.nearFadeDistanceMeters, distanceMeters)
		: 1.0;
	const double farWeight = fog.farFadeDistanceMeters > 0.0f
		? 1.0 - SmoothStep(maximum - fog.farFadeDistanceMeters,
			maximum, distanceMeters)
		: 1.0;
	return nearWeight * farWeight;
}

double ComputeHeightFogSegmentOpticalDepth(
	const Vans::VansSceneEnvironmentSettingsConfig& environment,
	const glm::dvec3& rayOriginWorldMeters,
	const glm::dvec3& rayDirection,
	double segmentStartDistanceMeters,
	double segmentEndDistanceMeters)
{
	const VansAtmosphereMediumSample medium = EvaluateHeightFogSegment(
		environment, rayOriginWorldMeters, rayDirection,
		segmentStartDistanceMeters, segmentEndDistanceMeters);
	return medium.extinctionPerMeter.x *
		std::max(segmentEndDistanceMeters - segmentStartDistanceMeters, 0.0);
}

VansAtmosphereMediumSample EvaluatePhysicalAerialPerspectiveSegmentMedium(
	const Vans::VansSceneEnvironmentSettingsConfig& environment,
	const glm::dvec3& rayOriginWorldMeters,
	const glm::dvec3& rayDirection,
	double segmentStartDistanceMeters,
	double segmentEndDistanceMeters)
{
	const glm::dvec3 direction = glm::normalize(rayDirection);
	const double midpointDistance =
		0.5 * (segmentStartDistanceMeters + segmentEndDistanceMeters);
	const glm::dvec3 midpoint =
		rayOriginWorldMeters + direction * midpointDistance;
	return EvaluatePhysicalMedium(environment,
		ComputeRadialAltitudeMeters(environment.planet, midpoint));
}

VansAtmosphereMediumSample EvaluateNearMediaSegmentMedium(
	const Vans::VansSceneEnvironmentSettingsConfig& environment,
	const glm::dvec3& rayOriginWorldMeters,
	const glm::dvec3& rayDirection,
	double segmentStartDistanceMeters,
	double segmentEndDistanceMeters)
{
	return CombineMedium(
		EvaluatePhysicalAerialPerspectiveSegmentMedium(
			environment, rayOriginWorldMeters, rayDirection,
			segmentStartDistanceMeters, segmentEndDistanceMeters),
		EvaluateHeightFogSegment(
			environment, rayOriginWorldMeters, rayDirection,
			segmentStartDistanceMeters, segmentEndDistanceMeters));
}

glm::dvec3 IntegrateAtmosphereOpticalDepth(
	const Vans::VansSceneEnvironmentSettingsConfig& environment,
	const glm::dvec3& rayOriginWorldMeters,
	const glm::dvec3& rayDirection,
	double distanceMeters,
	std::uint32_t sampleCount)
{
	if (distanceMeters <= 0.0 || sampleCount == 0)
		return glm::dvec3(0.0);
	const glm::dvec3 direction = glm::normalize(rayDirection);
	const double stepMeters = distanceMeters / static_cast<double>(sampleCount);
	glm::dvec3 opticalDepth(0.0);
	for (std::uint32_t index = 0; index < sampleCount; ++index)
	{
		const double segmentStart = static_cast<double>(index) * stepMeters;
		const double segmentEnd = segmentStart + stepMeters;
		opticalDepth += EvaluatePhysicalAerialPerspectiveSegmentMedium(
			environment, rayOriginWorldMeters, direction,
			segmentStart, segmentEnd).extinctionPerMeter * stepMeters;
	}
	return opticalDepth;
}

VansAtmosphereInterval ExtractAtmosphereInterval(
	const VansAtmosphereInterval& cumulativeNear,
	const VansAtmosphereInterval& cumulativeFar)
{
	const glm::dvec3 nearTransmittance =
		glm::exp(-cumulativeNear.opticalDepth);
	VansAtmosphereInterval interval;
	interval.opticalDepth = glm::max(
		cumulativeFar.opticalDepth - cumulativeNear.opticalDepth,
		glm::dvec3(0.0));
	interval.scattering = glm::max(
		cumulativeFar.scattering - cumulativeNear.scattering,
		glm::dvec3(0.0)) /
		glm::max(nearTransmittance, glm::dvec3(1.0e-12));
	return interval;
}

VansAtmosphereInterval ComposeAtmosphereIntervals(
	const VansAtmosphereInterval& nearInterval,
	const VansAtmosphereInterval& farInterval)
{
	VansAtmosphereInterval combined;
	combined.scattering = nearInterval.scattering +
		glm::exp(-nearInterval.opticalDepth) * farInterval.scattering;
	combined.opticalDepth =
		nearInterval.opticalDepth + farInterval.opticalDepth;
	return combined;
}

double EncodeAerialPerspectiveSliceDistance(
	double normalizedSlice,
	double maxDistanceMeters)
{
	if (maxDistanceMeters <= 0.0)
		return 0.0;
	const double u = std::clamp(normalizedSlice, 0.0, 1.0);
	return u * u * maxDistanceMeters;
}

double DecodeAerialPerspectiveSliceDistance(
	double distanceMeters,
	double maxDistanceMeters)
{
	if (maxDistanceMeters <= 0.0)
		return 0.0;
	return std::sqrt(
		std::clamp(distanceMeters / maxDistanceMeters, 0.0, 1.0));
}

glm::dvec3 CompositeAtmosphereInterval(
	const glm::dvec3& backgroundRadiance,
	const VansAtmosphereInterval& interval)
{
	return interval.scattering +
		backgroundRadiance * glm::exp(-interval.opticalDepth);
}
}
