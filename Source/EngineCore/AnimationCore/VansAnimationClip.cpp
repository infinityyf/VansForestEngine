#include "VansAnimationClip.h"
#include "../AssetCore/Storage/VansFileStorage.h"
#include "../Util/VansLog.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <type_traits>

using json = nlohmann::json;
using namespace VansGraphics;

// ────────────────────────────────────────────────────────────────
//  .vclip binary header layout (22 bytes total before JSON)
//
//  [0..5]   char[6]   magic  "VCLIP\0"
//  [6..9]   uint32    version
//  [10..13] uint32    headerSize  (JSON byte count)
//  [14..21] uint64    payloadSize (binary keyframe byte count)
// ────────────────────────────────────────────────────────────────

static constexpr size_t MAGIC_SIZE          = 6;    // includes null terminator
static constexpr size_t BINARY_HEADER_SIZE  = MAGIC_SIZE + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint64_t);
static constexpr size_t BYTES_PER_KEYFRAME  = sizeof(float) + sizeof(glm::vec3) + sizeof(glm::quat) + sizeof(glm::vec3);  // 44

// ─── Helper: write one keyframe to binary stream ───

static void WriteKeyframe(std::ostream& out, const BoneKeyframe& kf)
{
	out.write(reinterpret_cast<const char*>(&kf.time),     sizeof(float));
	out.write(reinterpret_cast<const char*>(&kf.position), sizeof(glm::vec3));
	out.write(reinterpret_cast<const char*>(&kf.rotation), sizeof(glm::quat));
	out.write(reinterpret_cast<const char*>(&kf.scale),    sizeof(glm::vec3));
}

static void WriteKeyframe(std::ostream& out, const TransformKeyframe& kf)
{
	out.write(reinterpret_cast<const char*>(&kf.time),     sizeof(float));
	out.write(reinterpret_cast<const char*>(&kf.position), sizeof(glm::vec3));
	out.write(reinterpret_cast<const char*>(&kf.rotation), sizeof(glm::quat));
	out.write(reinterpret_cast<const char*>(&kf.scale),    sizeof(glm::vec3));
}

// ─── Helper: read one keyframe from binary stream ───

class BinarySpanReader
{
public:
	BinarySpanReader() = default;
	BinarySpanReader(const char* data, size_t size)
		: m_Data(data), m_Size(size)
	{}

	bool ReadBytes(void* destination, size_t size)
	{
		if (destination == nullptr || size > Remaining())
			return false;
		std::memcpy(destination, m_Data + m_Offset, size);
		m_Offset += size;
		return true;
	}

	bool ReadString(size_t size, std::string& out)
	{
		if (size > Remaining())
			return false;
		out.assign(m_Data + m_Offset, size);
		m_Offset += size;
		return true;
	}

	size_t Remaining() const
	{
		return m_Size - m_Offset;
	}

private:
	const char* m_Data = nullptr;
	size_t m_Size = 0;
	size_t m_Offset = 0;
};

static bool ReadKeyframe(BinarySpanReader& reader, BoneKeyframe& kf)
{
	return reader.ReadBytes(&kf.time, sizeof(float)) &&
		reader.ReadBytes(&kf.position, sizeof(glm::vec3)) &&
		reader.ReadBytes(&kf.rotation, sizeof(glm::quat)) &&
		reader.ReadBytes(&kf.scale, sizeof(glm::vec3));
}

static bool ReadKeyframe(BinarySpanReader& reader, TransformKeyframe& kf)
{
	return reader.ReadBytes(&kf.time, sizeof(float)) &&
		reader.ReadBytes(&kf.position, sizeof(glm::vec3)) &&
		reader.ReadBytes(&kf.rotation, sizeof(glm::quat)) &&
		reader.ReadBytes(&kf.scale, sizeof(glm::vec3));
}

static std::optional<bool> ReadNodeTransformChannelsSetting(const std::string& filePath)
{
	std::ifstream meta(filePath + ".meta", std::ios::binary);
	if (!meta)
		return std::nullopt;

	json metaJson;
	try
	{
		meta >> metaJson;
	}
	catch (const json::parse_error&)
	{
		return std::nullopt;
	}

	if (!metaJson.contains("settings") || !metaJson["settings"].is_object())
		return std::nullopt;

	const json& settings = metaJson["settings"];
	if (settings.contains("nodeTransformChannels") && settings["nodeTransformChannels"].is_boolean())
		return settings["nodeTransformChannels"].get<bool>();
	return std::nullopt;
}

