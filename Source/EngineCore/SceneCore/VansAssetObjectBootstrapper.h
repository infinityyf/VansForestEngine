#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../AssetCore/VansAssetDatabase.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Vans
{
	class VansAssetObjectRepository;

	struct VansAssetObjectBootstrapResult
	{
		std::size_t published = 0;
		std::vector<std::string> errors;

		explicit operator bool() const { return errors.empty(); }
	};

	class VansAssetObjectBootstrapper
	{
	public:
		static bool Supports(VansAssetType type);
		static VansAssetObjectBootstrapResult Publish(
			const std::vector<VansAssetRecord>& records,
			VansAssetObjectRepository& repository);
		static bool PublishSerialized(
			const VansAssetRecord& record,
			const VansSerializedValue& sourceRoot,
			std::uint64_t contentHash,
			VansAssetObjectRepository& repository,
			std::string& error);
		static bool PublishMetadataSerialized(
			const VansAssetRecord& record,
			const VansSerializedValue& metaRoot,
			std::uint64_t contentHash,
			VansAssetObjectRepository& repository,
			std::string& error);
	};
}
