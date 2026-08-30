#include "VansAnimationTypes.h"

#include <algorithm>
#include <cmath>

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
	float maxWeight = 0.0f;
	for (uint32_t i = 0; i < MAX_BONE_INFLUENCE; i++)
	{
		if (boneIDs[i] >= 0 && std::isfinite(weights[i]) && weights[i] > 0.0f)
			maxWeight = std::max(maxWeight, weights[i]);
	}

	// Imported formats can contain very small, but still meaningful, positive
	// weights. Treating those vertices as unskinned leaves them in bind pose
	// while their neighbours animate, producing long stretched triangles.
	if (maxWeight > 0.0f)
	{
		float scaledSum = 0.0f;
		for (uint32_t i = 0; i < MAX_BONE_INFLUENCE; i++)
		{
			if (boneIDs[i] >= 0 && std::isfinite(weights[i]) && weights[i] > 0.0f)
				scaledSum += weights[i] / maxWeight;
		}
		if (!(scaledSum > 0.0f) || !std::isfinite(scaledSum))
			return;
		for (uint32_t i = 0; i < MAX_BONE_INFLUENCE; i++)
		{
			if (boneIDs[i] >= 0 && std::isfinite(weights[i]) && weights[i] > 0.0f)
				weights[i] = (weights[i] / maxWeight) / scaledSum;
		}
	}
}
