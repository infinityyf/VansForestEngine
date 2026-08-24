#include "VansAnimationRigStorage.h"

#include "../../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <unordered_set>

namespace VansGraphics
{
	using json = nlohmann::json;

	namespace
	{
		bool HasOnlyFields(const json& value,
		                   std::initializer_list<const char*> allowed,
		                   std::string& unknown)
		{
			if (!value.is_object())
				return false;
			std::unordered_set<std::string> fields;
			for (const char* field : allowed)
				fields.emplace(field);
			for (const auto& item : value.items())
			{
				if (fields.find(item.key()) == fields.end())
				{
					unknown = item.key();
					return false;
				}
			}
			return true;
		}

		bool ReadVec3(const json& value, glm::vec3& result)
		{
			if (!value.is_array() || value.size() != 3
				|| !value[0].is_number() || !value[1].is_number() || !value[2].is_number())
				return false;
			result = glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
			return std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z);
		}

		bool ReadVec2(const json& value, glm::vec2& result)
		{
			if (!value.is_array() || value.size() != 2
				|| !value[0].is_number() || !value[1].is_number())
				return false;
			result = glm::vec2(value[0].get<float>(), value[1].get<float>());
			return std::isfinite(result.x) && std::isfinite(result.y);
		}

		json WriteVec3(const glm::vec3& value)
		{
			return json::array({ value.x, value.y, value.z });
		}

		bool ReadQuat(const json& value, glm::quat& result)
		{
			if (!value.is_array() || value.size() != 4
				|| !value[0].is_number() || !value[1].is_number()
				|| !value[2].is_number() || !value[3].is_number())
				return false;
			result = glm::quat(value[3].get<float>(), value[0].get<float>(),
				value[1].get<float>(), value[2].get<float>());
			const float length = glm::length(result);
			if (!std::isfinite(length) || length <= 1.0e-6f)
				return false;
			result = glm::normalize(result);
			return true;
		}

		json WriteQuat(const glm::quat& value)
		{
			const glm::quat normalized = glm::normalize(value);
			return json::array({ normalized.x, normalized.y, normalized.z, normalized.w });
		}

		bool ReadSolver(const std::string& value, VansRigSolverKind& result)
		{
			if (value == "limb") { result = VansRigSolverKind::Limb; return true; }
			if (value == "ccd") { result = VansRigSolverKind::CCD; return true; }
			if (value == "fabrik") { result = VansRigSolverKind::FABRIK; return true; }
			if (value == "aim") { result = VansRigSolverKind::Aim; return true; }
			return false;
		}

		const char* WriteSolver(VansRigSolverKind value)
		{
			switch (value)
			{
			case VansRigSolverKind::Limb: return "limb";
			case VansRigSolverKind::CCD: return "ccd";
			case VansRigSolverKind::FABRIK: return "fabrik";
			case VansRigSolverKind::Aim: return "aim";
			}
			return nullptr;
		}

		bool ReadLimitKind(const std::string& value, VansJointLimitKind& result)
		{
			if (value == "hinge") { result = VansJointLimitKind::Hinge; return true; }
			if (value == "swingTwist") { result = VansJointLimitKind::SwingTwist; return true; }
			if (value == "locked") { result = VansJointLimitKind::Locked; return true; }
			return false;
		}

		const char* WriteLimitKind(VansJointLimitKind value)
		{
			switch (value)
			{
			case VansJointLimitKind::Hinge: return "hinge";
			case VansJointLimitKind::SwingTwist: return "swingTwist";
			case VansJointLimitKind::Locked: return "locked";
			}
			return nullptr;
		}

