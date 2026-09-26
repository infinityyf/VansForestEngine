#pragma once

#include "VansPoseTypes.h"

#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>

#include <vector>
#include <string>

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

	// These are the only local/model hierarchy propagation entry points. They
	// reject missing or invalid topology instead of falling back to bone-index
	// order, which is incorrect when a parent follows its child in storage.
	bool BuildModelTransforms(const std::vector<glm::mat4>& localTransforms,
	                          const Skeleton& skeleton,
	                          std::vector<glm::mat4>& outModelTransforms,
	                          std::string* error = nullptr);
	bool BuildModelTransforms(const VansAnimationFrameVector<glm::mat4>& localTransforms,
	                          const Skeleton& skeleton,
	                          VansAnimationFrameVector<glm::mat4>& outModelTransforms,
	                          std::string* error = nullptr);
	bool BuildModelTransforms(const VansAnimationFrameVector<VansBoneTransform>& localPose,
	                          const Skeleton& skeleton,
	                          VansAnimationFrameVector<glm::mat4>& outModelTransforms,
	                          std::string* error = nullptr);
	bool BuildModelTransforms(const std::vector<VansBoneTransform>& localPose,
	                          const Skeleton& skeleton,
	                          std::vector<glm::mat4>& outModelTransforms,
	                          std::string* error = nullptr);
	bool BuildLocalTransforms(const std::vector<glm::mat4>& modelTransforms,
	                          const Skeleton& skeleton,
	                          std::vector<glm::mat4>& outLocalTransforms,
	                          std::string* error = nullptr);
	bool BuildLocalTransforms(const VansAnimationFrameVector<glm::mat4>& modelTransforms,
	                          const Skeleton& skeleton,
	                          VansAnimationFrameVector<glm::mat4>& outLocalTransforms,
	                          std::string* error = nullptr);

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

	// Applies an additive transform authored in mesh/model space.  The
	// reference and additive poses are converted to a model-space delta first;
	// the weighted delta is then composed onto the base model transform.  This
	// is the transform-level equivalent of UE ALS Apply Mesh Space Additive.
	glm::mat4 ApplyMeshSpaceAdditiveTransform(const glm::mat4& baseModel,
	                                           const glm::mat4& additiveModel,
	                                           const glm::mat4& referenceModel,
	                                           float weight);

}
