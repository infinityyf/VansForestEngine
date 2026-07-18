#include "../AssetCore/VansClothProfile.h"
#include "VansClothNode.h"
#include "../AnimationCore/VansAnimationTypes.h"
#include "../Util/VansLog.h"

namespace VansEngine
{
    std::vector<ClothNodePinSkinData> VansClothProfile::ResolveBoneBindings(
        const VansGraphics::Skeleton& skeleton) const
    {
        std::vector<ClothNodePinSkinData> result;
        result.reserve(m_PinnedLocalPositions.size());

        for (size_t i = 0; i < m_PinnedLocalPositions.size(); ++i)
        {
            ClothNodePinSkinData skinData;
            skinData.m_BoneCount = 0;

            if (i < m_PinnedBoneBindings.size())
            {
                const auto& binding = m_PinnedBoneBindings[i];
                for (size_t b = 0; b < binding.m_BoneNames.size()
                     && skinData.m_BoneCount < MAX_CLOTH_PIN_BONE_INFLUENCE; ++b)
                {
                    auto it = skeleton.boneNameToIndex.find(binding.m_BoneNames[b]);
                    if (it != skeleton.boneNameToIndex.end())
                    {
                        float w = (b < binding.m_Weights.size()) ? binding.m_Weights[b] : 0.0f;
                        skinData.m_BoneWeights[skinData.m_BoneCount].m_BoneIndex = it->second;
                        skinData.m_BoneWeights[skinData.m_BoneCount].m_Weight = w;
                        skinData.m_BoneCount++;
                    }
                    else
                    {
                        VANS_LOG_WARN("[VansClothProfile] ResolveBoneBindings: bone '"
                                      << binding.m_BoneNames[b] << "' was not found in Skeleton, skipping.");
                    }
                }
            }

            result.push_back(skinData);
        }

        return result;
    }
}