		bool RequireObjectFields(const json& value,
		                         std::initializer_list<const char*> allowed,
		                         std::initializer_list<const char*> required,
		                         const std::string& label,
		                         std::string& error)
		{
			std::string unknown;
			if (!value.is_object() || !HasOnlyFields(value, allowed, unknown))
			{
				error = unknown.empty() ? label + " must be an object"
					: "Unknown " + label + " field '" + unknown + "'";
				return false;
			}
			for (const char* field : required)
			{
				if (!value.contains(field))
				{
					error = label + " is missing required field '" + field + "'";
					return false;
				}
			}
			return true;
		}
	}

	bool VansAnimationRigStorage::DeserializeFromJsonObject(
		const json& root,
		VansAnimationRigAsset& asset,
		std::string& error)
	{
		asset = {};
		error.clear();
		try
		{
			if (!RequireObjectFields(root,
				{ "assetKind", "name", "skeletonGuid", "modelAxes", "semanticBones",
				  "sockets", "goals", "chains", "jointLimits", "contacts" },
				{ "assetKind", "name", "skeletonGuid", "modelAxes", "semanticBones",
				  "sockets", "goals", "chains", "jointLimits", "contacts" },
				"Animation Rig", error))
				return false;
			for (const char* forbidden : { "version", "schemaVersion", "formatVersion" })
			{
				if (root.contains(forbidden))
				{
					error = std::string("Forbidden generation field '") + forbidden + "'";
					return false;
				}
			}
			if (!root["assetKind"].is_string() || root["assetKind"].get<std::string>() != "animationRig"
				|| !root["name"].is_string() || !root["skeletonGuid"].is_string()
				|| !root["semanticBones"].is_object() || !root["sockets"].is_array()
				|| !root["goals"].is_array()
				|| !root["chains"].is_array() || !root["jointLimits"].is_array()
				|| !root["contacts"].is_array())
			{
				error = "Animation Rig canonical field types are invalid";
				return false;
			}
			asset.name = root["name"].get<std::string>();
			asset.skeletonGuid = root["skeletonGuid"].get<std::string>();
			const json& axes = root["modelAxes"];
			if (!RequireObjectFields(axes, { "forward", "up" }, { "forward", "up" },
				"modelAxes", error)
				|| !ReadVec3(axes["forward"], asset.modelForward)
				|| !ReadVec3(axes["up"], asset.modelUp))
			{
				if (error.empty()) error = "modelAxes values must be finite vec3 arrays";
				return false;
			}
			for (const auto& item : root["semanticBones"].items())
			{
				if (item.key().empty() || !item.value().is_string()
					|| item.value().get<std::string>().empty())
				{
					error = "semanticBones must map non-empty ids to non-empty bone names";
					return false;
				}
				asset.semanticBones.emplace(item.key(), item.value().get<std::string>());
			}
			for (const json& value : root["sockets"])
			{
				if (!RequireObjectFields(value,
					{ "guid", "name", "boneGuid", "positionLocal", "rotationLocal", "scaleLocal" },
					{ "guid", "name", "boneGuid", "positionLocal", "rotationLocal", "scaleLocal" },
					"socket", error)
					|| !value["guid"].is_string() || !value["name"].is_string()
					|| !value["boneGuid"].is_string())
					return false;
				VansRigSocketDefinition socket;
				socket.guid = value["guid"].get<std::string>();
				socket.name = value["name"].get<std::string>();
				socket.boneGuid = value["boneGuid"].get<std::string>();
				if (!ReadVec3(value["positionLocal"], socket.positionLocal)
					|| !ReadQuat(value["rotationLocal"], socket.rotationLocal)
					|| !ReadVec3(value["scaleLocal"], socket.scaleLocal))
				{
					error = "socket local transform must be finite position/quaternion/scale arrays";
					return false;
				}
				asset.sockets.push_back(std::move(socket));
			}
			for (const json& value : root["goals"])
			{
				if (!RequireObjectFields(value, { "id", "effectorBone" },
					{ "id", "effectorBone" }, "goal", error)
					|| !value["id"].is_string() || !value["effectorBone"].is_string())
					return false;
				asset.goals.push_back({ value["id"].get<std::string>(),
					value["effectorBone"].get<std::string>() });
			}
			for (const json& value : root["chains"])
			{
				if (!RequireObjectFields(value,
					{ "id", "solver", "bones", "goal", "poleAxisLocal", "softReachStartRatio",
					  "maxStretchScale", "weights", "solveWeights", "maxStepDegrees",
					  "forwardAxisLocal", "upAxisLocal" },
					{ "id", "solver", "bones", "goal" }, "chain", error)
					|| !value["id"].is_string() || !value["solver"].is_string()
					|| !value["bones"].is_array() || !value["goal"].is_string())
					return false;
				VansRigChainDefinition chain;
				chain.id = value["id"].get<std::string>();
				chain.goal = value["goal"].get<std::string>();
				if (!ReadSolver(value["solver"].get<std::string>(), chain.solver))
				{
					error = "Unknown chain solver";
					return false;
				}
				if (chain.solver == VansRigSolverKind::Limb)
				{
					if (!RequireObjectFields(value,
						{ "id", "solver", "bones", "goal", "poleAxisLocal",
						  "softReachStartRatio", "maxStretchScale" },
						{ "id", "solver", "bones", "goal", "poleAxisLocal",
						  "softReachStartRatio", "maxStretchScale" }, "limb chain", error))
						return false;
				}
				else if (chain.solver == VansRigSolverKind::Aim)
				{
					if (!RequireObjectFields(value,
						{ "id", "solver", "bones", "goal", "weights",
						  "forwardAxisLocal", "upAxisLocal" },
						{ "id", "solver", "bones", "goal", "weights",
						  "forwardAxisLocal", "upAxisLocal" }, "aim chain", error))
						return false;
				}
				else if (chain.solver == VansRigSolverKind::CCD)
				{
					if (!RequireObjectFields(value,
						{ "id", "solver", "bones", "goal", "solveWeights", "maxStepDegrees" },
						{ "id", "solver", "bones", "goal", "solveWeights", "maxStepDegrees" },
						"ccd chain", error)) return false;
				}
				else if (!RequireObjectFields(value,
					{ "id", "solver", "bones", "goal", "solveWeights" },
					{ "id", "solver", "bones", "goal", "solveWeights" },
					"fabrik chain", error)) return false;
				for (const json& bone : value["bones"])
				{
					if (!bone.is_string()) { error = "chain bones must be strings"; return false; }
					chain.bones.push_back(bone.get<std::string>());
				}
				if (chain.solver == VansRigSolverKind::Limb)
				{
					if (!ReadVec3(value["poleAxisLocal"], chain.poleAxisLocal)
						|| !value["softReachStartRatio"].is_number()
						|| !value["maxStretchScale"].is_number())
					{
						error = "Limb chain parameters must use canonical finite numeric types";
						return false;
					}
					chain.softReachStartRatio = value["softReachStartRatio"].get<float>();
					chain.maxStretchScale = value["maxStretchScale"].get<float>();
					if (!std::isfinite(chain.softReachStartRatio)
						|| !std::isfinite(chain.maxStretchScale))
					{
						error = "Limb chain parameters must be finite";
						return false;
					}
				}
				if (chain.solver == VansRigSolverKind::Aim)
				{
					if (!ReadVec3(value["forwardAxisLocal"], chain.forwardAxisLocal)
						|| !ReadVec3(value["upAxisLocal"], chain.upAxisLocal)
						|| !value["weights"].is_array())
					{
						error = "Aim chain axes and weights must use canonical types";
						return false;
					}
					for (const json& weight : value["weights"])
					{
						if (!weight.is_number()) { error = "chain weights must be numbers"; return false; }
						const float parsedWeight = weight.get<float>();
						if (!std::isfinite(parsedWeight))
						{
							error = "chain weights must be finite";
							return false;
						}
						chain.weights.push_back(parsedWeight);
					}
				}
				if (chain.solver == VansRigSolverKind::CCD
					|| chain.solver == VansRigSolverKind::FABRIK)
				{
					if (!value["solveWeights"].is_array())
					{
						error = "solveWeights must be an array";
						return false;
					}
					for (const json& weight : value["solveWeights"])
					{
						if (!weight.is_number() || !std::isfinite(weight.get<float>()))
						{
							error = "solveWeights must contain finite numbers";
							return false;
						}
						chain.solveWeights.push_back(weight.get<float>());
					}
					if (chain.solver == VansRigSolverKind::CCD)
					{
						if (!value["maxStepDegrees"].is_number()
							|| !std::isfinite(value["maxStepDegrees"].get<float>()))
						{
							error = "maxStepDegrees must be finite";
							return false;
						}
						chain.maxStepDegrees = value["maxStepDegrees"].get<float>();
					}
				}
				asset.chains.push_back(std::move(chain));
			}
			for (const json& value : root["jointLimits"])
			{
				if (!RequireObjectFields(value,
					{ "bone", "kind", "axisLocal", "swingReferenceAxisLocal",
					  "minDegrees", "maxDegrees", "swingLimitDegrees" },
					{ "bone", "kind" },
					"jointLimit", error)
					|| !value["bone"].is_string() || !value["kind"].is_string())
					return false;
				VansRigJointLimitDefinition limit;
				limit.bone = value["bone"].get<std::string>();
				if (!ReadLimitKind(value["kind"].get<std::string>(), limit.kind))
				{
					error = "jointLimit kind is invalid";
					return false;
				}
				if (limit.kind == VansJointLimitKind::Locked)
				{
					if (!RequireObjectFields(value, { "bone", "kind" }, { "bone", "kind" },
						"locked jointLimit", error)) return false;
				}
				else
				{
					const bool swingTwist = limit.kind == VansJointLimitKind::SwingTwist;
					if (!RequireObjectFields(value,
						swingTwist
							? std::initializer_list<const char*>{ "bone", "kind", "axisLocal",
								"swingReferenceAxisLocal", "minDegrees", "maxDegrees", "swingLimitDegrees" }
							: std::initializer_list<const char*>{ "bone", "kind", "axisLocal",
								"minDegrees", "maxDegrees" },
						swingTwist
							? std::initializer_list<const char*>{ "bone", "kind", "axisLocal",
								"swingReferenceAxisLocal", "minDegrees", "maxDegrees", "swingLimitDegrees" }
							: std::initializer_list<const char*>{ "bone", "kind", "axisLocal",
								"minDegrees", "maxDegrees" },
						swingTwist ? "swingTwist jointLimit" : "hinge jointLimit", error)
						|| !ReadVec3(value["axisLocal"], limit.axisLocal)
						|| !value["minDegrees"].is_number() || !value["maxDegrees"].is_number())
						return false;
					limit.minDegrees = value["minDegrees"].get<float>();
					limit.maxDegrees = value["maxDegrees"].get<float>();
					if (!std::isfinite(limit.minDegrees) || !std::isfinite(limit.maxDegrees))
					{
						error = "jointLimit twist angles must be finite";
						return false;
					}
					if (swingTwist
						&& (!ReadVec3(value["swingReferenceAxisLocal"], limit.swingReferenceAxisLocal)
							|| !ReadVec2(value["swingLimitDegrees"], limit.swingLimitDegrees)))
					{
						error = "Swing-Twist reference axis and limits must be finite vectors";
						return false;
					}
				}
				asset.jointLimits.push_back(limit);
			}
			for (const json& value : root["contacts"])
			{
				if (!RequireObjectFields(value,
					{ "id", "chain", "footBone", "ballBone", "soleForwardLocal", "soleNormalLocal",
					  "soleSamplesLocal", "pivotsLocal", "sweepRadius" },
					{ "id", "chain", "footBone", "ballBone", "soleForwardLocal", "soleNormalLocal",
					  "soleSamplesLocal", "pivotsLocal", "sweepRadius" },
					"contact", error)
					|| !value["id"].is_string() || !value["chain"].is_string()
					|| !value["footBone"].is_string() || !value["ballBone"].is_string()
					|| !value["soleSamplesLocal"].is_array() || !value["sweepRadius"].is_number())
					return false;
				VansRigContactDefinition contact;
				contact.id = value["id"].get<std::string>();
				contact.chain = value["chain"].get<std::string>();
				contact.footBone = value["footBone"].get<std::string>();
				contact.ballBone = value["ballBone"].get<std::string>();
				contact.sweepRadius = value["sweepRadius"].get<float>();
				if (!std::isfinite(contact.sweepRadius))
				{
					error = "contact sweepRadius must be finite";
					return false;
				}
				if (!ReadVec3(value["soleForwardLocal"], contact.soleForwardLocal))
					{ error = "contact soleForwardLocal must be a finite vec3"; return false; }
				if (!ReadVec3(value["soleNormalLocal"], contact.soleNormalLocal))
					{ error = "contact soleNormalLocal must be a finite vec3"; return false; }
				for (const json& sampleValue : value["soleSamplesLocal"])
				{
					if (!RequireObjectFields(sampleValue, { "id", "position" }, { "id", "position" },
						"sole sample", error) || !sampleValue["id"].is_string())
						return false;
					VansRigSoleSample sample;
					sample.id = sampleValue["id"].get<std::string>();
					if (!ReadVec3(sampleValue["position"], sample.positionLocal))
						{ error = "sole sample position must be a finite vec3"; return false; }
					contact.soleSamplesLocal.push_back(sample);
				}
				const json& pivots = value["pivotsLocal"];
				if (!RequireObjectFields(pivots, { "heel", "ball", "ankle" },
					{ "heel", "ball", "ankle" }, "pivotsLocal", error)
					|| !ReadVec3(pivots["heel"], contact.heelPivotLocal)
					|| !ReadVec3(pivots["ball"], contact.ballPivotLocal)
					|| !ReadVec3(pivots["ankle"], contact.anklePivotLocal))
				{
					if (error.empty()) error = "pivotsLocal values must be finite vec3 arrays";
					return false;
				}
				asset.contacts.push_back(std::move(contact));
			}
		}
		catch (const json::exception& exception)
		{
			error = exception.what();
			return false;
		}
		return true;
	}

	bool VansAnimationRigStorage::SerializeToJsonObject(
		const VansAnimationRigAsset& asset,
		json& root,
		std::string& error)
	{
		error.clear();
		root = {
			{ "assetKind", "animationRig" },
			{ "name", asset.name },
			{ "skeletonGuid", asset.skeletonGuid },
			{ "modelAxes", { { "forward", WriteVec3(asset.modelForward) },
				{ "up", WriteVec3(asset.modelUp) } } },
			{ "semanticBones", asset.semanticBones },
			{ "sockets", json::array() },
			{ "goals", json::array() },
			{ "chains", json::array() },
			{ "jointLimits", json::array() },
			{ "contacts", json::array() }
		};
		for (const VansRigSocketDefinition& socket : asset.sockets)
		{
			root["sockets"].push_back({
				{ "guid", socket.guid }, { "name", socket.name }, { "boneGuid", socket.boneGuid },
				{ "positionLocal", WriteVec3(socket.positionLocal) },
				{ "rotationLocal", WriteQuat(socket.rotationLocal) },
				{ "scaleLocal", WriteVec3(socket.scaleLocal) }
			});
		}
		for (const VansRigGoalDefinition& goal : asset.goals)
			root["goals"].push_back({ { "id", goal.id }, { "effectorBone", goal.effectorBone } });
		for (const VansRigChainDefinition& chain : asset.chains)
		{
			const char* solver = WriteSolver(chain.solver);
			if (!solver)
			{
				error = "Animation Rig contains an invalid chain solver enum";
				return false;
			}
			json value = { { "id", chain.id }, { "solver", solver },
				{ "bones", chain.bones }, { "goal", chain.goal } };
			if (chain.solver == VansRigSolverKind::Limb)
			{
				value["poleAxisLocal"] = WriteVec3(chain.poleAxisLocal);
				value["softReachStartRatio"] = chain.softReachStartRatio;
				value["maxStretchScale"] = chain.maxStretchScale;
			}
			if (chain.solver == VansRigSolverKind::Aim)
			{
				value["weights"] = chain.weights;
				value["forwardAxisLocal"] = WriteVec3(chain.forwardAxisLocal);
				value["upAxisLocal"] = WriteVec3(chain.upAxisLocal);
			}
			if (chain.solver == VansRigSolverKind::CCD
				|| chain.solver == VansRigSolverKind::FABRIK)
				value["solveWeights"] = chain.solveWeights;
			if (chain.solver == VansRigSolverKind::CCD)
				value["maxStepDegrees"] = chain.maxStepDegrees;
			root["chains"].push_back(std::move(value));
		}
		for (const VansRigJointLimitDefinition& limit : asset.jointLimits)
		{
			const char* kind = WriteLimitKind(limit.kind);
			if (!kind)
			{
				error = "Animation Rig contains an invalid joint limit enum";
				return false;
			}
			json value = { { "bone", limit.bone }, { "kind", kind } };
			if (limit.kind != VansJointLimitKind::Locked)
			{
				value["axisLocal"] = WriteVec3(limit.axisLocal);
				value["minDegrees"] = limit.minDegrees;
				value["maxDegrees"] = limit.maxDegrees;
			}
			if (limit.kind == VansJointLimitKind::SwingTwist)
			{
				value["swingReferenceAxisLocal"] = WriteVec3(limit.swingReferenceAxisLocal);
				value["swingLimitDegrees"] = {
					limit.swingLimitDegrees.x, limit.swingLimitDegrees.y };
			}
			root["jointLimits"].push_back(std::move(value));
		}
		for (const VansRigContactDefinition& contact : asset.contacts)
		{
			json samples = json::array();
			for (const VansRigSoleSample& sample : contact.soleSamplesLocal)
				samples.push_back({ { "id", sample.id }, { "position", WriteVec3(sample.positionLocal) } });
			root["contacts"].push_back({ { "id", contact.id }, { "chain", contact.chain },
				{ "footBone", contact.footBone }, { "ballBone", contact.ballBone },
				{ "soleForwardLocal", WriteVec3(contact.soleForwardLocal) },
				{ "soleNormalLocal", WriteVec3(contact.soleNormalLocal) },
				{ "soleSamplesLocal", std::move(samples) },
				{ "pivotsLocal", { { "heel", WriteVec3(contact.heelPivotLocal) },
					{ "ball", WriteVec3(contact.ballPivotLocal) },
					{ "ankle", WriteVec3(contact.anklePivotLocal) } } },
				{ "sweepRadius", contact.sweepRadius } });
		}
		VansAnimationRigAsset verified;
		return DeserializeFromJsonObject(root, verified, error);
	}

	bool VansAnimationRigStorage::Load(const std::filesystem::path& path,
	                                  VansAnimationRigAsset& asset,
	                                  std::string& error)
	{
		json root;
		return Vans::VansJsonFileStorage::Read(path, root, error)
			&& DeserializeFromJsonObject(root, asset, error);
	}

	bool VansAnimationRigStorage::SaveAtomic(const std::filesystem::path& path,
	                                        const VansAnimationRigAsset& asset,
	                                        std::string& error)
	{
		json root;
		return SerializeToJsonObject(asset, root, error)
			&& Vans::VansJsonFileStorage::WriteAtomic(path, root, error);
	}
}