static bool ReadClipEnvelope(
	const std::string& filePath,
	std::string& bytes,
	uint32_t& version,
	std::string& headerStr,
	BinarySpanReader& payloadReader,
	bool logFailures)
{
	std::string error;
	if (!Vans::VansFileStorage::ReadAllBytes(filePath, bytes, error))
	{
		if (logFailures)
			VANS_LOG_ERROR("[VansAnimationClipIO] Failed to open for reading: " << filePath);
		return false;
	}

	BinarySpanReader reader(bytes.data(), bytes.size());
	char magic[MAGIC_SIZE];
	if (!reader.ReadBytes(magic, MAGIC_SIZE) || std::memcmp(magic, VCLIP_MAGIC, MAGIC_SIZE) != 0)
	{
		if (logFailures)
			VANS_LOG_ERROR("[VansAnimationClipIO] Invalid magic in: " << filePath);
		return false;
	}

	if (!reader.ReadBytes(&version, sizeof(uint32_t)))
	{
		if (logFailures)
			VANS_LOG_ERROR("[VansAnimationClipIO] Truncated version in: " << filePath);
		return false;
	}

	uint32_t headerSize = 0;
	uint64_t payloadSize = 0;
	if (!reader.ReadBytes(&headerSize, sizeof(uint32_t)) ||
		!reader.ReadBytes(&payloadSize, sizeof(uint64_t)) ||
		!reader.ReadString(headerSize, headerStr))
	{
		if (logFailures)
			VANS_LOG_ERROR("[VansAnimationClipIO] Truncated header in: " << filePath);
		return false;
	}

	if (payloadSize > reader.Remaining() || payloadSize > static_cast<uint64_t>((std::numeric_limits<size_t>::max)()))
	{
		if (logFailures)
			VANS_LOG_ERROR("[VansAnimationClipIO] Truncated payload in: " << filePath);
		return false;
	}

	payloadReader = BinarySpanReader(bytes.data() + (bytes.size() - reader.Remaining()), static_cast<size_t>(payloadSize));
	return true;
}

// ─── Helper: mat4 → json array of 16 floats (column-major) ───

static json Mat4ToJson(const glm::mat4& m)
{
	json arr = json::array();
	const float* p = &m[0][0];
	for (int i = 0; i < 16; i++)
		arr.push_back(p[i]);
	return arr;
}

// ─── Helper: json array of 16 floats → mat4 ───

static glm::mat4 JsonToMat4(const json& arr)
{
	glm::mat4 m(1.0f);
	float* p = &m[0][0];
	for (int i = 0; i < 16 && i < (int)arr.size(); i++)
		p[i] = arr[i].get<float>();
	return m;
}

static json EventValueToJson(const AnimationEventValue& value)
{
	json result = json::object();
	std::visit([&result](const auto& typedValue)
	{
		using T = std::decay_t<decltype(typedValue)>;
		if constexpr (std::is_same_v<T, std::monostate>)
			result["type"] = "none";
		else if constexpr (std::is_same_v<T, bool>)
		{
			result["type"] = "bool";
			result["value"] = typedValue;
		}
		else if constexpr (std::is_same_v<T, std::int64_t>)
		{
			result["type"] = "int";
			result["value"] = typedValue;
		}
		else if constexpr (std::is_same_v<T, double>)
		{
			result["type"] = "number";
			result["value"] = typedValue;
		}
		else if constexpr (std::is_same_v<T, std::string>)
		{
			result["type"] = "string";
			result["value"] = typedValue;
		}
		else if constexpr (std::is_same_v<T, glm::vec3>)
		{
			result["type"] = "vec3";
			result["value"] = { typedValue.x, typedValue.y, typedValue.z };
		}
	}, value);
	return result;
}

