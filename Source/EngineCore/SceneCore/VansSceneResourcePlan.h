#pragma once

#include "../AssetCore/VansSkeletalMeshImportSettings.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Vans
{
struct VansSceneMeshResourceRequest
{
	std::string name;
	std::string assetGuid;
	std::string path;
	std::string artifactPath;
	bool needTangent = true;
	bool supportRayTracing = true;
	bool needCpuData = false;
	float scaleFactor = 1.0f;
	bool loadMultiMesh = false;
	VansSkeletalMeshImportSettings skeletalImport;
	bool cookedOnly = false;
};

struct VansSceneTextureResourceRequest
{
	std::string name;
	std::string assetGuid;
	std::string path;
	std::string artifactPath;
	int textureType = 0;
	bool srgb = true;
	bool useCompress = true;
	bool needMip = true;
	std::string precision = "low8";
	int importChannel = 4;
	std::string addressMode = "repeat";
	bool cookedOnly = false;
};

struct VansSceneAudioResourceRequest
{
	std::string name;
	std::string assetGuid;
	std::string path;
	std::string playMode = "static";
	bool loop = false;
	bool autoPlay = false;
	float volume = 1.0f;
	float pitch = 1.0f;
	bool spatial = false;
	float referenceDistance = 1.0f;
	float maxDistance = 100.0f;
	float rolloff = 1.0f;
	std::string attenuationMode = "linear";
	float reverbSend = 0.0f;
	std::string bus = "SFX";
	float lowpassHighFrequencyGain = 1.0f;
};

struct VansSceneVideoResourceRequest
{
	std::string name;
	std::string assetGuid;
	std::string path;
	bool loop = true;
	bool autoplay = false;
	bool srgb = true;
};

struct VansSceneShaderResourceRequest
{
	std::string name;
	std::string assetGuid;
	std::string source;
	std::string kind = "graphics";
	int pushConstantSize = -1;
	bool depthTest = true;
	bool depthWrite = true;
	std::string depthCompare = "lessOrEqual";
	std::string cull = "back";
	bool alphaBlend = false;
	bool decalBlend = false;
	bool additiveBlend = false;
	std::uint32_t additiveBlendAttachmentMask = 0;
	bool premultipliedAlphaBlend = false;
	int colorAttachmentCount = -1;
	std::string polygonMode = "fill";
	std::string frontFace = "counterClockwise";
	std::string primitiveTopology = "triangleList";
	std::uint32_t patchControlPoints = 1;
	std::string renderPath;
	std::map<std::string, std::string> stages;
	std::vector<std::string> materialPasses;
};

struct VansSceneResourceBuildPlan
{
	std::vector<VansSceneMeshResourceRequest> meshes;
	std::vector<VansSceneTextureResourceRequest> textures;
	std::vector<VansSceneShaderResourceRequest> shaders;
	std::vector<VansSceneAudioResourceRequest> audios;
	std::vector<VansSceneVideoResourceRequest> videos;
	bool includeDefaultTextureSet = true;
	bool loadRegisteredShaders = true;
};
}
