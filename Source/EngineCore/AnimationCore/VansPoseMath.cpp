#include "VansPoseMath.h"

#include <../../GLM/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/matrix_decompose.hpp>
#include <../../GLM/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace VansGraphics::VansPoseMath
{
	namespace
	{
		constexpr float kTransformEpsilon = 1.0e-6f;

		bool IsFinite(const glm::mat4& matrix)
		{
			for (int column = 0; column < 4; ++column)
			{
				for (int row = 0; row < 4; ++row)
				{
					if (!std::isfinite(matrix[column][row]))
						return false;
				}
			}
			return true;
		}

		bool IsFinite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool IsFinite(const glm::quat& value)
		{
			return std::isfinite(value.w) && std::isfinite(value.x)
			    && std::isfinite(value.y) && std::isfinite(value.z);
		}

		glm::quat ShortestArcSlerp(glm::quat first, glm::quat second, float alpha)
		{
			first = glm::normalize(first);
			second = glm::normalize(second);
			if (glm::dot(first, second) < 0.0f)
				second = -second;
			return glm::normalize(glm::slerp(first, second, alpha));
		}
	}

	bool TryDecompose(const glm::mat4& matrix, VansBoneTransform& outTransform)
	{
		if (!IsFinite(matrix))
			return false;

		glm::vec3 skew(0.0f);
		glm::vec4 perspective(0.0f);
		VansBoneTransform decomposed;
		if (!glm::decompose(matrix, decomposed.scale, decomposed.rotation,
		                    decomposed.translation, skew, perspective))
			return false;
		if (!IsFinite(decomposed.translation) || !IsFinite(decomposed.scale)
		    || !IsFinite(decomposed.rotation)
		    || glm::dot(decomposed.rotation, decomposed.rotation) <= kTransformEpsilon)
			return false;

		decomposed.rotation = glm::normalize(decomposed.rotation);
		outTransform = decomposed;
		return true;
	}

	glm::mat4 Compose(const VansBoneTransform& transform)
	{
		return glm::translate(glm::mat4(1.0f), transform.translation)
		    * glm::toMat4(glm::normalize(transform.rotation))
		    * glm::scale(glm::mat4(1.0f), transform.scale);
	}

	VansBoneTransform BlendTransforms(const VansBoneTransform& first,
	                                  const VansBoneTransform& second,
	                                  float alpha)
	{
		const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
		VansBoneTransform blended;
		blended.translation = glm::mix(first.translation, second.translation, clampedAlpha);
		blended.rotation = ShortestArcSlerp(first.rotation, second.rotation, clampedAlpha);
		blended.scale = glm::mix(first.scale, second.scale, clampedAlpha);
		return blended;
	}

	VansBoneTransform ApplyAdditiveTransform(const VansBoneTransform& base,
	                                         const VansBoneTransform& additive,
	                                         float weight,
	                                         const VansBoneTransform& reference)
	{
		const float clampedWeight = std::clamp(weight, 0.0f, 1.0f);
		if (clampedWeight <= 0.0f)
			return base;
		if (std::fabs(reference.scale.x) <= kTransformEpsilon
		    || std::fabs(reference.scale.y) <= kTransformEpsilon
		    || std::fabs(reference.scale.z) <= kTransformEpsilon)
			return base;

		const glm::vec3 translationDelta = additive.translation - reference.translation;
		glm::quat rotationDelta = glm::normalize(glm::inverse(reference.rotation) * additive.rotation);
		if (rotationDelta.w < 0.0f)
			rotationDelta = -rotationDelta;
		const glm::quat weightedRotationDelta = ShortestArcSlerp(
			glm::quat(1.0f, 0.0f, 0.0f, 0.0f), rotationDelta, clampedWeight);
		const glm::vec3 scaleRatio = additive.scale / reference.scale;

		VansBoneTransform result;
		result.translation = base.translation + translationDelta * clampedWeight;
		result.rotation = glm::normalize(base.rotation * weightedRotationDelta);
		result.scale = base.scale * glm::mix(glm::vec3(1.0f), scaleRatio, clampedWeight);
		return result;
	}

	void BlendPoses(const VansAnimationFrameVector<VansBoneTransform>& first,
	                const VansAnimationFrameVector<VansBoneTransform>& second,
	                float alpha,
	                VansAnimationFrameVector<VansBoneTransform>& outPose)
	{
		const size_t count = (std::min)(first.size(), second.size());
		outPose.resize(count);
		for (size_t index = 0; index < count; ++index)
			outPose[index] = BlendTransforms(first[index], second[index], alpha);
	}

	void ApplyAdditivePose(const VansAnimationFrameVector<VansBoneTransform>& base,
	                       const VansAnimationFrameVector<VansBoneTransform>& additive,
	                       float weight,
	                       VansAnimationFrameVector<VansBoneTransform>& outPose,
	                       const VansAnimationFrameVector<VansBoneTransform>* referencePose)
	{
		const size_t count = (std::min)(base.size(), additive.size());
		outPose.resize(count);
		for (size_t index = 0; index < count; ++index)
		{
			const VansBoneTransform reference = referencePose && index < referencePose->size()
				? (*referencePose)[index]
				: VansBoneTransform{};
			outPose[index] = ApplyAdditiveTransform(base[index], additive[index], weight, reference);
		}
	}

	void ToMatrices(const VansAnimationFrameVector<VansBoneTransform>& pose,
	                std::vector<glm::mat4>& outMatrices)
	{
		outMatrices.resize(pose.size());
		for (size_t index = 0; index < pose.size(); ++index)
			outMatrices[index] = Compose(pose[index]);
	}

	bool FromMatrices(const std::vector<glm::mat4>& matrices,
	                  VansAnimationFrameVector<VansBoneTransform>& outPose)
	{
		VansAnimationFrameVector<VansBoneTransform> converted(matrices.size());
		for (size_t index = 0; index < matrices.size(); ++index)
		{
			if (!TryDecompose(matrices[index], converted[index]))
				return false;
		}
		outPose = std::move(converted);
		return true;
	}

	glm::mat4 BlendTransforms(const glm::mat4& first, const glm::mat4& second, float alpha)
	{
		const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
		if (clampedAlpha <= 0.0f)
			return first;
		if (clampedAlpha >= 1.0f)
			return second;

		VansBoneTransform firstTransform;
		VansBoneTransform secondTransform;
		if (!TryDecompose(first, firstTransform) || !TryDecompose(second, secondTransform))
			return clampedAlpha < 0.5f ? first : second;

		return Compose(BlendTransforms(firstTransform, secondTransform, clampedAlpha));
	}

	glm::mat4 ApplyAdditiveTransform(const glm::mat4& base,
	                                 const glm::mat4& additive,
	                                 float weight,
	                                 const glm::mat4& reference)
	{
		const float clampedWeight = std::clamp(weight, 0.0f, 1.0f);
		if (clampedWeight <= 0.0f)
			return base;

		VansBoneTransform baseTransform;
		VansBoneTransform additiveTransform;
		VansBoneTransform referenceTransform;
		if (!TryDecompose(base, baseTransform)
		    || !TryDecompose(additive, additiveTransform)
		    || !TryDecompose(reference, referenceTransform))
			return base;

		return Compose(ApplyAdditiveTransform(baseTransform, additiveTransform,
		                                      clampedWeight, referenceTransform));
	}

	void BlendPoses(const std::vector<glm::mat4>& first,
	                const std::vector<glm::mat4>& second,
	                float alpha,
	                std::vector<glm::mat4>& outPose)
	{
		const size_t count = (std::min)(first.size(), second.size());
		outPose.resize(count);
		for (size_t index = 0; index < count; ++index)
			outPose[index] = BlendTransforms(first[index], second[index], alpha);
	}

	void ApplyAdditivePose(const std::vector<glm::mat4>& base,
	                       const std::vector<glm::mat4>& additive,
	                       float weight,
	                       std::vector<glm::mat4>& outPose)
	{
		const size_t count = (std::min)(base.size(), additive.size());
		outPose.resize(count);
		for (size_t index = 0; index < count; ++index)
			outPose[index] = ApplyAdditiveTransform(base[index], additive[index], weight);
	}
}