static bool EventValueFromJson(const json& source, AnimationEventValue& outValue)
{
	if (!source.is_object() || !source.contains("type") || !source["type"].is_string())
		return false;
	const std::string type = source["type"].get<std::string>();
	if (type == "none")
		outValue = std::monostate{};
	else if (type == "bool" && source.contains("value") && source["value"].is_boolean())
		outValue = source["value"].get<bool>();
	else if (type == "int" && source.contains("value") && source["value"].is_number_integer())
		outValue = source["value"].get<std::int64_t>();
	else if (type == "number" && source.contains("value") && source["value"].is_number())
		outValue = source["value"].get<double>();
	else if (type == "string" && source.contains("value") && source["value"].is_string())
		outValue = source["value"].get<std::string>();
	else if (type == "vec3" && source.contains("value") && source["value"].is_array()
	         && source["value"].size() == 3)
	{
		outValue = glm::vec3(source["value"][0].get<float>(),
		                     source["value"][1].get<float>(),
		                     source["value"][2].get<float>());
	}
	else
		return false;
	return true;
}

// ════════════════════════════════════════════════════════════════
//  Save
// ════════════════════════════════════════════════════════════════

bool VansGraphics::VansAnimationClipIO::Save(const std::string& filePath,
                                              const VansAnimationClip& clip,
                                              const Skeleton& skeleton)
{
	// ── Build JSON header ──
	json header;
	header["clipName"]       = clip.clipName;
	header["clipId"]         = clip.stableId != 0
		? clip.stableId
		: VansAnimationStableId(clip.clipName);
	header["duration"]       = clip.duration;
	header["ticksPerSecond"] = clip.ticksPerSecond;
	header["boneCount"]      = (uint32_t)skeleton.bones.size();
	header["sourceSkeletonGuid"] = skeleton.sourceSkeletonGuid;
	header["skeletonSignature"] = skeleton.ComputeSignature();

	// Serialize globalInverseTransform
	header["globalInverseTransform"] = Mat4ToJson(skeleton.globalInverseTransform);

	json bonesJson = json::array();
	uint64_t totalKeyframes = 0;

	for (size_t b = 0; b < skeleton.bones.size(); b++)
	{
		const BoneInfo& bone = skeleton.bones[b];
		json boneJson;
		if (bone.id != static_cast<int>(b) || bone.guid.empty() || bone.canonicalPath.empty())
		{
			VANS_LOG_ERROR("[VansAnimationClipIO] Skeleton identity is incomplete for bone index "
				<< b << " in: " << filePath);
			return false;
		}
		boneJson["name"]         = bone.name;
		boneJson["guid"]         = bone.guid;
		boneJson["canonicalPath"] = bone.canonicalPath;
		boneJson["id"]           = bone.id;
		boneJson["parentIndex"]  = bone.parentIndex;
		boneJson["offsetMatrix"] = Mat4ToJson(bone.offsetMatrix);
		boneJson["localTransform"] = Mat4ToJson(bone.localTransform);
		boneJson["children"]     = bone.children;

		uint32_t kfCount = (b < clip.boneKeyframes.size()) ? (uint32_t)clip.boneKeyframes[b].size() : 0;
		boneJson["keyframeCount"] = kfCount;
		totalKeyframes += kfCount;

		bonesJson.push_back(boneJson);
	}
	header["bones"] = bonesJson;

	json nodeChannelsJson = json::array();
	uint64_t totalNodeTransformKeyframes = 0;
	for (const NodeTransformChannel& channel : clip.nodeTransformChannels)
	{
		json channelJson;
		channelJson["nodeName"] = channel.nodeName;
		channelJson["nodePath"] = channel.nodePath;
		channelJson["parentChannelIndex"] = channel.parentChannelIndex;
		channelJson["bindLocalTransform"] = Mat4ToJson(channel.bindLocalTransform);
		channelJson["bindModelTransform"] = Mat4ToJson(channel.bindModelTransform);
		channelJson["keyframeCount"] = static_cast<uint32_t>(channel.keyframes.size());
		totalNodeTransformKeyframes += channel.keyframes.size();
		nodeChannelsJson.push_back(channelJson);
	}
	header["nodeTransformChannels"] = nodeChannelsJson;

	json curvesJson = json::array();
	for (const AnimationCurveTrack& curve : clip.curves)
	{
		json curveJson;
		curveJson["id"] = curve.id != 0 ? curve.id : VansAnimationStableId(curve.name);
		curveJson["name"] = curve.name;
		curveJson["keys"] = json::array();
		for (const AnimationCurveKey& key : curve.keys)
			curveJson["keys"].push_back({ { "time", key.time }, { "value", key.value } });
		curvesJson.push_back(std::move(curveJson));
	}
	header["curves"] = std::move(curvesJson);

	json eventsJson = json::array();
	for (const AnimationClipEvent& event : clip.events)
	{
		eventsJson.push_back({
			{ "id", event.id != 0 ? event.id : VansAnimationStableId(event.name) },
			{ "time", event.time },
			{ "name", event.name },
			{ "payload", EventValueToJson(event.payload) }
		});
	}
	header["events"] = std::move(eventsJson);

	json markersJson = json::array();
	for (const AnimationSyncMarker& marker : clip.syncMarkers)
	{
		markersJson.push_back({
			{ "id", marker.id != 0 ? marker.id : VansAnimationStableId(marker.name) },
			{ "time", marker.time },
			{ "name", marker.name }
		});
	}
	header["sync"] = {
		{ "group", clip.syncGroupName },
		{ "markers", std::move(markersJson) }
	};
	header["rootMotion"] = {
		{ "enabled", clip.rootMotion.enabled },
		{ "bone", clip.rootMotion.boneName },
		{ "translation", clip.rootMotion.extractTranslation },
		{ "rotation", clip.rootMotion.extractRotation },
		{ "scale", clip.rootMotion.extractScale }
	};

	std::string headerStr = header.dump();
	uint32_t headerSize   = (uint32_t)headerStr.size();
	uint64_t payloadSize  = (totalKeyframes + totalNodeTransformKeyframes) * BYTES_PER_KEYFRAME;

	// ── Write file ──
	std::ostringstream file(std::ios::binary);

	// Binary header
	file.write(VCLIP_MAGIC, MAGIC_SIZE);

	uint32_t version = VCLIP_VERSION;
	file.write(reinterpret_cast<const char*>(&version),     sizeof(uint32_t));
	file.write(reinterpret_cast<const char*>(&headerSize),  sizeof(uint32_t));
	file.write(reinterpret_cast<const char*>(&payloadSize), sizeof(uint64_t));

	// JSON header
	file.write(headerStr.c_str(), headerSize);

	// Binary payload: keyframes for each bone in order
	for (size_t b = 0; b < skeleton.bones.size(); b++)
	{
		if (b < clip.boneKeyframes.size())
		{
			for (const auto& kf : clip.boneKeyframes[b])
				WriteKeyframe(file, kf);
		}
	}
	for (const NodeTransformChannel& channel : clip.nodeTransformChannels)
	{
		for (const TransformKeyframe& kf : channel.keyframes)
			WriteKeyframe(file, kf);
	}

	if (!file)
	{
		VANS_LOG_ERROR("[VansAnimationClipIO] Failed to serialize clip bytes: " << filePath);
		return false;
	}

	std::string error;
	if (!Vans::VansFileStorage::WriteAtomicBytes(filePath, file.str(), error))
	{
		VANS_LOG_ERROR("[VansAnimationClipIO] Failed to save: " << filePath << " (" << error << ")");
		return false;
	}

	VANS_LOG("[VansAnimationClipIO] Saved: " << filePath
	         << " (" << skeleton.bones.size() << " bones, " << totalKeyframes
	         << " bone keyframes, " << clip.nodeTransformChannels.size()
	         << " node transform channels)");
	return true;
}

