#include "VansClothProfileJsonCodec.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <exception>
#include <initializer_list>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace VansEngine
{
	namespace
	{
		bool HasOnlyFields(
			const ClothProfileJson& value,
			std::initializer_list<std::string_view> fields)
		{
			if (!value.is_object() || value.size() != fields.size())
				return false;
			for (std::string_view field : fields)
				if (!value.contains(std::string(field)))
					return false;
			return true;
		}

		bool IsFiniteNumber(const ClothProfileJson& value)
		{
			return value.is_number() && std::isfinite(value.get<float>());
		}

		bool ReadVec3(
			const ClothProfileJson& value,
			glm::vec3& result,
			const char* field,
			std::string& error)
		{
			if (!value.is_array() || value.size() != 3u ||
				!IsFiniteNumber(value[0]) || !IsFiniteNumber(value[1]) ||
				!IsFiniteNumber(value[2]))
			{
				error = std::string("Cloth profile '") + field +
					"' must be a finite vec3";
				return false;
			}
			result = glm::vec3(
				value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
			return true;
		}
	}

	ClothProfileJson VansClothProfileJsonCodec::Encode(const VansClothProfile& profile)
	{
		ClothProfileJson root;
		root["name"] = profile.m_Name;
		root["description"] = profile.m_Description;
		root["modelPath"] = profile.m_ModelPath;
		root["simulation"] = {
			{ "stiffness", profile.m_Stiffness },
			{ "stiffnessFrequency", profile.m_StiffnessFrequency },
			{ "damping", profile.m_Damping },
			{ "friction", profile.m_Friction },
			{ "gravity", profile.m_Gravity },
			{ "selfCollision", profile.m_SelfCollision },
			{ "weldTolerance", profile.m_WeldTolerance }
		};
		root["pinnedMatchTolerance"] = profile.m_PinnedMatchTolerance;
		root["followBones"] = profile.m_FollowBones;
		root["referenceSkeletonPath"] = profile.m_ReferenceSkeletonPath;
		root["skeletonOffset"] = {
			{ "position", {
				profile.m_SkeletonOffset.m_Position.x,
				profile.m_SkeletonOffset.m_Position.y,
				profile.m_SkeletonOffset.m_Position.z } },
			{ "rotation", {
				profile.m_SkeletonOffset.m_Rotation.x,
				profile.m_SkeletonOffset.m_Rotation.y,
				profile.m_SkeletonOffset.m_Rotation.z } },
			{ "scale", {
				profile.m_SkeletonOffset.m_Scale.x,
				profile.m_SkeletonOffset.m_Scale.y,
				profile.m_SkeletonOffset.m_Scale.z } }
		};

		ClothProfileJson pinnedVertices = ClothProfileJson::array();
		for (std::size_t pinIndex = 0;
			pinIndex < profile.m_PinnedLocalPositions.size(); ++pinIndex)
		{
			const glm::vec3& position = profile.m_PinnedLocalPositions[pinIndex];
			ClothProfileJson entry = {
				{ "localPosition", { position.x, position.y, position.z } }
			};
			if (pinIndex < profile.m_PinnedBoneBindings.size())
			{
				const auto& binding = profile.m_PinnedBoneBindings[pinIndex];
				if (!binding.m_BoneNames.empty())
				{
					ClothProfileJson influences = ClothProfileJson::array();
					for (std::size_t influenceIndex = 0;
						influenceIndex < binding.m_BoneNames.size(); ++influenceIndex)
					{
						influences.push_back({
							{ "boneName", binding.m_BoneNames[influenceIndex] },
							{ "weight", binding.m_Weights[influenceIndex] }
						});
					}
					entry["boneBindings"] = std::move(influences);
				}
			}
			pinnedVertices.push_back(std::move(entry));
		}
		root["pinnedVertices"] = std::move(pinnedVertices);
		return root;
	}

	bool VansClothProfileJsonCodec::Decode(
		const ClothProfileJson& root,
		const std::filesystem::path& filePath,
		VansClothProfile& profile,
		std::string& error)
	{
		error.clear();
		try
		{
			if (!HasOnlyFields(root, {
				"name", "description", "modelPath", "simulation",
				"pinnedMatchTolerance", "followBones", "referenceSkeletonPath",
				"skeletonOffset", "pinnedVertices" }))
			{
				error = "Cloth profile must use the current schema without unknown fields";
				return false;
			}
			if (!root["name"].is_string() || !root["description"].is_string() ||
				!root["modelPath"].is_string() || !root["followBones"].is_boolean() ||
				!root["referenceSkeletonPath"].is_string() ||
				!root["pinnedVertices"].is_array())
			{
				error = "Cloth profile metadata and pinnedVertices have invalid types";
				return false;
			}

			const ClothProfileJson& simulation = root["simulation"];
			if (!HasOnlyFields(simulation, {
				"stiffness", "stiffnessFrequency", "damping", "friction",
				"gravity", "selfCollision", "weldTolerance" }) ||
				!IsFiniteNumber(simulation["stiffness"]) ||
				!IsFiniteNumber(simulation["stiffnessFrequency"]) ||
				!IsFiniteNumber(simulation["damping"]) ||
				!IsFiniteNumber(simulation["friction"]) ||
				!IsFiniteNumber(simulation["gravity"]) ||
				!simulation["selfCollision"].is_boolean() ||
				!IsFiniteNumber(simulation["weldTolerance"]) ||
				!IsFiniteNumber(root["pinnedMatchTolerance"]))
			{
				error = "Cloth simulation settings have invalid fields or types";
				return false;
			}

			VansClothProfile decoded;
			decoded.m_Name = root["name"].get<std::string>();
			decoded.m_Description = root["description"].get<std::string>();
			decoded.m_ModelPath = root["modelPath"].get<std::string>();
			decoded.m_Stiffness = simulation["stiffness"].get<float>();
			decoded.m_StiffnessFrequency = simulation["stiffnessFrequency"].get<float>();
			decoded.m_Damping = simulation["damping"].get<float>();
			decoded.m_Friction = simulation["friction"].get<float>();
			decoded.m_Gravity = simulation["gravity"].get<float>();
			decoded.m_SelfCollision = simulation["selfCollision"].get<bool>();
			decoded.m_WeldTolerance = simulation["weldTolerance"].get<float>();
			decoded.m_PinnedMatchTolerance = root["pinnedMatchTolerance"].get<float>();
			decoded.m_FollowBones = root["followBones"].get<bool>();
			decoded.m_ReferenceSkeletonPath = root["referenceSkeletonPath"].get<std::string>();
			if (decoded.m_Stiffness < 0.0f || decoded.m_Stiffness > 1.0f ||
				decoded.m_StiffnessFrequency <= 0.0f || decoded.m_Damping < 0.0f ||
				decoded.m_Damping > 1.0f || decoded.m_Friction < 0.0f ||
				decoded.m_WeldTolerance <= 0.0f || decoded.m_PinnedMatchTolerance <= 0.0f)
			{
				error = "Cloth simulation settings are outside their valid ranges";
				return false;
			}

			const ClothProfileJson& offset = root["skeletonOffset"];
			if (!HasOnlyFields(offset, { "position", "rotation", "scale" }) ||
				!ReadVec3(offset["position"], decoded.m_SkeletonOffset.m_Position,
					"skeletonOffset.position", error) ||
				!ReadVec3(offset["rotation"], decoded.m_SkeletonOffset.m_Rotation,
					"skeletonOffset.rotation", error) ||
				!ReadVec3(offset["scale"], decoded.m_SkeletonOffset.m_Scale,
					"skeletonOffset.scale", error))
			{
				if (error.empty())
					error = "Cloth skeletonOffset must contain position, rotation and scale";
				return false;
			}
			if (std::abs(decoded.m_SkeletonOffset.m_Scale.x) <= 1.0e-6f ||
				std::abs(decoded.m_SkeletonOffset.m_Scale.y) <= 1.0e-6f ||
				std::abs(decoded.m_SkeletonOffset.m_Scale.z) <= 1.0e-6f)
			{
				error = "Cloth skeletonOffset scale cannot contain zero";
				return false;
			}

			for (std::size_t pinIndex = 0; pinIndex < root["pinnedVertices"].size(); ++pinIndex)
			{
				const ClothProfileJson& entry = root["pinnedVertices"][pinIndex];
				if (!entry.is_object() || !entry.contains("localPosition") ||
					(entry.size() != 1u && entry.size() != 2u) ||
					(entry.size() == 2u && !entry.contains("boneBindings")))
				{
					error = "Cloth pinned vertex " + std::to_string(pinIndex) +
						" has invalid fields";
					return false;
				}
				glm::vec3 position;
				if (!ReadVec3(entry["localPosition"], position, "localPosition", error))
					return false;
				decoded.m_PinnedLocalPositions.push_back(position);

				VansClothProfile::PinBoneBinding binding;
				if (entry.contains("boneBindings"))
				{
					const ClothProfileJson& influences = entry["boneBindings"];
					if (!influences.is_array() || influences.empty() || influences.size() > 4u)
					{
						error = "Cloth pin " + std::to_string(pinIndex) +
							" must contain between one and four bone bindings";
						return false;
					}
					std::unordered_set<std::string> uniqueNames;
					float weightSum = 0.0f;
					for (const ClothProfileJson& influence : influences)
					{
						if (!HasOnlyFields(influence, { "boneName", "weight" }) ||
							!influence["boneName"].is_string() ||
							influence["boneName"].get<std::string>().empty() ||
							!IsFiniteNumber(influence["weight"]))
						{
							error = "Cloth pin " + std::to_string(pinIndex) +
								" contains an invalid bone binding";
							return false;
						}
						std::string boneName = influence["boneName"].get<std::string>();
						const float weight = influence["weight"].get<float>();
						if (!uniqueNames.insert(boneName).second || weight <= 0.0f)
						{
							error = "Cloth pin " + std::to_string(pinIndex) +
								" has a duplicate bone or non-positive weight";
							return false;
						}
						binding.m_BoneNames.push_back(std::move(boneName));
						binding.m_Weights.push_back(weight);
						weightSum += weight;
					}
					if (std::abs(weightSum - 1.0f) > 1.0e-4f)
					{
						error = "Cloth pin " + std::to_string(pinIndex) +
							" bone weights must sum to 1";
						return false;
					}
				}
				if (decoded.m_FollowBones && binding.m_BoneNames.empty())
				{
					error = "Cloth followBones requires a bone binding for every pinned vertex";
					return false;
				}
				decoded.m_PinnedBoneBindings.push_back(std::move(binding));
			}

			profile = std::move(decoded);
			return true;
		}
		catch (const std::exception& exception)
		{
			error = "Invalid cloth profile JSON " + filePath.string() + ": " + exception.what();
			return false;
		}
	}
}
