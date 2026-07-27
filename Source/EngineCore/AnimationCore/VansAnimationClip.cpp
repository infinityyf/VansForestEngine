#include "VansAnimationClip.h"
#include "../AssetCore/Storage/VansFileStorage.h"
#include "../Util/VansLog.h"

#include <nlohmann/json.hpp>
#include <cstring>
#include <limits>
#include <sstream>

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
	header["duration"]       = clip.duration;
	header["ticksPerSecond"] = clip.ticksPerSecond;
	header["boneCount"]      = (uint32_t)skeleton.bones.size();

	// Serialize globalInverseTransform
	header["globalInverseTransform"] = Mat4ToJson(skeleton.globalInverseTransform);

	json bonesJson = json::array();
	uint64_t totalKeyframes = 0;

	for (size_t b = 0; b < skeleton.bones.size(); b++)
	{
		const BoneInfo& bone = skeleton.bones[b];
		json boneJson;
		boneJson["name"]         = bone.name;
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

	std::string headerStr = header.dump();
	uint32_t headerSize   = (uint32_t)headerStr.size();
	uint64_t payloadSize  = totalKeyframes * BYTES_PER_KEYFRAME;

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
	         << " (" << skeleton.bones.size() << " bones, " << totalKeyframes << " keyframes)");
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
	if (version > VCLIP_VERSION)
	{
		VANS_LOG_WARN("[VansAnimationClipIO] File version " << version
		             << " is newer than supported " << VCLIP_VERSION << ", attempting load anyway.");
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

	outClip.clipName       = header.value("clipName", "");
	outClip.duration       = header.value("duration", 0.0f);
	outClip.ticksPerSecond = header.value("ticksPerSecond", 60.0f);

	// ── Reconstruct skeleton ──
	if (header.contains("globalInverseTransform"))
		outSkeleton.globalInverseTransform = JsonToMat4(header["globalInverseTransform"]);

	outSkeleton.bones.clear();
	outSkeleton.boneNameToIndex.clear();

	const auto& bonesJson = header["bones"];
	uint32_t boneCount = (uint32_t)bonesJson.size();

	outSkeleton.bones.resize(boneCount);
	outClip.boneKeyframes.resize(boneCount);

	std::vector<uint32_t> keyframeCounts(boneCount, 0);

	for (uint32_t b = 0; b < boneCount; b++)
	{
		const auto& bj = bonesJson[b];
		BoneInfo& bone     = outSkeleton.bones[b];
		bone.name          = bj.value("name", "");
		bone.id            = bj.value("id", (int)b);
		bone.parentIndex   = bj.value("parentIndex", -1);
		bone.offsetMatrix  = bj.contains("offsetMatrix") ? JsonToMat4(bj["offsetMatrix"]) : glm::mat4(1.0f);
		bone.localTransform = bj.contains("localTransform") ? JsonToMat4(bj["localTransform"]) : glm::mat4(1.0f);

		if (bj.contains("children"))
		{
			for (const auto& c : bj["children"])
				bone.children.push_back(c.get<int>());
		}

		keyframeCounts[b] = bj.value("keyframeCount", (uint32_t)0);
		outSkeleton.boneNameToIndex[bone.name] = bone.id;
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

	// 从 .vclip 还原的骨架也需要拓扑排序
	outSkeleton.BuildTopologicalOrder();

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

	try
	{
		json header = json::parse(headerStr);
		outInfo.clipName  = header.value("clipName", "");
		outInfo.duration  = header.value("duration", 0.0f);
		outInfo.boneCount = header.value("boneCount", (uint32_t)0);
	}
	catch (...)
	{
		return false;
	}

	return true;
}