// ════════════════════════════════════════════════════════════════
//  Load
// ════════════════════════════════════════════════════════════════

bool VansGraphics::VansAnimationClipIO::Load(const std::string& filePath,
                                              VansAnimationClip& outClip,
                                              Skeleton& outSkeleton)
{
	std::string bytes;
	std::string headerStr;
	uint32_t version = 0;
	BinarySpanReader payloadReader;
	if (!ReadClipEnvelope(filePath, bytes, version, headerStr, payloadReader, true))
		return false;

	// ── Read binary header ──
	if (version != VCLIP_VERSION)
	{
		VANS_LOG_ERROR("[VansAnimationClipIO] Unsupported clip format version " << version
		             << " in: " << filePath << " (expected " << VCLIP_VERSION << ")");
		return false;
	}

	// ── Read JSON header ──
	json header;
	try
	{
		header = json::parse(headerStr);
	}
	catch (const json::parse_error& e)
	{
		VANS_LOG_ERROR("[VansAnimationClipIO] JSON parse error in " << filePath << ": " << e.what());
		return false;
	}

	if (!header.is_object() || !header.contains("sourceSkeletonGuid")
		|| !header["sourceSkeletonGuid"].is_string()
		|| !header.contains("skeletonSignature")
		|| !header["skeletonSignature"].is_number_unsigned()
		|| !header.contains("globalInverseTransform")
		|| !header.contains("bones") || !header["bones"].is_array())
	{
		VANS_LOG_ERROR("[VansAnimationClipIO] Missing canonical skeleton identity in: " << filePath);
		return false;
	}

	outClip.clipName       = header.value("clipName", "");
	outClip.stableId       = header.value("clipId", VansAnimationStableId(outClip.clipName));
	outClip.duration       = header.value("duration", 0.0f);
	outClip.ticksPerSecond = header.value("ticksPerSecond", 60.0f);
	outClip.nodeTransformChannels.clear();
	outClip.curves.clear();
	outClip.events.clear();
	outClip.syncMarkers.clear();
	outClip.syncGroupName.clear();
	outClip.rootMotion = {};

	// ── Reconstruct skeleton ──
	outSkeleton = {};
	outSkeleton.sourceSkeletonGuid = header["sourceSkeletonGuid"].get<std::string>();
	outSkeleton.globalInverseTransform = JsonToMat4(header["globalInverseTransform"]);
	const std::uint64_t storedSkeletonSignature = header["skeletonSignature"].get<std::uint64_t>();

	const auto& bonesJson = header["bones"];
	uint32_t boneCount = (uint32_t)bonesJson.size();

	outSkeleton.bones.resize(boneCount);
	outClip.boneKeyframes.assign(boneCount, {});

	std::vector<uint32_t> keyframeCounts(boneCount, 0);

	for (uint32_t b = 0; b < boneCount; b++)
	{
		const auto& bj = bonesJson[b];
		if (!bj.is_object() || !bj.contains("name") || !bj["name"].is_string()
			|| !bj.contains("guid") || !bj["guid"].is_string() || bj["guid"].get<std::string>().empty()
			|| !bj.contains("canonicalPath") || !bj["canonicalPath"].is_string()
			|| bj["canonicalPath"].get<std::string>().empty()
			|| !bj.contains("id") || !bj["id"].is_number_integer()
			|| bj["id"].get<int>() != static_cast<int>(b)
			|| !bj.contains("parentIndex") || !bj["parentIndex"].is_number_integer()
			|| !bj.contains("offsetMatrix") || !bj.contains("localTransform")
			|| !bj.contains("children") || !bj["children"].is_array()
			|| !bj.contains("keyframeCount") || !bj["keyframeCount"].is_number_unsigned())
		{
			VANS_LOG_ERROR("[VansAnimationClipIO] Invalid canonical bone entry " << b
				<< " in: " << filePath);
			return false;
		}
		BoneInfo& bone     = outSkeleton.bones[b];
		bone.name          = bj["name"].get<std::string>();
		bone.guid          = bj["guid"].get<std::string>();
		bone.canonicalPath = bj["canonicalPath"].get<std::string>();
		bone.id            = bj["id"].get<int>();
		bone.parentIndex   = bj["parentIndex"].get<int>();
		bone.offsetMatrix  = JsonToMat4(bj["offsetMatrix"]);
		bone.localTransform = JsonToMat4(bj["localTransform"]);

		for (const auto& c : bj["children"])
			bone.children.push_back(c.get<int>());

		keyframeCounts[b] = bj["keyframeCount"].get<std::uint32_t>();
	}

	std::vector<uint32_t> nodeKeyframeCounts;
	if (!header.contains("nodeTransformChannels") || !header["nodeTransformChannels"].is_array())
	{
		const std::optional<bool> nodeTransformChannels = ReadNodeTransformChannelsSetting(filePath);
		if (!nodeTransformChannels || *nodeTransformChannels)
		{
			VANS_LOG_ERROR("[VansAnimationClipIO] Missing nodeTransformChannels in current clip format: "
				<< filePath << " (set settings.nodeTransformChannels=false for skeletal-only clips)");
			return false;
		}
	}
	else
	{
		const auto& channelsJson = header["nodeTransformChannels"];
		outClip.nodeTransformChannels.resize(channelsJson.size());
		nodeKeyframeCounts.resize(channelsJson.size(), 0);
		for (size_t i = 0; i < channelsJson.size(); ++i)
		{
			const auto& cj = channelsJson[i];
			NodeTransformChannel& channel = outClip.nodeTransformChannels[i];
			channel.nodeName = cj.value("nodeName", "");
			channel.nodePath = cj.value("nodePath", "");
			channel.parentChannelIndex = cj.value("parentChannelIndex", -1);
			channel.bindLocalTransform = cj.contains("bindLocalTransform")
				? JsonToMat4(cj["bindLocalTransform"])
				: glm::mat4(1.0f);
			channel.bindModelTransform = cj.contains("bindModelTransform")
				? JsonToMat4(cj["bindModelTransform"])
				: glm::mat4(1.0f);
			nodeKeyframeCounts[i] = cj.value("keyframeCount", (uint32_t)0);
		}
	}

	try
	{
		if (header.contains("curves"))
		{
			if (!header["curves"].is_array())
				throw std::runtime_error("curves must be an array");
			for (const json& curveJson : header["curves"])
			{
				if (!curveJson.is_object() || !curveJson.contains("name") || !curveJson["name"].is_string()
				    || !curveJson.contains("keys") || !curveJson["keys"].is_array())
					throw std::runtime_error("invalid curve track");
				AnimationCurveTrack curve;
				curve.name = curveJson["name"].get<std::string>();
				curve.id = curveJson.value("id", VansAnimationStableId(curve.name));
				for (const json& keyJson : curveJson["keys"])
				{
					if (!keyJson.is_object() || !keyJson.contains("time") || !keyJson["time"].is_number()
					    || !keyJson.contains("value") || !keyJson["value"].is_number())
						throw std::runtime_error("invalid curve key");
					curve.keys.push_back({ keyJson["time"].get<float>(), keyJson["value"].get<float>() });
				}
				std::stable_sort(curve.keys.begin(), curve.keys.end(),
					[](const AnimationCurveKey& a, const AnimationCurveKey& b) { return a.time < b.time; });
				outClip.curves.push_back(std::move(curve));
			}
		}

		if (header.contains("events"))
		{
			if (!header["events"].is_array())
				throw std::runtime_error("events must be an array");
			for (const json& eventJson : header["events"])
			{
				if (!eventJson.is_object() || !eventJson.contains("time") || !eventJson["time"].is_number()
				    || !eventJson.contains("name") || !eventJson["name"].is_string())
					throw std::runtime_error("invalid animation event");
				AnimationClipEvent event;
				event.time = eventJson["time"].get<float>();
				event.name = eventJson["name"].get<std::string>();
				event.id = eventJson.value("id", VansAnimationStableId(event.name));
				if (eventJson.contains("payload") && !EventValueFromJson(eventJson["payload"], event.payload))
					throw std::runtime_error("invalid animation event payload");
				outClip.events.push_back(std::move(event));
			}
			std::stable_sort(outClip.events.begin(), outClip.events.end(),
				[](const AnimationClipEvent& a, const AnimationClipEvent& b) { return a.time < b.time; });
		}

		if (header.contains("sync"))
		{
			const json& syncJson = header["sync"];
			if (!syncJson.is_object() || !syncJson.contains("markers") || !syncJson["markers"].is_array())
				throw std::runtime_error("invalid sync marker block");
			outClip.syncGroupName = syncJson.value("group", "");
			for (const json& markerJson : syncJson["markers"])
			{
				if (!markerJson.is_object() || !markerJson.contains("time") || !markerJson["time"].is_number()
				    || !markerJson.contains("name") || !markerJson["name"].is_string())
					throw std::runtime_error("invalid sync marker");
				AnimationSyncMarker marker;
				marker.time = markerJson["time"].get<float>();
				marker.name = markerJson["name"].get<std::string>();
				marker.id = markerJson.value("id", VansAnimationStableId(marker.name));
				outClip.syncMarkers.push_back(std::move(marker));
			}
			std::stable_sort(outClip.syncMarkers.begin(), outClip.syncMarkers.end(),
				[](const AnimationSyncMarker& a, const AnimationSyncMarker& b) { return a.time < b.time; });
		}

		if (header.contains("rootMotion"))
		{
			const json& rootMotionJson = header["rootMotion"];
			if (!rootMotionJson.is_object())
				throw std::runtime_error("rootMotion must be an object");
			outClip.rootMotion.enabled = rootMotionJson.value("enabled", true);
			outClip.rootMotion.boneName = rootMotionJson.value("bone", "");
			outClip.rootMotion.extractTranslation = rootMotionJson.value("translation", true);
			outClip.rootMotion.extractRotation = rootMotionJson.value("rotation", true);
			outClip.rootMotion.extractScale = rootMotionJson.value("scale", false);
		}
	}
	catch (const std::exception& exception)
	{
		VANS_LOG_ERROR("[VansAnimationClipIO] Invalid animation payload metadata in "
			<< filePath << ": " << exception.what());
		return false;
	}

	// ── Read binary payload ──
	for (uint32_t b = 0; b < boneCount; b++)
	{
		uint32_t kfCount = keyframeCounts[b];
		outClip.boneKeyframes[b].resize(kfCount);
		for (uint32_t k = 0; k < kfCount; k++)
		{
			if (!ReadKeyframe(payloadReader, outClip.boneKeyframes[b][k]))
			{
				VANS_LOG_ERROR("[VansAnimationClipIO] Truncated keyframe payload in: " << filePath);
				return false;
			}
		}
	}
	for (size_t i = 0; i < outClip.nodeTransformChannels.size(); ++i)
	{
		uint32_t kfCount = i < nodeKeyframeCounts.size() ? nodeKeyframeCounts[i] : 0;
		outClip.nodeTransformChannels[i].keyframes.resize(kfCount);
		for (uint32_t k = 0; k < kfCount; ++k)
		{
			if (!ReadKeyframe(payloadReader, outClip.nodeTransformChannels[i].keyframes[k]))
			{
				VANS_LOG_ERROR("[VansAnimationClipIO] Truncated node transform keyframe payload in: " << filePath);
				return false;
			}
		}
	}

	// 从 .vclip 还原的骨架也需要拓扑排序和稳定身份索引。
	outSkeleton.BuildTopologicalOrder();
	outSkeleton.RebuildIdentityMapsAndSignature();
	if (outSkeleton.signature != storedSkeletonSignature)
	{
		VANS_LOG_ERROR("[VansAnimationClipIO] Skeleton signature mismatch in: " << filePath);
		return false;
	}

	return true;
}

