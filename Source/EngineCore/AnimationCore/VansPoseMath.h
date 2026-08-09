#pragma once

#include "VansPoseTypes.h"

#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>

#include <vector>

namespace VansGraphics::VansPoseMath
{
	bool TryDecompose(const glm::mat4& matrix, VansBoneTransform& outTransform);
	glm::mat4 Compose(const VansBoneTransform& transform);

	VansBoneTransform BlendTransforms(const VansBoneTransform& first,
	                                  const VansBoneTransform& second,
	                                  float alpha);
	VansBoneTransform ApplyAdditiveTransform(const VansBoneTransform& base,
	                                         const VansBoneTransform& additive,
	                                         float weight,
	                                         const VansBoneTransform& reference = {});

	void BlendPoses(const VansAnimationFrameVector<VansBoneTransform>& first,
	                const VansAnimationFrameVector<VansBoneTransform>& second,
	                float alpha,
	                VansAnimationFrameVector<VansBoneTransform>& outPose);
	void ApplyAdditivePose(const VansAnimationFrameVector<VansBoneTransform>& base,
	                       const VansAnimationFrameVector<VansBoneTransform>& additive,
	                       float weight,
	                       VansAnimationFrameVector<VansBoneTransform>& outPose,
	                       const VansAnimationFrameVector<VansBoneTransform>* referencePose = nullptr);

	void ToMatrices(const VansAnimationFrameVector<VansBoneTransform>& pose,
	                std::vector<glm::mat4>& outMatrices);
	bool FromMatrices(const std::vector<glm::mat4>& matrices,
	                  VansAnimationFrameVector<VansBoneTransform>& outPose);

	// Translation/scale use linear interpolation; rotation uses normalized
	// shortest-arc slerp. Invalid inputs use a deterministic nearest endpoint.
	glm::mat4 BlendTransforms(const glm::mat4& first,
	                          const glm::mat4& second,
	                          float alpha);

	// Applies an authored local-space additive transform relative to reference.
	// The identity transform is the canonical reference for current graph nodes.
	glm::mat4 ApplyAdditiveTransform(const glm::mat4& base,
	                                 const glm::mat4& additive,
	                                 float weight,
	                                 const glm::mat4& reference = glm::mat4(1.0f));

	void BlendPoses(const std::vector<glm::mat4>& first,
	                const std::vector<glm::mat4>& second,
	                float alpha,
	                std::vector<glm::mat4>& outPose);

	void ApplyAdditivePose(const std::vector<glm::mat4>& base,
	                       const std::vector<glm::mat4>& additive,
	                       float weight,
	                       std::vector<glm::mat4>& outPose);
}
