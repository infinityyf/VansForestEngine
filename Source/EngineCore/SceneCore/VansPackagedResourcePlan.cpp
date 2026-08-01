#include "VansPackagedResourcePlan.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace Vans
{
namespace
{
	using Json = nlohmann::ordered_json;

	std::string NormalizePathString(const fs::path& path)
	{
		return path.lexically_normal().generic_string();
	}

	bool IsPathUnder(const fs::path& childPath, const fs::path& parentPath)
	{
		const fs::path child = childPath.lexically_normal();
		const fs::path parent = parentPath.lexically_normal();

		auto childIt = child.begin();
		auto parentIt = parent.begin();
		for (; parentIt != parent.end(); ++parentIt, ++childIt)
		{
			if (childIt == child.end())
				return false;
			if (*childIt != *parentIt)
				return false;
		}
		return true;
	}

	std::string MakeStoredPath(const std::string& value, const fs::path& contentRoot)
	{
		if (value.empty())
			return {};

		std::error_code ec;
		const fs::path path(value);
		if (path.is_absolute() && IsPathUnder(path, contentRoot))
		{
			const fs::path relativePath = fs::relative(path, contentRoot, ec);
			if (!ec)
				return NormalizePathString(relativePath);
		}
		return NormalizePathString(path);
	}

	std::string ResolveStoredPath(const std::string& value, const fs::path& contentRoot)
	{
		if (value.empty())
			return {};
		fs::path path(value);
		if (!path.is_absolute())
			path = contentRoot / path;
		return NormalizePathString(path);
	}

	Json EncodeMesh(const VansSceneMeshResourceRequest& request, const fs::path& contentRoot)
	{
		Json json;
		json["name"] = request.name;
		json["assetGuid"] = request.assetGuid;
		json["path"] = request.path;
		json["artifactPath"] = MakeStoredPath(request.artifactPath, contentRoot);
		json["needTangent"] = request.needTangent;
		json["supportRayTracing"] = request.supportRayTracing;
		json["needCpuData"] = request.needCpuData;
		json["scaleFactor"] = request.scaleFactor;
		json["loadMultiMesh"] = request.loadMultiMesh;
		json["rebuildIdentityBoneOffsetsFromHierarchy"] = request.rebuildIdentityBoneOffsetsFromHierarchy;
		json["remapWeaponAttachmentBonesToHands"] = request.remapWeaponAttachmentBonesToHands;
		json["cookedOnly"] = request.cookedOnly;
		return json;
	}

	VansSceneMeshResourceRequest DecodeMesh(const Json& json, const fs::path& contentRoot)
	{
		VansSceneMeshResourceRequest request;
		request.name = json.value("name", "");
		request.assetGuid = json.at("assetGuid").get<std::string>();
		request.path = json.value("path", "");
		request.artifactPath = ResolveStoredPath(json.value("artifactPath", ""), contentRoot);
		request.needTangent = json.value("needTangent", request.needTangent);
		request.supportRayTracing = json.value("supportRayTracing", request.supportRayTracing);
		request.needCpuData = json.value("needCpuData", request.needCpuData);
		request.scaleFactor = json.value("scaleFactor", request.scaleFactor);
		request.loadMultiMesh = json.value("loadMultiMesh", request.loadMultiMesh);
		request.rebuildIdentityBoneOffsetsFromHierarchy =
			json.value("rebuildIdentityBoneOffsetsFromHierarchy", request.rebuildIdentityBoneOffsetsFromHierarchy);
		request.remapWeaponAttachmentBonesToHands =
			json.value("remapWeaponAttachmentBonesToHands", request.remapWeaponAttachmentBonesToHands);
		request.cookedOnly = json.value("cookedOnly", request.cookedOnly);
		return request;
	}

	Json EncodeTexture(const VansSceneTextureResourceRequest& request, const fs::path& contentRoot)
	{
		Json json;
		json["name"] = request.name;
		json["assetGuid"] = request.assetGuid;
		json["path"] = request.path;
		json["artifactPath"] = MakeStoredPath(request.artifactPath, contentRoot);
		json["textureType"] = request.textureType;
		json["srgb"] = request.srgb;
		json["useCompress"] = request.useCompress;
		json["needMip"] = request.needMip;
		json["precision"] = request.precision;
		json["importChannel"] = request.importChannel;
		json["addressMode"] = request.addressMode;
		json["cookedOnly"] = request.cookedOnly;
		return json;
	}

	VansSceneTextureResourceRequest DecodeTexture(const Json& json, const fs::path& contentRoot)
	{
		VansSceneTextureResourceRequest request;
		request.name = json.value("name", "");
		request.assetGuid = json.at("assetGuid").get<std::string>();
		request.path = json.value("path", "");
		request.artifactPath = ResolveStoredPath(json.value("artifactPath", ""), contentRoot);
		request.textureType = json.value("textureType", request.textureType);
		request.srgb = json.value("srgb", request.srgb);
		request.useCompress = json.value("useCompress", request.useCompress);
		request.needMip = json.value("needMip", request.needMip);
		request.precision = json.value("precision", request.precision);
		request.importChannel = json.value("importChannel", request.importChannel);
		request.addressMode = json.value("addressMode", request.addressMode);
		request.cookedOnly = json.value("cookedOnly", request.cookedOnly);
		return request;
	}

	Json EncodeAudio(const VansSceneAudioResourceRequest& request)
	{
		Json json;
		json["name"] = request.name;
		json["assetGuid"] = request.assetGuid;
		json["path"] = request.path;
		json["playMode"] = request.playMode;
		json["loop"] = request.loop;
		json["autoPlay"] = request.autoPlay;
		json["volume"] = request.volume;
		json["pitch"] = request.pitch;
		json["spatial"] = request.spatial;
		json["referenceDistance"] = request.referenceDistance;
		json["maxDistance"] = request.maxDistance;
		json["rolloff"] = request.rolloff;
		return json;
	}

	VansSceneAudioResourceRequest DecodeAudio(const Json& json)
	{
		VansSceneAudioResourceRequest request;
		request.name = json.value("name", "");
		request.assetGuid = json.at("assetGuid").get<std::string>();
		request.path = json.value("path", "");
		request.playMode = json.value("playMode", request.playMode);
		request.loop = json.value("loop", request.loop);
		request.autoPlay = json.value("autoPlay", request.autoPlay);
		request.volume = json.value("volume", request.volume);
		request.pitch = json.value("pitch", request.pitch);
		request.spatial = json.value("spatial", request.spatial);
		request.referenceDistance = json.value("referenceDistance", request.referenceDistance);
		request.maxDistance = json.value("maxDistance", request.maxDistance);
		request.rolloff = json.value("rolloff", request.rolloff);
		return request;
	}

	Json EncodeVideo(const VansSceneVideoResourceRequest& request)
	{
		Json json;
		json["name"] = request.name;
		json["assetGuid"] = request.assetGuid;
		json["path"] = request.path;
		json["loop"] = request.loop;
		json["autoplay"] = request.autoplay;
		json["srgb"] = request.srgb;
		return json;
	}

	VansSceneVideoResourceRequest DecodeVideo(const Json& json)
	{
		VansSceneVideoResourceRequest request;
		request.name = json.value("name", "");
		request.assetGuid = json.at("assetGuid").get<std::string>();
		request.path = json.value("path", "");
		request.loop = json.value("loop", request.loop);
		request.autoplay = json.value("autoplay", request.autoplay);
		request.srgb = json.value("srgb", request.srgb);
		return request;
	}

	Json EncodeShader(const VansSceneShaderResourceRequest& request)
	{
		Json json;
		json["name"] = request.name;
		json["assetGuid"] = request.assetGuid;
		json["source"] = request.source;
		json["kind"] = request.kind;
		json["pushConstantSize"] = request.pushConstantSize;
		json["depthTest"] = request.depthTest;
		json["depthWrite"] = request.depthWrite;
		json["depthCompare"] = request.depthCompare;
		json["cull"] = request.cull;
		json["alphaBlend"] = request.alphaBlend;
		json["decalBlend"] = request.decalBlend;
		json["additiveBlend"] = request.additiveBlend;
		json["additiveBlendAttachmentMask"] = request.additiveBlendAttachmentMask;
		json["premultipliedAlphaBlend"] = request.premultipliedAlphaBlend;
		json["colorAttachmentCount"] = request.colorAttachmentCount;
		json["polygonMode"] = request.polygonMode;
		json["frontFace"] = request.frontFace;
		json["primitiveTopology"] = request.primitiveTopology;
		json["patchControlPoints"] = request.patchControlPoints;
		json["renderPath"] = request.renderPath;
		json["stages"] = request.stages;
		json["materialPasses"] = request.materialPasses;
		return json;
	}

	VansSceneShaderResourceRequest DecodeShader(const Json& json)
	{
		VansSceneShaderResourceRequest request;
		request.name = json.value("name", "");
		request.assetGuid = json.at("assetGuid").get<std::string>();
		request.source = json.value("source", "");
		request.kind = json.value("kind", request.kind);
		request.pushConstantSize = json.value("pushConstantSize", request.pushConstantSize);
		request.depthTest = json.value("depthTest", request.depthTest);
		request.depthWrite = json.value("depthWrite", request.depthWrite);
		request.depthCompare = json.value("depthCompare", request.depthCompare);
		request.cull = json.value("cull", request.cull);
		request.alphaBlend = json.value("alphaBlend", request.alphaBlend);
		request.decalBlend = json.value("decalBlend", request.decalBlend);
		request.additiveBlend = json.value("additiveBlend", request.additiveBlend);
		request.additiveBlendAttachmentMask = json.value("additiveBlendAttachmentMask", request.additiveBlendAttachmentMask);
		request.premultipliedAlphaBlend = json.value("premultipliedAlphaBlend", request.premultipliedAlphaBlend);
		request.colorAttachmentCount = json.value("colorAttachmentCount", request.colorAttachmentCount);
		request.polygonMode = json.value("polygonMode", request.polygonMode);
		request.frontFace = json.value("frontFace", request.frontFace);
		request.primitiveTopology = json.value("primitiveTopology", request.primitiveTopology);
		request.patchControlPoints = json.value("patchControlPoints", request.patchControlPoints);
		request.renderPath = json.value("renderPath", request.renderPath);
		request.stages = json.value("stages", request.stages);
		request.materialPasses = json.value("materialPasses", request.materialPasses);
		return request;
	}

	Json EncodeAssetIndexRecord(const VansPackagedAssetIndexRecord& record, const fs::path& contentRoot)
	{
		Json json;
		json["guid"] = record.guid;
		json["type"] = record.type;
		json["sourcePath"] = MakeStoredPath(record.sourcePath, contentRoot);
		json["authoringPath"] = MakeStoredPath(record.authoringPath, contentRoot);
		json["artifactPath"] = MakeStoredPath(record.artifactPath, contentRoot);
		json["artifactFormat"] = record.artifactFormat;
		json["sourceHash"] = record.sourceHash;
		json["metaHash"] = record.metaHash;
		json["missing"] = record.missing;
		return json;
	}

	VansPackagedAssetIndexRecord DecodeAssetIndexRecord(const Json& json, const fs::path& contentRoot)
	{
		VansPackagedAssetIndexRecord record;
		record.guid = json.value("guid", "");
		record.type = json.value("type", "");
		record.sourcePath = ResolveStoredPath(json.value("sourcePath", ""), contentRoot);
		record.authoringPath = ResolveStoredPath(json.value("authoringPath", ""), contentRoot);
		record.artifactPath = ResolveStoredPath(json.value("artifactPath", ""), contentRoot);
		record.artifactFormat = json.at("artifactFormat").get<std::string>();
		record.sourceHash = json.value("sourceHash", static_cast<std::uint64_t>(0));
		record.metaHash = json.value("metaHash", static_cast<std::uint64_t>(0));
		record.missing = json.value("missing", record.missing);
		return record;
	}
}

const char* VansPackagedResourcePlanIO::DefaultRelativePath()
{
	return "Library/Package/ResourcePlan.json";
}

bool VansPackagedResourcePlanIO::Save(
	const fs::path& path,
	const VansPackagedResourcePlan& plan,
	const fs::path& sourceContentRoot,
	std::string& error)
{
	try
	{
		Json root;
		root["format"] = "ForestPackagedResourcePlan";

		Json resourcePlan;
		resourcePlan["includeDefaultTextureSet"] = plan.resourcePlan.includeDefaultTextureSet;
		resourcePlan["loadRegisteredShaders"] = plan.resourcePlan.loadRegisteredShaders;

		resourcePlan["meshes"] = Json::array();
		for (const auto& mesh : plan.resourcePlan.meshes)
			resourcePlan["meshes"].push_back(EncodeMesh(mesh, sourceContentRoot));

		resourcePlan["textures"] = Json::array();
		for (const auto& texture : plan.resourcePlan.textures)
			resourcePlan["textures"].push_back(EncodeTexture(texture, sourceContentRoot));

		resourcePlan["shaders"] = Json::array();
		for (const auto& shader : plan.resourcePlan.shaders)
			resourcePlan["shaders"].push_back(EncodeShader(shader));

		resourcePlan["audios"] = Json::array();
		for (const auto& audio : plan.resourcePlan.audios)
			resourcePlan["audios"].push_back(EncodeAudio(audio));

		resourcePlan["videos"] = Json::array();
		for (const auto& video : plan.resourcePlan.videos)
			resourcePlan["videos"].push_back(EncodeVideo(video));

		root["resourcePlan"] = std::move(resourcePlan);
		root["runtimeAssetBindings"] = plan.runtimeAssetBindings;
		root["assetIndex"] = Json::array();
		for (const auto& record : plan.assetIndex)
			root["assetIndex"].push_back(EncodeAssetIndexRecord(record, sourceContentRoot));

		std::error_code ec;
		fs::create_directories(path.parent_path(), ec);
		if (ec)
		{
			error = "Cannot create packaged resource plan directory: " + path.parent_path().string();
			return false;
		}

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			error = "Cannot write packaged resource plan: " + path.string();
			return false;
		}
		file << root.dump(2) << '\n';
		return true;
	}
	catch (const std::exception& exception)
	{
		error = exception.what();
		return false;
	}
}

