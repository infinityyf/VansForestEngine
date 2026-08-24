#include "../EngineCore/AnimationCore/VansAnimatorIO.h"
#include "../EngineCore/AnimationCore/VansAnimationClip.h"
#include "../EngineCore/AnimationCore/VansSkinnedMeshLoader.h"
#include "../EngineCore/AnimationCore/Storage/VansAnimationRigStorage.h"
#include "../EngineCore/AnimationCore/Storage/VansBoneMaskStorage.h"
#include "../EngineCore/AnimationCore/Storage/VansRetargetProfileStorage.h"
#include "../EngineCore/AssetCore/VansAssetDatabase.h"
#include "../EngineCore/AssetCore/Importers/VansModelImporter.h"
#include "../EngineCore/AssetCore/Storage/VansAssetMetaStorage.h"
#include "../EngineCore/AssetCore/Storage/VansJsonFileStorage.h"
#include "../EngineCore/ScriptCore/VansLuaScriptInspectorService.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/gtx/matrix_decompose.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	enum class Command
	{
		Invalid,
		Rewrite,
		Validate,
		ImportAnimationFbx,
		RebuildAnimationClips,
		RefreshSkeletonSubAssets,
		InspectSkeleton,
		ValidateLuaScript
	};

	struct Options
	{
		Command command = Command::Invalid;
		fs::path projectRoot;
		fs::path sourcePath;
		fs::path skeletonClipPath;
		fs::path destinationRoot;
		std::vector<fs::path> sourceRoots;
		fs::path scriptPath;
		std::string entryName;
		std::vector<std::string> boneNames;
		bool write = false;
		bool showHelp = false;
	};

	void PrintUsage()
	{
		std::cout
			<< "ForestAssetTool\n"
			<< "Usage:\n"
			<< "  ForestAssetTool rewrite-animation-assets --project <path> --dry-run\n"
			<< "  ForestAssetTool rewrite-animation-assets --project <path> --write\n"
			<< "  ForestAssetTool validate-animation-assets --project <path>\n"
			<< "  ForestAssetTool import-animation-fbx --project <path> --source <asset-relative-fbx>"
				" --skeleton <asset-relative-vclip>\n"
			<< "  ForestAssetTool rebuild-animation-clips --project <path>"
				" --source-root <path>... --destination-root <asset-relative-directory>"
				" --skeleton <asset-relative-model|none> (--dry-run|--write)\n"
			<< "  ForestAssetTool refresh-skeleton-subassets --project <path>"
				" (--dry-run|--write)\n"
			<< "  ForestAssetTool inspect-skeleton --project <path> --source <asset-relative-model>"
				" [--bone <name>]...\n"
			<< "  ForestAssetTool validate-lua-script --project <path> --script <project-relative-lua>"
				" --entry <table-name>\n";
	}

	bool ParseOptions(int argc, char** argv, Options& options, std::string& error)
	{
		if (argc < 2)
		{
			error = "Missing command.";
			return false;
		}
		const std::string command = argv[1];
		if (command == "rewrite-animation-assets") options.command = Command::Rewrite;
		else if (command == "validate-animation-assets") options.command = Command::Validate;
		else if (command == "import-animation-fbx") options.command = Command::ImportAnimationFbx;
		else if (command == "rebuild-animation-clips") options.command = Command::RebuildAnimationClips;
		else if (command == "refresh-skeleton-subassets") options.command = Command::RefreshSkeletonSubAssets;
		else if (command == "inspect-skeleton") options.command = Command::InspectSkeleton;
		else if (command == "validate-lua-script") options.command = Command::ValidateLuaScript;
		else if (command == "--help" || command == "-h") options.showHelp = true;
		else
		{
			error = "Unknown command: " + command;
			return false;
		}

		int modeCount = 0;
		for (int index = 2; index < argc; ++index)
		{
			const std::string argument = argv[index];
			if (argument == "--project")
			{
				if (++index >= argc)
				{
					error = "Missing value for --project.";
					return false;
				}
				options.projectRoot = argv[index];
			}
			else if (argument == "--source")
			{
				if (++index >= argc)
				{
					error = "Missing value for --source.";
					return false;
				}
				options.sourcePath = argv[index];
			}
			else if (argument == "--skeleton")
			{
				if (++index >= argc)
				{
					error = "Missing value for --skeleton.";
					return false;
				}
				options.skeletonClipPath = argv[index];
			}
			else if (argument == "--source-root")
			{
				if (++index >= argc)
				{
					error = "Missing value for --source-root.";
					return false;
				}
				options.sourceRoots.emplace_back(argv[index]);
			}
			else if (argument == "--destination-root")
			{
				if (++index >= argc)
				{
					error = "Missing value for --destination-root.";
					return false;
				}
				options.destinationRoot = argv[index];
			}
			else if (argument == "--script")
			{
				if (++index >= argc)
				{
					error = "Missing value for --script.";
					return false;
				}
				options.scriptPath = argv[index];
			}
			else if (argument == "--entry")
			{
				if (++index >= argc)
				{
					error = "Missing value for --entry.";
					return false;
				}
				options.entryName = argv[index];
			}
			else if (argument == "--bone")
			{
				if (++index >= argc)
				{
					error = "Missing value for --bone.";
					return false;
				}
				options.boneNames.emplace_back(argv[index]);
			}
			else if (argument == "--dry-run")
			{
				options.write = false;
				++modeCount;
			}
			else if (argument == "--write")
			{
				options.write = true;
				++modeCount;
			}
			else if (argument == "--help" || argument == "-h")
				options.showHelp = true;
			else
			{
				error = "Unknown option: " + argument;
				return false;
			}
		}
		if (options.showHelp) return true;
		if (options.projectRoot.empty())
		{
			error = "--project is required.";
			return false;
		}
		if (options.command == Command::Rewrite && modeCount != 1)
		{
			error = "Rewrite requires exactly one of --dry-run or --write.";
			return false;
		}
		if (options.command == Command::Validate && modeCount != 0)
		{
			error = "Validate does not accept --dry-run or --write.";
			return false;
		}
		if (options.command == Command::ImportAnimationFbx
			&& (modeCount != 0 || options.sourcePath.empty() || options.skeletonClipPath.empty()))
		{
			error = "Import requires --source and --skeleton and does not accept a rewrite mode.";
			return false;
		}
		if (options.command == Command::RebuildAnimationClips
			&& (modeCount != 1 || options.sourceRoots.empty()
				|| options.destinationRoot.empty() || options.skeletonClipPath.empty()))
		{
			error = "Clip rebuild requires --source-root, --destination-root, --skeleton,"
				" and exactly one of --dry-run or --write.";
			return false;
		}
		if (options.command == Command::RefreshSkeletonSubAssets && modeCount != 1)
		{
			error = "Skeleton sub-asset refresh requires exactly one of --dry-run or --write.";
			return false;
		}
		if (options.command == Command::InspectSkeleton
			&& (modeCount != 0 || options.sourcePath.empty()))
		{
			error = "Skeleton inspection requires --source and does not accept a rewrite mode.";
			return false;
		}
		if (options.command == Command::ValidateLuaScript
			&& (modeCount != 0 || options.scriptPath.empty() || options.entryName.empty()))
		{
			error = "Lua validation requires --script and --entry and does not accept a rewrite mode.";
			return false;
		}
		return true;
	}

	bool IsWithin(const fs::path& path, const fs::path& root);

	int RefreshSkeletonSubAssets(const fs::path& projectRoot,
		const fs::path& assetsRoot,
		const Options& options)
	{
		Vans::VansAssetDatabase database(assetsRoot);
		const Vans::VansAssetScanResult scan = database.Scan(
			Vans::VansAssetOperationPolicy::ReadOnly());
		if (!scan)
		{
			std::cerr << "Asset database scan failed:";
			for (const std::string& scanError : scan.errors)
				std::cerr << "\n  " << scanError;
			std::cerr << '\n';
			return 2;
		}

		std::vector<Vans::VansAssetRecord> models;
		for (const Vans::VansAssetRecord& record : database.All())
			if (record.type == Vans::VansAssetType::Model)
				models.push_back(record);
		std::sort(models.begin(), models.end(), [](const auto& left, const auto& right) {
			return left.sourcePath.generic_string() < right.sourcePath.generic_string();
		});

		Vans::VansModelImporter importer;
		std::size_t skeletalModels = 0;
		std::size_t changed = 0;
		std::size_t failed = 0;
		std::size_t boneSubAssets = 0;
		for (const Vans::VansAssetRecord& model : models)
		{
			Vans::VansAssetMeta meta;
			std::string error;
			if (!Vans::VansAssetMetaStorage::Load(model.metaPath, meta, error))
			{
				++failed;
				std::cerr << fs::relative(model.sourcePath, projectRoot).generic_string()
					<< ": " << error << '\n';
				continue;
			}
			const auto originalSubAssets = meta.subAssets;
			const Vans::VansSkeletonSubAssetRefreshResult refresh =
				importer.RefreshSkeletonSubAssets(model.sourcePath, meta);
			if (!refresh.succeeded)
			{
				++failed;
				std::cerr << fs::relative(model.sourcePath, projectRoot).generic_string()
					<< ": " << refresh.error << '\n';
				continue;
			}
			if (refresh.hasSkeleton)
				++skeletalModels;
			boneSubAssets += refresh.boneSubAssetCount;
			if (meta.subAssets == originalSubAssets)
				continue;
			++changed;
			if (options.write && !Vans::VansAssetMetaStorage::SaveAtomic(
				model.metaPath, meta, error))
			{
				++failed;
				std::cerr << fs::relative(model.metaPath, projectRoot).generic_string()
					<< ": " << error << '\n';
				continue;
			}
			std::cout << (options.write ? "Refreshed " : "Would refresh ")
				<< fs::relative(model.metaPath, projectRoot).generic_string()
				<< " bones=" << refresh.boneSubAssetCount << '\n';
		}

		std::cout << "Model assets: " << models.size()
			<< ", skeletal models: " << skeletalModels
			<< ", bone sub-assets: " << boneSubAssets
			<< ", metadata changes: " << changed
			<< ", failures: " << failed << '\n';
		if (!options.write && changed > 0)
			std::cout << "Dry run only; rerun with --write to publish atomic metadata updates.\n";
		return failed == 0 ? 0 : 1;
	}

	int InspectSkeleton(const fs::path& projectRoot, const fs::path& assetsRoot,
		const Options& options)
	{
		std::error_code fileError;
		const fs::path sourcePath = fs::weakly_canonical(projectRoot / options.sourcePath, fileError);
		if (fileError || !fs::is_regular_file(sourcePath) || !IsWithin(sourcePath, assetsRoot))
		{
			std::cerr << "Invalid project model source: " << options.sourcePath << '\n';
			return 2;
		}
		VansGraphics::Skeleton skeleton;
		std::string skeletonError;
		if (!VansGraphics::VansSkinnedMeshLoader::LoadSkeletonFromModelAsset(
			sourcePath.string(), skeleton, skeletonError))
		{
			std::cerr << skeletonError << ": " << sourcePath << '\n';
			return 1;
		}
		std::vector<glm::mat4> model(skeleton.bones.size(), glm::mat4(1.0f));
		std::vector<bool> resolved(skeleton.bones.size(), false);
		std::function<const glm::mat4&(int)> resolveModel = [&](int index) -> const glm::mat4&
		{
			if (resolved[static_cast<std::size_t>(index)])
				return model[static_cast<std::size_t>(index)];
			const auto& bone = skeleton.bones[static_cast<std::size_t>(index)];
			model[static_cast<std::size_t>(index)] = bone.parentIndex >= 0
				? resolveModel(bone.parentIndex) * bone.localTransform : bone.localTransform;
			resolved[static_cast<std::size_t>(index)] = true;
			return model[static_cast<std::size_t>(index)];
		};
		nlohmann::json output = {
			{ "source", fs::relative(sourcePath, projectRoot).generic_string() },
			{ "boneCount", skeleton.bones.size() },
			{ "bones", nlohmann::json::array() }
		};
		for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
		{
			const auto& bone = skeleton.bones[index];
			if (!options.boneNames.empty()
				&& std::find(options.boneNames.begin(), options.boneNames.end(), bone.name)
					== options.boneNames.end())
				continue;
			glm::vec3 localScale(1.0f), localTranslation(0.0f), localSkew(0.0f);
			glm::quat localRotation(1.0f, 0.0f, 0.0f, 0.0f);
			glm::vec4 localPerspective(0.0f);
			glm::decompose(bone.localTransform, localScale, localRotation,
				localTranslation, localSkew, localPerspective);
			glm::vec3 modelScale(1.0f), modelTranslation(0.0f), modelSkew(0.0f);
			glm::quat modelRotation(1.0f, 0.0f, 0.0f, 0.0f);
			glm::vec4 modelPerspective(0.0f);
			glm::decompose(resolveModel(static_cast<int>(index)), modelScale, modelRotation,
				modelTranslation, modelSkew, modelPerspective);
			output["bones"].push_back({
				{ "name", bone.name },
				{ "canonicalPath", bone.canonicalPath },
				{ "parent", bone.parentIndex >= 0
					? skeleton.bones[static_cast<std::size_t>(bone.parentIndex)].name : "" },
				{ "localTranslation", { localTranslation.x, localTranslation.y, localTranslation.z } },
				{ "localRotation", { localRotation.w, localRotation.x, localRotation.y, localRotation.z } },
				{ "modelTranslation", { modelTranslation.x, modelTranslation.y, modelTranslation.z } },
				{ "modelRotation", { modelRotation.w, modelRotation.x, modelRotation.y, modelRotation.z } }
			});
		}
		std::cout << output.dump(2) << '\n';
		return 0;
	}

	bool IsWithin(const fs::path& path, const fs::path& root)
	{
		auto pathIt = path.begin();
		auto rootIt = root.begin();
		for (; rootIt != root.end(); ++rootIt, ++pathIt)
			if (pathIt == path.end() || *pathIt != *rootIt)
				return false;
		return true;
	}

	int ImportAnimationFbx(const fs::path& projectRoot, const fs::path& assetsRoot,
		const Options& options)
	{
		std::error_code fileError;
		const fs::path sourcePath = fs::weakly_canonical(projectRoot / options.sourcePath, fileError);
		if (fileError || !fs::is_regular_file(sourcePath) || sourcePath.extension() != ".fbx"
			|| !IsWithin(sourcePath, assetsRoot))
		{
			std::cerr << "Invalid project FBX source: " << options.sourcePath << '\n';
			return 2;
		}

		const fs::path skeletonPath = fs::weakly_canonical(projectRoot / options.skeletonClipPath, fileError);
		if (fileError || !fs::is_regular_file(skeletonPath) || skeletonPath.extension() != ".vclip"
			|| !IsWithin(skeletonPath, assetsRoot))
		{
			std::cerr << "Invalid project skeleton clip: " << options.skeletonClipPath << '\n';
			return 2;
		}

		VansGraphics::VansAnimationClip referenceClip;
		VansGraphics::Skeleton skeleton;
		if (!VansGraphics::VansAnimationClipIO::Load(skeletonPath.string(), referenceClip, skeleton)
			|| skeleton.bones.empty())
		{
			std::cerr << "Failed to load reference skeleton from: " << skeletonPath << '\n';
			return 1;
		}

		std::vector<VansGraphics::VansAnimationClip> clips;
		if (!VansGraphics::VansSkinnedMeshLoader::ExtractExternAnimationClips(
			sourcePath.string(), skeleton, clips) || clips.empty())
		{
			std::cerr << "Failed to extract animation clips from: " << sourcePath << '\n';
			return 1;
		}

		Vans::VansAssetDatabase database(assetsRoot);
		std::string error;
		if (!database.RegisterOrRefresh(sourcePath, Vans::VansAssetOperationPolicy::Authoring(), error))
		{
			std::cerr << error << '\n';
			return 1;
		}

		const std::string baseName = sourcePath.stem().string();
		for (auto& clip : clips)
		{
			std::string clipName = clip.clipName;
			for (char& character : clipName)
				if (character == ' ' || character == '/' || character == '\\' || character == ':')
					character = '_';
			const fs::path clipPath = sourcePath.parent_path() / (baseName + "_" + clipName + ".vclip");

			int rootIndex = -1;
			for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
				if (skeleton.bones[index].name == "root")
				{
					rootIndex = static_cast<int>(index);
					break;
				}
			if (rootIndex < 0)
				for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
					if (skeleton.bones[index].parentIndex < 0)
					{
						rootIndex = static_cast<int>(index);
						break;
					}
			clip.rootMotion.enabled = true;
			clip.rootMotion.boneName = rootIndex >= 0 ? skeleton.bones[rootIndex].name : "";
			clip.rootMotion.extractTranslation = true;
			clip.rootMotion.extractRotation = true;
			clip.rootMotion.extractScale = false;
			if (!VansGraphics::VansAnimationClipIO::Save(clipPath.string(), clip, skeleton))
			{
				std::cerr << "Failed to publish root-motion clip: " << clipPath << '\n';
				return 1;
			}

			if (!fs::is_regular_file(clipPath)
				|| !database.RegisterOrRefresh(clipPath, Vans::VansAssetOperationPolicy::Authoring(), error))
			{
				std::cerr << (error.empty() ? "Animation clip was not created: " + clipPath.string() : error) << '\n';
				return 1;
			}

			std::cout << "Imported " << fs::relative(clipPath, projectRoot).generic_string()
				<< " clip=\"" << clip.clipName << "\" duration=" << clip.duration
				<< " bones=" << skeleton.bones.size();
			if (rootIndex >= 0 && rootIndex < static_cast<int>(clip.boneKeyframes.size())
				&& !clip.boneKeyframes[rootIndex].empty())
			{
				const auto& keys = clip.boneKeyframes[rootIndex];
				const auto delta = keys.back().position - keys.front().position;
				float maxRootTravel = 0.0f;
				float maxRootRotation = 0.0f;
				for (const auto& key : keys)
				{
					maxRootTravel = std::max(maxRootTravel, glm::length(key.position - keys.front().position));
					const float quaternionDot = std::clamp(
						std::abs(glm::dot(key.rotation, keys.front().rotation)), 0.0f, 1.0f);
					maxRootRotation = std::max(maxRootRotation, 2.0f * std::acos(quaternionDot));
				}
				std::cout << " root=\"" << skeleton.bones[rootIndex].name << "\""
					<< " rootKeys=" << keys.size()
					<< " rootDelta=(" << delta.x << ',' << delta.y << ',' << delta.z << ')'
					<< " maxRootTravel=" << maxRootTravel
					<< " maxRootRotationDegrees=" << maxRootRotation * 57.2957795f;
			}
			std::cout << '\n';
		}
		return 0;
	}

	std::string SanitizeAnimationName(std::string name)
	{
		for (char& character : name)
			if (character == ' ' || character == '/' || character == '\\' || character == ':')
				character = '_';
		return name;
	}

	bool SameRelativeDirectory(const fs::path& sourcePath,
		const fs::path& sourceRoot,
		const fs::path& clipPath,
		const fs::path& destinationRoot)
	{
		std::error_code error;
		const fs::path sourceRelative = fs::relative(sourcePath.parent_path(), sourceRoot, error);
		if (error) return false;
		const fs::path destinationRelative = fs::relative(
			clipPath.parent_path(), destinationRoot, error);
		return !error && sourceRelative.lexically_normal() == destinationRelative.lexically_normal();
	}

	int RebuildAnimationClips(const fs::path& projectRoot,
		const fs::path& assetsRoot,
		const Options& options)
	{
		std::error_code fileError;
		const fs::path destinationRoot = fs::weakly_canonical(
			projectRoot / options.destinationRoot, fileError);
		if (fileError || !fs::is_directory(destinationRoot)
			|| !IsWithin(destinationRoot, assetsRoot))
		{
			std::cerr << "Invalid Animation Clip destination root: "
				<< options.destinationRoot << '\n';
			return 2;
		}

		VansGraphics::Skeleton skeleton;
		const bool skeletal = options.skeletonClipPath != "none";
		if (skeletal)
		{
			const fs::path skeletonPath = fs::weakly_canonical(
				projectRoot / options.skeletonClipPath, fileError);
			if (fileError || !fs::is_regular_file(skeletonPath)
				|| !IsWithin(skeletonPath, assetsRoot))
			{
				std::cerr << "Invalid Skeleton model asset: " << options.skeletonClipPath << '\n';
				return 2;
			}
			std::string skeletonError;
			if (!VansGraphics::VansSkinnedMeshLoader::LoadSkeletonFromModelAsset(
				skeletonPath.string(), skeleton, skeletonError))
			{
				std::cerr << skeletonError << '\n';
				return 1;
			}
		}

		std::vector<fs::path> sourceRoots;
		std::vector<fs::path> sourceFiles;
		for (const fs::path& configuredRoot : options.sourceRoots)
		{
			fs::path root = configuredRoot.is_absolute()
				? configuredRoot : projectRoot / configuredRoot;
			root = fs::weakly_canonical(root, fileError);
			if (fileError || !fs::is_directory(root))
			{
				std::cerr << "Invalid animation source root: " << configuredRoot << '\n';
				return 2;
			}
			sourceRoots.push_back(root);
			for (fs::recursive_directory_iterator iterator(root,
				fs::directory_options::skip_permission_denied, fileError), end;
				iterator != end; iterator.increment(fileError))
			{
				if (fileError)
				{
					std::cerr << "Animation source scan failed: " << fileError.message() << '\n';
					return 2;
				}
				if (!iterator->is_regular_file()) continue;
				std::string extension = iterator->path().extension().string();
				std::transform(extension.begin(), extension.end(), extension.begin(),
					[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
				if (extension == ".fbx")
					sourceFiles.push_back(iterator->path());
			}
		}
		std::sort(sourceFiles.begin(), sourceFiles.end());
		sourceFiles.erase(std::unique(sourceFiles.begin(), sourceFiles.end()), sourceFiles.end());

		std::vector<fs::path> clipFiles;
		for (fs::recursive_directory_iterator iterator(destinationRoot,
			fs::directory_options::skip_permission_denied, fileError), end;
			iterator != end; iterator.increment(fileError))
		{
			if (fileError)
			{
				std::cerr << "Animation Clip scan failed: " << fileError.message() << '\n';
				return 2;
			}
			if (iterator->is_regular_file() && iterator->path().extension() == ".vclip")
				clipFiles.push_back(iterator->path());
		}
		std::sort(clipFiles.begin(), clipFiles.end());

		std::size_t rebuilt = 0;
		std::size_t failed = 0;
		for (const fs::path& clipPath : clipFiles)
		{
			const std::string clipStem = clipPath.stem().string();
			const fs::path* matchedSource = nullptr;
			std::size_t bestScore = 0;
			bool ambiguous = false;
			for (const fs::path& sourcePath : sourceFiles)
			{
				const std::string sourceStem = sourcePath.stem().string();
				if (clipStem.size() <= sourceStem.size()
					|| clipStem.compare(0, sourceStem.size(), sourceStem) != 0
					|| clipStem[sourceStem.size()] != '_')
					continue;
				std::size_t score = sourceStem.size();
				for (const fs::path& sourceRoot : sourceRoots)
					if (IsWithin(sourcePath, sourceRoot)
						&& SameRelativeDirectory(sourcePath, sourceRoot,
							clipPath, destinationRoot))
					{
						score += 1000000;
						break;
					}
				if (score > bestScore)
				{
					matchedSource = &sourcePath;
					bestScore = score;
					ambiguous = false;
				}
				else if (score == bestScore && matchedSource && *matchedSource != sourcePath)
					ambiguous = true;
			}

			const std::string relativeClip = fs::relative(
				clipPath, projectRoot).generic_string();
			if (!matchedSource || ambiguous)
			{
				++failed;
				std::cerr << relativeClip << ": "
					<< (!matchedSource ? "no matching source FBX" : "ambiguous source FBX") << '\n';
				continue;
			}

			const std::string desiredAnimationName = clipStem.substr(
				matchedSource->stem().string().size() + 1);
			Assimp::Importer importer;
			const aiScene* scene = importer.ReadFile(matchedSource->string(),
				aiProcess_Triangulate | aiProcess_FlipUVs);
			if (!scene || !scene->HasAnimations())
			{
				++failed;
				std::cerr << relativeClip << ": source FBX has no readable animation ("
					<< importer.GetErrorString() << ")\n";
				continue;
			}

			const aiAnimation* matchedAnimation = nullptr;
			for (unsigned int index = 0; index < scene->mNumAnimations; ++index)
			{
				std::string animationName = scene->mAnimations[index]->mName.C_Str();
				if (animationName.empty())
					animationName = matchedSource->stem().string() + "_clip" + std::to_string(index);
				if (SanitizeAnimationName(std::move(animationName)) == desiredAnimationName)
				{
					matchedAnimation = scene->mAnimations[index];
					break;
				}
			}
			if (!matchedAnimation)
			{
				++failed;
				std::cerr << relativeClip << ": animation '" << desiredAnimationName
					<< "' does not exist in " << matchedSource->string() << '\n';
				continue;
			}

			if (options.write)
			{
				VansGraphics::VansAnimationClip clip;
				VansGraphics::VansSkinnedMeshLoader::ExtractClipFromAssimp(
					matchedAnimation, skeleton, clip, scene);
				clip.clipName = desiredAnimationName;
				int rootIndex = -1;
				const auto namedRoot = skeleton.boneNameToIndex.find("root");
				if (namedRoot != skeleton.boneNameToIndex.end())
					rootIndex = namedRoot->second;
				else
					for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
						if (skeleton.bones[index].parentIndex < 0)
						{
							rootIndex = static_cast<int>(index);
							break;
						}
				clip.rootMotion.enabled = skeletal;
				clip.rootMotion.boneName = rootIndex >= 0
					? skeleton.bones[static_cast<std::size_t>(rootIndex)].name : "";
				clip.rootMotion.extractTranslation = true;
				clip.rootMotion.extractRotation = true;
				clip.rootMotion.extractScale = false;
				if (!VansGraphics::VansAnimationClipIO::Save(
					clipPath.string(), clip, skeleton))
				{
					++failed;
					std::cerr << relativeClip << ": failed to publish rebuilt clip\n";
					continue;
				}
			}
			++rebuilt;
			std::cout << (options.write ? "Rebuilt " : "Would rebuild ")
				<< relativeClip << " <- " << matchedSource->string() << '\n';
		}

		std::cout << "Animation Clips: " << clipFiles.size()
			<< ", rebuilt: " << rebuilt << ", failures: " << failed << '\n';
		return failed == 0 ? 0 : 1;
	}

	bool ContainsForbiddenGenerationField(const nlohmann::json& value, std::string& field)
	{
		if (value.is_object())
		{
			for (auto member = value.begin(); member != value.end(); ++member)
			{
				if (member.key() == "version" || member.key() == "schemaVersion"
					|| member.key() == "formatVersion")
				{
					field = member.key();
					return true;
				}
				if (ContainsForbiddenGenerationField(member.value(), field)) return true;
			}
		}
		else if (value.is_array())
			for (const auto& item : value)
				if (ContainsForbiddenGenerationField(item, field)) return true;
		return false;
	}

	bool ProcessAnimator(const fs::path& path, bool write, bool requireCanonical,
		std::size_t& changed, std::string& error)
	{
		nlohmann::json source;
		if (!Vans::VansJsonFileStorage::Read(path, source, error)) return false;
		std::string forbiddenField;
		if (ContainsForbiddenGenerationField(source, forbiddenField))
		{
			error = "Forbidden generation field '" + forbiddenField + "'.";
			return false;
		}
		VansGraphics::AnimatorAssetData asset;
		if (!VansGraphics::VansAnimatorIO::DeserializeFromJsonObject(source, asset, error)) return false;
		nlohmann::json canonical;
		if (!VansGraphics::VansAnimatorIO::SerializeToJsonObject(asset, canonical, error)) return false;
		if (source == canonical) return true;
		++changed;
		if (requireCanonical && !write)
		{
			error = "Asset is valid but is not in canonical form.";
			return false;
		}
		if (write && !VansGraphics::VansAnimatorIO::Save(path.string(), asset, error)) return false;
		return true;
	}

	bool ProcessBoneMask(const fs::path& path, bool write, bool requireCanonical,
		std::size_t& changed, std::string& error)
	{
		nlohmann::json source;
		if (!Vans::VansJsonFileStorage::Read(path, source, error)) return false;
		std::string forbiddenField;
		if (ContainsForbiddenGenerationField(source, forbiddenField))
		{
			error = "Forbidden generation field '" + forbiddenField + "'.";
			return false;
		}
		VansGraphics::VansBoneMaskAsset asset;
		if (!VansGraphics::VansBoneMaskStorage::DeserializeFromJsonObject(source, asset, error)) return false;
		nlohmann::json canonical;
		if (!VansGraphics::VansBoneMaskStorage::SerializeToJsonObject(asset, canonical, error)) return false;
		if (source == canonical) return true;
		++changed;
		if (requireCanonical && !write)
		{
			error = "Asset is valid but is not in canonical form.";
			return false;
		}
		if (write && !VansGraphics::VansBoneMaskStorage::SaveAtomic(path, asset, error)) return false;
		return true;
	}

	bool ProcessAnimationClip(const fs::path& path, bool write, bool requireCanonical,
		std::size_t& changed, std::string& error)
	{
		VansGraphics::VansAnimationClip clip;
		VansGraphics::Skeleton skeleton;
		if (!VansGraphics::VansAnimationClipIO::Load(path.string(), clip, skeleton))
		{
			error = "Animation Clip cannot be loaded by the current binary envelope.";
			return false;
		}

		VansGraphics::VansAnimationClipInfo info;
		if (VansGraphics::VansAnimationClipIO::Peek(path.string(), info))
			return true;

		++changed;
		if (requireCanonical && !write)
		{
			error = "Animation Clip is valid but is not self-contained canonical form.";
			return false;
		}
		if (write && !VansGraphics::VansAnimationClipIO::Save(path.string(), clip, skeleton))
		{
			error = "Failed to publish the canonical Animation Clip atomically.";
			return false;
		}
		return true;
	}

	bool ProcessAnimationRig(const fs::path& path,
		bool write,
		bool requireCanonical,
		Vans::VansAssetDatabase& database,
		std::unordered_map<std::string, VansGraphics::Skeleton>& skeletonCache,
		std::size_t& changed,
		std::string& error)
	{
		nlohmann::json source;
		if (!Vans::VansJsonFileStorage::Read(path, source, error))
			return false;
		std::string forbiddenField;
		if (ContainsForbiddenGenerationField(source, forbiddenField))
		{
			error = "Forbidden generation field '" + forbiddenField + "'.";
			return false;
		}

		VansGraphics::VansAnimationRigAsset asset;
		if (!VansGraphics::VansAnimationRigStorage::DeserializeFromJsonObject(source, asset, error))
			return false;
		nlohmann::json canonical;
		if (!VansGraphics::VansAnimationRigStorage::SerializeToJsonObject(asset, canonical, error))
			return false;

		Vans::VansAssetGuid skeletonGuid;
		if (!Vans::VansAssetGuid::TryParse(asset.skeletonGuid, skeletonGuid))
		{
			error = "Animation Rig skeletonGuid is invalid.";
			return false;
		}
		const auto skeletonRecord = database.Find(skeletonGuid);
		if (!skeletonRecord || skeletonRecord->type != Vans::VansAssetType::Model)
		{
			error = "Animation Rig skeletonGuid does not resolve to a Model asset.";
			return false;
		}

		auto skeletonIt = skeletonCache.find(asset.skeletonGuid);
		if (skeletonIt == skeletonCache.end())
		{
			VansGraphics::Skeleton skeleton;
			if (!VansGraphics::VansSkinnedMeshLoader::LoadSkeletonFromModelAsset(
				skeletonRecord->sourcePath.string(), skeleton, error))
				return false;
			skeletonIt = skeletonCache.emplace(asset.skeletonGuid, std::move(skeleton)).first;
		}

		VansGraphics::VansCompiledAnimationRig compiledRig;
		if (!VansGraphics::VansAnimationRigCompiler::Compile(
			asset, skeletonIt->second, compiledRig, error))
			return false;

		if (source == canonical)
			return true;
		++changed;
		if (requireCanonical && !write)
		{
			error = "Asset is valid but is not in canonical form.";
			return false;
		}
		if (write && !VansGraphics::VansAnimationRigStorage::SaveAtomic(path, asset, error))
			return false;
		return true;
	}

	bool ProcessRetargetProfile(const fs::path& path,
		bool write,
		bool requireCanonical,
		std::size_t& changed,
		std::string& error)
	{
		nlohmann::json source;
		if (!Vans::VansJsonFileStorage::Read(path, source, error))
			return false;
		VansGraphics::VansRetargetProfileAsset asset;
		if (!VansGraphics::VansRetargetProfileStorage::DeserializeFromJsonObject(
			source, asset, error))
			return false;
		nlohmann::json canonical;
		if (!VansGraphics::VansRetargetProfileStorage::SerializeToJsonObject(
			asset, canonical, error))
			return false;
		if (source == canonical)
			return true;
		++changed;
		if (requireCanonical && !write)
		{
			error = "Asset is valid but is not in canonical form.";
			return false;
		}
		return !write || VansGraphics::VansRetargetProfileStorage::SaveAtomic(path, asset, error);
	}

	int Run(const Options& options)
	{
		std::error_code fileError;
		const fs::path projectRoot = fs::weakly_canonical(options.projectRoot, fileError);
		if (fileError || !fs::is_directory(projectRoot))
		{
			std::cerr << "Invalid project root: " << options.projectRoot << '\n';
			return 2;
		}
		const fs::path assetsRoot = projectRoot / "Assets";
		if (!fs::is_directory(assetsRoot))
		{
			std::cerr << "Project has no Assets directory: " << assetsRoot << '\n';
			return 2;
		}
		if (options.command == Command::ImportAnimationFbx)
			return ImportAnimationFbx(projectRoot, fs::weakly_canonical(assetsRoot), options);
		if (options.command == Command::RebuildAnimationClips)
			return RebuildAnimationClips(
				projectRoot, fs::weakly_canonical(assetsRoot), options);
		if (options.command == Command::RefreshSkeletonSubAssets)
			return RefreshSkeletonSubAssets(
				projectRoot, fs::weakly_canonical(assetsRoot), options);
		if (options.command == Command::InspectSkeleton)
			return InspectSkeleton(projectRoot, fs::weakly_canonical(assetsRoot), options);
		if (options.command == Command::ValidateLuaScript)
		{
			const auto result = Vans::VansLuaScriptInspectorService::BuildDefaultFieldData(
				projectRoot, options.scriptPath.generic_string(), options.entryName);
			if (!result)
			{
				std::cerr << result.message << '\n';
				return 1;
			}
			std::cout << "Lua script valid: " << options.scriptPath.generic_string()
				<< " entry=\"" << options.entryName << "\" fields=" << result.fields.size() << '\n';
			return 0;
		}

		Vans::VansAssetDatabase database(assetsRoot);
		const Vans::VansAssetScanResult scanResult = database.Scan(
			Vans::VansAssetOperationPolicy::ReadOnly());
		if (!scanResult)
		{
			std::cerr << "Asset database scan failed:";
			for (const std::string& scanError : scanResult.errors)
				std::cerr << "\n  " << scanError;
			std::cerr << '\n';
			return 2;
		}

		std::vector<fs::path> assets;
		for (fs::recursive_directory_iterator iterator(assetsRoot,
			fs::directory_options::skip_permission_denied, fileError), end;
			iterator != end; iterator.increment(fileError))
		{
			if (fileError)
			{
				std::cerr << "Asset scan failed: " << fileError.message() << '\n';
				return 2;
			}
			if (!iterator->is_regular_file()) continue;
			const std::string extension = iterator->path().extension().string();
			if (extension == ".vanimator" || extension == ".vanimrig"
				|| extension == ".vretarget"
				|| extension == ".vbonemask" || extension == ".vclip")
				assets.push_back(iterator->path());
		}
		std::sort(assets.begin(), assets.end());

		std::size_t changed = 0;
		std::size_t failed = 0;
		const bool write = options.command == Command::Rewrite && options.write;
		const bool requireCanonical = options.command == Command::Validate;
		std::unordered_map<std::string, VansGraphics::Skeleton> skeletonCache;
		for (const fs::path& assetPath : assets)
		{
			std::string error;
			const std::size_t changedBefore = changed;
			const std::string extension = assetPath.extension().string();
			const bool ok = extension == ".vanimator"
				? ProcessAnimator(assetPath, write, requireCanonical, changed, error)
				: extension == ".vanimrig"
					? ProcessAnimationRig(assetPath, write, requireCanonical,
						database, skeletonCache, changed, error)
				: extension == ".vretarget"
					? ProcessRetargetProfile(assetPath, write, requireCanonical, changed, error)
				: extension == ".vbonemask"
					? ProcessBoneMask(assetPath, write, requireCanonical, changed, error)
					: ProcessAnimationClip(assetPath, write, requireCanonical, changed, error);
			if (!ok)
			{
				++failed;
				std::cerr << fs::relative(assetPath, projectRoot).generic_string()
					<< ": " << error << '\n';
			}
			else if (!write && !requireCanonical && changed > changedBefore)
			{
				std::cout << fs::relative(assetPath, projectRoot).generic_string()
					<< ": would rewrite to canonical form.\n";
			}
		}

		std::cout << "Animation assets: " << assets.size()
			<< ", canonical changes: " << changed
			<< ", failures: " << failed << '\n';
		if (options.command == Command::Rewrite && !write && changed > 0)
			std::cout << "Dry run only; rerun with --write to publish atomic canonical rewrites.\n";
		return failed == 0 ? 0 : 1;
	}
}

int main(int argc, char** argv)
{
	Options options;
	std::string error;
	if (!ParseOptions(argc, argv, options, error))
	{
		std::cerr << error << '\n';
		PrintUsage();
		return 2;
	}
	if (options.showHelp)
	{
		PrintUsage();
		return 0;
	}
	return Run(options);
}
