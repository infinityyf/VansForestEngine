#include "VansAnimationTypes.h"

using namespace VansGraphics;

void VansGraphics::VertexBoneData::AddBoneInfluence(int boneID, float weight)
{
	// Find an empty slot first
	for (uint32_t i = 0; i < MAX_BONE_INFLUENCE; i++)
	{
		if (boneIDs[i] < 0)
		{
			boneIDs[i] = boneID;
			weights[i] = weight;
			return;
		}
	}

	// All slots full — replace the smallest weight if new weight is larger
	int   minIndex  = 0;
	float minWeight = weights[0];
	for (uint32_t i = 1; i < MAX_BONE_INFLUENCE; i++)
	{
		if (weights[i] < minWeight)
		{
			minWeight = weights[i];
			minIndex  = i;
		}
	}

	if (weight > minWeight)
	{
		boneIDs[minIndex] = boneID;
		weights[minIndex] = weight;
	}
}

void VansGraphics::VertexBoneData::Normalize()
{
	float sum = 0.0f;
	for (uint32_t i = 0; i < MAX_BONE_INFLUENCE; i++)
	{
		if (boneIDs[i] >= 0)
			sum += weights[i];
	}

	if (sum > 0.0001f)
	{
		float invSum = 1.0f / sum;
		for (uint32_t i = 0; i < MAX_BONE_INFLUENCE; i++)
		{
			if (boneIDs[i] >= 0)
				weights[i] *= invSum;
		}
	}
}