bool VansPackagedResourcePlanIO::Load(
	const fs::path& path,
	const fs::path& packageContentRoot,
	VansPackagedResourcePlan& outPlan,
	std::string& error)
{
	try
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			error = "Cannot read packaged resource plan: " + path.string();
			return false;
		}

		Json root;
		file >> root;
		if (root.value("format", "") != "ForestPackagedResourcePlan")
		{
			error = "Unsupported packaged resource plan format: " + root.value("format", "");
			return false;
		}

		VansPackagedResourcePlan loaded;
		const Json& resourcePlan = root.at("resourcePlan");
		loaded.resourcePlan.includeDefaultTextureSet =
			resourcePlan.value("includeDefaultTextureSet", loaded.resourcePlan.includeDefaultTextureSet);
		loaded.resourcePlan.loadRegisteredShaders =
			resourcePlan.value("loadRegisteredShaders", loaded.resourcePlan.loadRegisteredShaders);

		for (const auto& mesh : resourcePlan.value("meshes", Json::array()))
			loaded.resourcePlan.meshes.push_back(DecodeMesh(mesh, packageContentRoot));
		for (const auto& texture : resourcePlan.value("textures", Json::array()))
			loaded.resourcePlan.textures.push_back(DecodeTexture(texture, packageContentRoot));
		for (const auto& shader : resourcePlan.value("shaders", Json::array()))
			loaded.resourcePlan.shaders.push_back(DecodeShader(shader));
		for (const auto& audio : resourcePlan.value("audios", Json::array()))
			loaded.resourcePlan.audios.push_back(DecodeAudio(audio));
		for (const auto& video : resourcePlan.value("videos", Json::array()))
			loaded.resourcePlan.videos.push_back(DecodeVideo(video));

		loaded.runtimeAssetBindings =
			root.value("runtimeAssetBindings", std::map<std::string, std::string>{});
		for (const auto& record : root.value("assetIndex", Json::array()))
			loaded.assetIndex.push_back(DecodeAssetIndexRecord(record, packageContentRoot));

		outPlan = std::move(loaded);
		return true;
	}
	catch (const std::exception& exception)
	{
		error = exception.what();
		return false;
	}
}
}