// ════════════════════════════════════════════════════════════════
//  Peek (metadata only, no keyframes)
// ════════════════════════════════════════════════════════════════

bool VansGraphics::VansAnimationClipIO::Peek(const std::string& filePath,
                                              VansAnimationClipInfo& outInfo)
{
	std::string bytes;
	std::string headerStr;
	BinarySpanReader payloadReader;
	if (!ReadClipEnvelope(filePath, bytes, outInfo.version, headerStr, payloadReader, false))
		return false;
	if (outInfo.version != VCLIP_VERSION)
		return false;

	try
	{
		json header = json::parse(headerStr);
		if (!header.contains("nodeTransformChannels") || !header["nodeTransformChannels"].is_array())
			return false;
		outInfo.clipName  = header.value("clipName", "");
		outInfo.duration  = header.value("duration", 0.0f);
		outInfo.boneCount = header.value("boneCount", (uint32_t)0);
			outInfo.nodeTransformChannelCount = static_cast<uint32_t>(header["nodeTransformChannels"].size());
			outInfo.curveCount = header.contains("curves") && header["curves"].is_array()
				? static_cast<uint32_t>(header["curves"].size()) : 0;
			outInfo.eventCount = header.contains("events") && header["events"].is_array()
				? static_cast<uint32_t>(header["events"].size()) : 0;
			outInfo.syncMarkerCount = header.contains("sync") && header["sync"].is_object()
				&& header["sync"].contains("markers") && header["sync"]["markers"].is_array()
				? static_cast<uint32_t>(header["sync"]["markers"].size()) : 0;
	}
	catch (...)
	{
		return false;
	}

	return true;
}
