#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "../PhysicsCore/VansCollisionLayerConfig.h"
#include "VansProjectSettings.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace Vans
{
enum class VansProjectDocumentDomain : std::uint8_t
{
	ProjectConfig = 1u << 0u,
	RenderSettings = 1u << 1u,
	PhysicsSettings = 1u << 2u,
	CollisionLayers = 1u << 3u,
	AudioMix = 1u << 4u,
	GAFConfiguration = 1u << 5u,
	NavigationSettings = 1u << 6u,
};

constexpr std::uint8_t ProjectDocumentMask(VansProjectDocumentDomain domain)
{
	return static_cast<std::uint8_t>(domain);
}

constexpr bool HasProjectDocumentDomain(std::uint8_t mask, VansProjectDocumentDomain domain)
{
	return (mask & ProjectDocumentMask(domain)) != 0;
}

struct VansProjectDocumentSnapshot
{
	VansProjectSettings m_Settings;
	VansSerializedValue m_CollisionLayerDocument;
	VansSerializedValue m_AudioMixDocument;
	VansGAFProjectConfiguration m_GAFConfiguration;
	std::optional<VansEngine::VansCollisionLayerConfig> m_CollisionLayers;
	bool m_HasCollisionLayerDocument = false;
	bool m_HasAudioMixDocument = false;
	bool m_HasGAFConfiguration = false;
};

struct VansProjectDocumentFingerprint
{
	bool m_Valid = false;
	bool m_Exists = false;
	std::uintmax_t m_Size = 0;
	std::uint64_t m_ContentHash = 0;
	std::filesystem::file_time_type m_WriteTime{};
};
} // namespace Vans
