#include "../EngineCore/AnimationCore/VansAnimatorIO.h"
#include "../EngineCore/AnimationCore/VansAnimationClip.h"
#include "../EngineCore/AnimationCore/Storage/VansBoneMaskStorage.h"
#include "../EngineCore/AssetCore/Storage/VansJsonFileStorage.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	enum class Command
	{
		Invalid,
		Rewrite,
		Validate
	};

	struct Options
	{
		Command command = Command::Invalid;
		fs::path projectRoot;
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
			<< "  ForestAssetTool validate-animation-assets --project <path>\n";
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
		return true;
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
			if (extension == ".vanimator" || extension == ".vbonemask" || extension == ".vclip")
				assets.push_back(iterator->path());
		}
		std::sort(assets.begin(), assets.end());

		std::size_t changed = 0;
		std::size_t failed = 0;
		const bool write = options.command == Command::Rewrite && options.write;
		const bool requireCanonical = options.command == Command::Validate;
		for (const fs::path& assetPath : assets)
		{
			std::string error;
			const std::size_t changedBefore = changed;
			const std::string extension = assetPath.extension().string();
			const bool ok = extension == ".vanimator"
				? ProcessAnimator(assetPath, write, requireCanonical, changed, error)
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
